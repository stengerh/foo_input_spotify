#include "pch.h"

#include "util.h"

#include "../helpers/dropdown_helper.h"
#include <functional>
#include <shlobj.h>

#include <stdint.h>
#include <stdlib.h>
#include <vector>

#include "SpotifySession.h"
#include "SpotifyPlusPlus.h"

extern "C" {
	extern const uint8_t g_appkey[];
	extern const size_t g_appkey_size;
}

template <typename T>
void CALLBACK notifyEvent(T *result, void *userdata) {
	HANDLE ev = userdata;
	SetEvent(ev);
	CloseHandle(ev);
}

class InputSpotify
{
	t_filestats m_stats;

	std::string url;
	std::vector<SpotifyTrackPtr> t;
	typedef std::vector<SpotifyTrackPtr>::iterator tr_iter;

	int channels;
	int sampleRate;

#define FOR_TRACKS() for (tr_iter it = t.begin(); it != t.end(); ++it)

	void freeTracks() {
		t.clear();
	}

	SpotifySession &ss;

public:

	InputSpotify() : ss(SpotifySession::instance()) {
	}

	~InputSpotify() {
		freeTracks();
		ss.releaseDecoder(this);
	}

	void open( service_ptr_t<file> m_file, const char * p_path, t_input_open_reason p_reason, abort_callback & p_abort )
	{
		if ( p_reason == input_open_info_write ) throw exception_io_denied_readonly();
		url = p_path;

		sp_session *sess = ss.get(p_abort);
		switch (sp_session_connectionstate(sess))
		{
		case SP_CONNECTION_STATE_LOGGED_IN:
		case SP_CONNECTION_STATE_OFFLINE:
			break;
		default:
			throw exception_io_denied("could not log in to Spotify");
		}

		{
			LockedCS lock(ss.getSpotifyCS());

			SpotifyLinkPtr link;
			link.Attach(sp_link_create_from_string(p_path));
			if (!link)
				throw exception_io_data("couldn't parse url");

			freeTracks();

			switch(sp_link_type(link)) {
				case SP_LINKTYPE_ALBUM: {
					SpotifyAlbumPtr album = sp_link_as_album(link);

					Event ev(false, false);
					SpotifyAlbumBrowsePtr browse = sp_albumbrowse_create(sess, album, &notifyEvent, ev.duplicateHandle());

					while (!sp_albumbrowse_is_loaded(browse)) {
						lock.waitForEvent(ev, p_abort);
					}

					const int count = sp_albumbrowse_num_tracks(browse);
					if (0 == count)
						throw exception_io_data("empty (or failed to load?) album");

					for (int i = 0; i < count; ++i) {
						SpotifyTrackPtr track = sp_albumbrowse_track(browse, i);
						t.push_back(track);
					}
				} break;

				case SP_LINKTYPE_PLAYLIST: {
					SpotifyPlaylistPtr playlist;
					playlist.Attach(sp_playlist_create(sess, link));

					SpotifyAwaitLoaded(playlist.m_ptr, lock, p_abort);

					int count = sp_playlist_num_tracks(playlist);
					if (0 == count)
						throw exception_io_data("empty (or failed to load?) playlist");

					for (int i = 0; i < count; ++i) {
						SpotifyTrackPtr track = sp_playlist_track(playlist, i);
						t.push_back(track);
					}
				} break;

				case SP_LINKTYPE_ARTIST: {
					SpotifyArtistPtr artist = sp_link_as_artist(link);

					Event ev(false, false);
					SpotifyArtistBrowsePtr browse;
					browse.Attach(sp_artistbrowse_create(sess, sp_link_as_artist(link), SP_ARTISTBROWSE_FULL, &notifyEvent, ev.duplicateHandle()));

					while (!sp_artistbrowse_is_loaded(browse)) {
						lock.waitForEvent(ev, p_abort);
					}

					const int count = sp_artistbrowse_num_tracks(browse);
					if (0 == count)
						throw exception_io_data("empty (or failed to load?) artist");

					for (int i = 0; i < count; ++i) {
						SpotifyTrackPtr track = sp_artistbrowse_track(browse, i);
						t.push_back(track);
					}
				} break;

				case SP_LINKTYPE_SEARCH: {
					std::string query = p_path;
					query = query.substr(15, sizeof(p_path) - 15);

					// spotify:search:

					Event ev(false, false);
					SpotifySearchPtr browse;
					browse.Attach(sp_search_create(sess, query.c_str(), 0, 200, 0, 10, 0, 10, 0, 20, SP_SEARCH_SUGGEST, &notifyEvent, ev.duplicateHandle()));

					while (!sp_search_is_loaded(browse)) {
						lock.waitForEvent(ev, p_abort);
					}

					const int count = sp_search_num_tracks(browse);
					if (0 == count)
						throw exception_io_data("empty (or failed to load?) search");

					for (int i = 0; i < count; ++i) {
						SpotifyTrackPtr track = sp_search_track(browse, i);
						t.push_back(track);
					}
				} break;

				case SP_LINKTYPE_TRACK: {
					SpotifyTrackPtr ptr = sp_link_as_track(link);
					t.push_back(ptr);
				} break;

				default:
					throw exception_io_data("Only artist, track, playlist and album URIs are supported");
			}
		}

		while (true) {
			{
				LockedCS lock(ss.getSpotifyCS());
				size_t done = 0;
				FOR_TRACKS() {
					const sp_error e = sp_track_error(*it);
					if (SP_ERROR_OK == e)
						++done;
					else if (SP_ERROR_IS_LOADING != e)
						assertSucceeds("preloading track", e);
				}

				if (done == t.size())
					break;
			}

			p_abort.sleep(0.05);
		}
	}

	void meta_add_if_positive(file_info &p_info, const char * p_name, int p_value)
	{
		if (p_value > 0) {
			p_info.meta_add(p_name, pfc::format_int(p_value));
		}
	}

	void get_info(t_int32 subsong, file_info & p_info, abort_callback & p_abort )
	{
		LockedCS lock(ss.getSpotifyCS());
		sp_track *tr = t.at(subsong);
		p_info.set_length(sp_track_duration(tr)/1000.0);
		const int artist_count = sp_track_num_artists(tr);
		for (int artist_index = 0; artist_index < artist_count; ++artist_index) {
			p_info.meta_add("ARTIST", sp_artist_name(sp_track_artist(tr, artist_index)));
		}
		p_info.meta_add("ALBUM ARTIST", sp_artist_name(sp_album_artist(sp_track_album(tr))));
		p_info.meta_add("ALBUM", sp_album_name(sp_track_album(tr)));
		p_info.meta_add("TITLE", sp_track_name(tr));
		meta_add_if_positive(p_info, "TRACKNUMBER", sp_track_index(tr));
		meta_add_if_positive(p_info, "DISCNUMBER", sp_track_disc(tr));
		meta_add_if_positive(p_info, "DATE", sp_album_year(sp_track_album(tr)));
	}

	t_filestats get_file_stats( abort_callback & p_abort )
	{
		return m_stats;
	}

	void decode_initialize(t_int32 subsong, unsigned p_flags, abort_callback & p_abort )
	{
		if ((p_flags & input_flag_playback) == 0)
			throw exception_io_denied();

		ss.takeDecoder(this);

		ss.buf.flush();
		sp_session *sess = ss.get(p_abort);

		LockedCS lock(ss.getSpotifyCS());
		assertSucceeds("load track (including region check)", sp_session_player_load(sess, t.at(subsong)));
		sp_session_player_play(sess, 1);
	}

	bool decode_run( audio_chunk & p_chunk, abort_callback & p_abort )
	{
		ss.ensureDecoder(this);

		Gentry *e = ss.buf.take(&p_abort);

		if (NULL == e->data) {
			ss.buf.free(e);
			ss.releaseDecoder(this);
			return false;
		}

		p_chunk.set_data_fixedpoint(
			e->data,
			e->size,
			e->sampleRate,
			e->channels,
			16,
			audio_chunk::channel_config_stereo);

		channels = e->channels;
		sampleRate = e->sampleRate;

		ss.buf.free(e);

		return true;
	}

	void decode_seek( double p_seconds,abort_callback & p_abort )
	{
		ss.ensureDecoder(this);

		ss.buf.flush();
		sp_session *sess = ss.get(p_abort);
		LockedCS lock(ss.getSpotifyCS());
		sp_session_player_seek(sess, static_cast<int>(p_seconds*1000));
	}

	bool decode_can_seek()
	{
		return true;
	}

	bool decode_get_dynamic_info( file_info & p_out, double & p_timestamp_delta )
	{
		p_out.info_set_int("CHANNELS", channels);
		p_out.info_set_int("SAMPLERATE", sampleRate);
		return true;
	}

	bool decode_get_dynamic_info_track( file_info & p_out, double & p_timestamp_delta )
	{
		return false;
	}

	void decode_on_idle( abort_callback & p_abort ) { }

	void retag_set_info( t_int32 subsong, const file_info & p_info, abort_callback & p_abort )
	{
		throw exception_io_data();
	}

	void retag_commit( abort_callback & p_abort )
	{
		throw exception_io_data();
	}

	static bool g_is_our_content_type( const char * p_content_type )
	{
		return false;
	}

	static bool g_is_our_path( const char * p_full_path, const char * p_extension )
	{
		return !strncmp( p_full_path, "spotify:", strlen("spotify:") );
	}

	t_uint32 get_subsong_count() {
		return t.size();
	}

	t_uint32 get_subsong(t_uint32 song) {
		return song;
	}
};

static input_factory_t< InputSpotify > inputFactorySpotify;

DECLARE_COMPONENT_VERSION("Spotify Decoder", MYVERSION, "Support for spotify: urls.");
