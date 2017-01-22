#include "util.h"

#include <foobar2000.h>
#include <libspotify/api.h>

#include <shlobj.h>

#include <stdint.h>
#include <stdlib.h>

#include "SpotifySession.h"

#include "cred_prompt.h"

extern "C" {
	extern const uint8_t g_appkey[];
	extern const size_t g_appkey_size;
}

SpotifySession & SpotifySession::instance()
{
	static SpotifySession session;

	return session;
}

DWORD WINAPI spotifyThread(void *data) {
	SpotifyThreadData *dat = (SpotifyThreadData*)data;

	int nextTimeout = INFINITE;
	while (true) {
		DWORD result = WaitForSingleObject(dat->processEventsEvent, nextTimeout);
		switch (result) {
		case WAIT_OBJECT_0:
		{
			LockedCS lock(dat->cs);
			sp_session_process_events(dat->sess, &nextTimeout);
		}
		}
	}
}

pfc::string8 &doctor(pfc::string8 &msg, sp_error err) {
	msg += " failed: ";
	msg += sp_error_message(err);
	return msg;
}

void alert(pfc::string8 msg) {
	console::complain("boom", msg.toString());
}

/* @param msg "logging in" */
void assertSucceeds(pfc::string8 msg, sp_error err) {
	if (SP_ERROR_OK == err)
		return;

	throw exception_io_data(doctor(msg, err));
}

void alertIfFailure(pfc::string8 msg, sp_error err) {
	if (SP_ERROR_OK == err)
		return;
	alert(doctor(msg, err));
}

void CALLBACK log_message(sp_session *sess, const char *error);
void CALLBACK message_to_user(sp_session *sess, const char *error);
void CALLBACK start_playback(sp_session *sess);
void CALLBACK logged_in(sp_session *sess, sp_error error);
void CALLBACK notify_main_thread(sp_session *sess);
int CALLBACK music_delivery(sp_session *sess, const sp_audioformat *format, const void *frames, int num_frames);
void CALLBACK end_of_track(sp_session *sess);
void CALLBACK play_token_lost(sp_session *sess);

//BOOL CALLBACK makeSpotifySession(PINIT_ONCE initOnce, PVOID param, PVOID *context);

SpotifySession::SpotifySession() :
		threadData(spotifyCS), decoderOwner(NULL) {

	processEventsEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	loggingIn = false;

	static sp_session_callbacks session_callbacks = {};
	static sp_session_config spconfig = {};

	PWSTR path;
	if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &path))
		throw pfc::exception("couldn't get local app data path");

	size_t num;
	char lpath[MAX_PATH];
	if (wcstombs_s(&num, lpath, MAX_PATH, path, MAX_PATH)) {
		CoTaskMemFree(path);
		throw pfc::exception("couldn't convert local app data path");
	}
	CoTaskMemFree(path);

	if (strcat_s(lpath, "\\foo_input_spotify"))
		throw pfc::exception("couldn't append to path");

	spconfig.api_version = SPOTIFY_API_VERSION,
	spconfig.cache_location = lpath;
	spconfig.settings_location = lpath;
	spconfig.application_key = g_appkey;
	spconfig.application_key_size = g_appkey_size;
	spconfig.user_agent = "spotify-foobar2000-faux-" MYVERSION;
	spconfig.userdata = this;
	spconfig.callbacks = &session_callbacks;

	session_callbacks.logged_in = &logged_in;
	session_callbacks.notify_main_thread = &notify_main_thread;
	session_callbacks.music_delivery = &music_delivery;
	session_callbacks.play_token_lost = &play_token_lost;
	session_callbacks.end_of_track = &end_of_track;
	session_callbacks.log_message = &log_message;
	session_callbacks.message_to_user = &message_to_user;
	session_callbacks.start_playback = &start_playback;

	{
		LockedCS lock(spotifyCS);

		assertSucceeds("creating session", sp_session_create(&spconfig, &sp));
	}

	threadData.processEventsEvent = processEventsEvent;
	threadData.sess = sp;

	SetLastError(ERROR_SUCCESS);
	const HANDLE thread = CreateThread(NULL, 0, &spotifyThread, &threadData, 0, NULL);
	if (NULL == thread) {
		throw win32exception("Couldn't create thread");
	}
	CloseHandle(thread);
}

SpotifySession::~SpotifySession() {
	CloseHandle(processEventsEvent);
}

sp_session *SpotifySession::getAnyway() {
	return sp;
}

struct SpotifySessionData {
	SpotifySessionData(abort_callback & p_abort, SpotifySession *ss) : p_abort(p_abort), ss(ss) {}
	abort_callback & p_abort;
	SpotifySession *ss;
};

sp_session *SpotifySession::get(abort_callback & p_abort) {
	requireLoggedIn();
	waitForLogin(p_abort);
	return getAnyway();
}

CriticalSection &SpotifySession::getSpotifyCS() {
	return spotifyCS;
}

class main_thread_callback_spotify_login : public main_thread_callback {
private:
	sp_session * const session;
	CriticalSection * cs;
	bool * const loggingIn;
	const sp_error lastLoginResult;

public:
	main_thread_callback_spotify_login(sp_session *session, CriticalSection * cs, bool * loggingIn, sp_error lastLoginResult)
		: session(session)
		, cs(cs)
		, loggingIn(loggingIn)
		, lastLoginResult(lastLoginResult)
	{
	}

	virtual void callback_run() {
		const char * msg = nullptr;
		if (lastLoginResult != SP_ERROR_OK) {
			msg = sp_error_message(lastLoginResult);
		}
		std::auto_ptr<CredPromptResult> cpr = credPrompt(msg);
		if (cpr->cancelled) {
			LockedCS lock(*cs);
			*loggingIn = false;
		}
		else {
			LockedCS lock(SpotifySession::instance().getSpotifyCS());
			sp_error loginResult = sp_session_login(session, cpr->un.data(), cpr->pw.data(), /*remember_me*/ false, /*blob*/ nullptr);
		}
	}
};

void SpotifySession::showLoginUI(sp_error last_login_result) {
	service_ptr_t<main_thread_callback_spotify_login> callback = new service_impl_t<main_thread_callback_spotify_login>(getAnyway(), &loginCS, &loggingIn, last_login_result);
	callback->callback_enqueue();

	loggingIn = true;
}

void SpotifySession::requireLoggedIn() {
	LockedCS lock(getSpotifyCS());

	sp_session * session = getAnyway();

	sp_connectionstate state = sp_session_connectionstate(session);
	switch (state) {
	case SP_CONNECTION_STATE_LOGGED_IN:
	case SP_CONNECTION_STATE_OFFLINE:
		break;

	case SP_CONNECTION_STATE_LOGGED_OUT:
	case SP_CONNECTION_STATE_UNDEFINED:
	case SP_CONNECTION_STATE_DISCONNECTED:
	{
		sp_error reloginResult = sp_session_relogin(session);
		switch (reloginResult) {
		case SP_ERROR_OK:
			break;
		case SP_ERROR_NO_CREDENTIALS:
			if (!loggingIn) {
				showLoginUI();
			}
			break;
		default:
			break;
		}
	}
	}
}

void SpotifySession::waitForLogin(abort_callback & p_abort) {
	LockedCS lock(loginCS);
	while (!loggedIn) {
		loginCondVar.sleep(loginCS, 100);
		p_abort.check();
	}
}

void SpotifySession::onLoggedIn(sp_error err) {
	LockedCS lock(loginCS);

	if (SP_ERROR_OK == err) {
		loggingIn = false;
		loggedIn = true;
	}
	else {
		loggedIn = false;
		if (loggingIn) {
			showLoginUI(err);
		}
	}

	loginCondVar.wakeAll();
}

void SpotifySession::onLoggedOut() {
	LockedCS lock(loginCS);

	loggedIn = false;

	loginCondVar.wakeAll();
}

void SpotifySession::processEvents() {
	SetEvent(processEventsEvent);
}

bool SpotifySession::hasDecoder(void *owner) {
	return decoderOwner == owner;
}

void SpotifySession::takeDecoder(void *owner) {
	if (!hasDecoder(NULL))
		throw exception_io_data("Someone else is already decoding");
 
	InterlockedCompareExchangePointer(&decoderOwner, owner, NULL);

	if (!hasDecoder(owner))
		throw exception_io_data("Someone else beat us to the decoder");
}

void SpotifySession::ensureDecoder(void *owner) {
	if (!hasDecoder(owner))
		throw exception_io_data("bugcheck: we should own the decoder...");
}

void SpotifySession::releaseDecoder(void *owner) {
	InterlockedCompareExchangePointer(&decoderOwner, NULL, owner);
}

/** sp_session_userdata is assumed to be thread safe. */
SpotifySession *from(sp_session *sess) {
	return static_cast<SpotifySession *>(sp_session_userdata(sess));
}

void SP_CALLCONV log_message(sp_session *sess, const char *error) {
	console::formatter() << "spotify log: " << error;
}

void SP_CALLCONV message_to_user(sp_session *sess, const char *message) {
	alert(message);
}

void SP_CALLCONV start_playback(sp_session *sess) {
	return;
}

void SP_CALLCONV logged_in(sp_session *sess, sp_error error)
{
	from(sess)->onLoggedIn(error);
}

void SP_CALLCONV logged_out(sp_session *sess)
{
	from(sess)->onLoggedOut();
}

void SP_CALLCONV notify_main_thread(sp_session *sess)
{
    from(sess)->processEvents();
}

int SP_CALLCONV music_delivery(sp_session *sess, const sp_audioformat *format,
                          const void *frames, int num_frames)
{
	if (num_frames == 0) {
		from(sess)->buf.flush();
        return 0;
	}

	if (from(sess)->buf.isFull()) {
		return 0;
	}

	const size_t s = num_frames * sizeof(int16_t) * format->channels;

	void *data = new char[s];
	memcpy(data, frames, s);

	from(sess)->buf.add(data, s, format->sample_rate, format->channels);

	return num_frames;
}

void SP_CALLCONV end_of_track(sp_session *sess)
{
	from(sess)->buf.add(NULL, 0, 0, 0);
}

void SP_CALLCONV play_token_lost(sp_session *sess)
{
	alert("play token lost (someone's using your account elsewhere)");
}
