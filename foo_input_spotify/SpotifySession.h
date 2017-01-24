#pragma once

#include "util.h"
#include <libspotify/api.h>

struct SpotifyThreadData {
	SpotifyThreadData(CriticalSection &cs) : cs(cs) {
	}

	HANDLE processEventsEvent;
	CriticalSection &cs;
	sp_session *sess;
};

#if defined(_MSC_VER)
#if _MSC_VER < 1900
#define POINTER_ALIGN __declspec(align(__alignof(PVOID)))
#else
#define POINTER_ALIGN alignas(alignof(PVOID))
#endif
#else
#error PORTME
#endif

class SpotifySession {
	sp_session *sp;
	SpotifyThreadData threadData;
	CriticalSection spotifyCS;
	HANDLE processEventsEvent;
	POINTER_ALIGN volatile PVOID decoderOwner;
	CriticalSection loginCS;
	ConditionVariable loginCondVar;
	bool loggingIn;
	bool loggedIn;

	SpotifySession();
	~SpotifySession();

public:
	static SpotifySession & instance();

	Buffer buf;

	sp_session *getAnyway();

	sp_session *get(abort_callback & p_abort);

	CriticalSection &getSpotifyCS();

	void showLoginUI(sp_error last_login_result = SP_ERROR_OK);
	void requireLoggedIn();
	void waitForLogin(abort_callback & p_abort);

	void onLoggedIn(sp_error err);
	void onLoggedOut();

	void processEvents();

	void takeDecoder(void *owner);
	void ensureDecoder(void *owner);
	void releaseDecoder(void *owner);
	bool hasDecoder(void *owner);
};

void assertSucceeds(pfc::string8 msg, sp_error err);
