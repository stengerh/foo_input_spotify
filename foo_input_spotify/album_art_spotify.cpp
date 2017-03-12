#include "pch.h"
#include "util.h"
#include "SpotifySession.h"
#include "SpotifyPlusPlus.h"
#include <future>
template <typename T>
void CALLBACK notifyEvent(T *result, void *userdata) {
	HANDLE ev = userdata;
	SetEvent(ev);
	CloseHandle(ev);
}

class album_art_extractor_instance_spotify : public album_art_extractor_instance
{
protected:
	sp_session *m_session;

public:
	album_art_extractor_instance_spotify(sp_session *session)
		: m_session(session)
	{
	}

	virtual void initialize(LockedCS &lock, abort_callback &p_abort) {}

	//! Throws exception_album_art_not_found when the requested album art entry could not be found in the referenced media file.
	virtual album_art_data::ptr query(const GUID & p_what, abort_callback & p_abort)
	{
		if (p_what == album_art_ids::artist)
		{
			console::formatter() << "Loading artist image from Spotify";

			SpotifyLockScope lock;

			SpotifyArtistPtr artist = get_artist(lock, p_abort);

			if (artist)
			{
				SpotifyArtistBrowsePtr browse;
				Event ev(false, false);
				browse.Attach(sp_artistbrowse_create(m_session, artist, SP_ARTISTBROWSE_NO_ALBUMS, &notifyEvent, ev.duplicateHandle()));
				lock.waitForEvent(ev, p_abort);

				const byte * image_id = sp_artist_portrait(artist, SP_IMAGE_SIZE_LARGE);

				if (image_id != nullptr)
				{
					return load_image_locked(image_id, lock, p_abort);
				}
			}
		}
		else if (p_what == album_art_ids::cover_front)
		{
			console::formatter() << "Loading cover image from Spotify";

			SpotifyLockScope lock;

			SpotifyAlbumPtr album = get_album(lock, p_abort);

			if (album)
			{
				SpotifyAwaitLoaded(album.m_ptr, lock, p_abort);

				const byte * image_id = sp_album_cover(album, SP_IMAGE_SIZE_LARGE);

				if (image_id != nullptr)
				{
					return load_image_locked(image_id, lock, p_abort);
				}
			}
		}
		else
		{
			console::formatter() << "Unsupported image type for Spotify album or track link";
		}

		throw exception_album_art_not_found();
	}

	album_art_data_ptr load_image_locked(const byte * image_id, LockedCS & lock, abort_callback & p_abort)
	{
		SpotifyImagePtr image;
		image.Attach(sp_image_create(m_session, image_id));

		SpotifyAwaitLoaded(image.m_ptr, lock, p_abort);

		size_t data_size = 0;
		const void * data = sp_image_data(image, &data_size);

		if (data != nullptr && data_size > 0)
			return album_art_data_impl::g_create(data, data_size);
		else
			throw exception_album_art_not_found();
	}

	virtual SpotifyAlbumPtr get_album(LockedCS & lock, abort_callback & p_abort)
	{
		return nullptr;
	}

	virtual SpotifyArtistPtr get_artist(LockedCS & lock, abort_callback & p_abort)
	{
		return nullptr;
	}
};

class album_art_extractor_instance_spotify_album : public album_art_extractor_instance_spotify
{
private:
	SpotifyAlbumPtr m_album;

public:
	album_art_extractor_instance_spotify_album(const SpotifyAlbumPtr & album, sp_session *session)
		: album_art_extractor_instance_spotify(session)
		, m_album(album)
	{
	}

	virtual void initialize(LockedCS &lock, abort_callback &p_abort)
	{
		Event ev(false, false);
		SpotifyAlbumBrowsePtr browse;
		browse.Attach(sp_albumbrowse_create(m_session, m_album, &notifyEvent, ev.duplicateHandle()));

		lock.waitForEvent(ev, p_abort);
	}

	virtual SpotifyAlbumPtr get_album(LockedCS & lock, abort_callback & p_abort)
	{
		return m_album;
	}

	virtual SpotifyArtistPtr get_artist(LockedCS & lock, abort_callback & p_abort)
	{
		return sp_album_artist(m_album);
	}
};

class album_art_extractor_instance_spotify_artist : public album_art_extractor_instance_spotify
{
private:
	SpotifyArtistPtr m_artist;

public:
	album_art_extractor_instance_spotify_artist(const SpotifyArtistPtr & artist, sp_session *session)
		: album_art_extractor_instance_spotify(session)
		, m_artist(artist)
	{
	}

	virtual void initialize(LockedCS &lock, abort_callback &p_abort)
	{
		Event ev(false, false);
		SpotifyArtistBrowsePtr browse;
		browse.Attach(sp_artistbrowse_create(m_session, m_artist, SP_ARTISTBROWSE_NO_ALBUMS, &notifyEvent, ev.duplicateHandle()));

		lock.waitForEvent(ev, p_abort);
	}

	virtual SpotifyAlbumPtr get_album(LockedCS & lock, abort_callback & p_abort)
	{
		return nullptr;
	
	}

	virtual SpotifyArtistPtr get_artist(LockedCS & lock, abort_callback & p_abort)
	{
		return m_artist;
	}
};

class album_art_extractor_instance_spotify_track : public album_art_extractor_instance_spotify
{
private:
	SpotifyTrackPtr m_track;

public:
	album_art_extractor_instance_spotify_track(const SpotifyTrackPtr & track, sp_session *session)
		: album_art_extractor_instance_spotify(session)
		, m_track(track)
	{
	}

	virtual void initialize(LockedCS &lock, abort_callback &p_abort)
	{
		SpotifyAwaitLoaded(m_track.m_ptr, lock, p_abort);

		SpotifyAlbumPtr album = sp_track_album(m_track);

		Event ev(false, false);
		SpotifyAlbumBrowsePtr browse;
		browse.Attach(sp_albumbrowse_create(m_session, album, &notifyEvent, ev.duplicateHandle()));

		lock.waitForEvent(ev, p_abort);
	}

	virtual SpotifyAlbumPtr get_album(LockedCS & lock, abort_callback & p_abort)
	{
		return sp_track_album(m_track);
	}

	virtual SpotifyArtistPtr get_artist(LockedCS & lock, abort_callback & p_abort)
	{
		return sp_track_artist(m_track, 0);
	}
};

class album_art_extractor_instance_spotify_playlist : public album_art_extractor_instance_spotify
{
private:
	SpotifyLinkPtr m_link;
	SpotifyPlaylistPtr m_playlist;

public:
	album_art_extractor_instance_spotify_playlist(const SpotifyLinkPtr &link, sp_session *session)
		: album_art_extractor_instance_spotify(session)
		, m_link(link)
	{
	}

	virtual void initialize(LockedCS &lock, abort_callback &p_abort)
	{
	}

	template <typename F>
	void in_main_thread(F f)
	{
		struct in_main : main_thread_callback
		{
			void callback_run() override
			{
				f();
			}

			in_main(F f) : f(f) {}
			F f;
		};
		static_api_ptr_t<main_thread_callback_manager>()->add_callback(new service_impl_t<in_main>(f));
	}

	virtual album_art_data::ptr query(const GUID & p_what, abort_callback & p_abort)
	{
		if (p_what == album_art_ids::cover_front)
		{
			console::formatter() << "Loading cover image from Spotify";

			SpotifyLockScope lock;

			if (!m_playlist)
			{
				m_playlist.Attach(sp_playlist_create(m_session, m_link));

				SpotifyAwaitLoaded(m_playlist.m_ptr, lock, p_abort);
			}

			//Size of the playlist from m_playlist
			int playlistSize = sp_playlist_num_tracks(m_playlist);
			
			//Ask main thread for the playlist index of the selected song
			std::promise<int> promisePlaylistIndex;
			bool isPlaying = false;
			std::string playingLink;

			in_main_thread(
				[playlistSize, &isPlaying, &playingLink, &promisePlaylistIndex]
			{
				int indexValue = 0;

				static_api_ptr_t<playback_control> playbackControl;
				static_api_ptr_t<playlist_manager> playlistManager;
				static_api_ptr_t<ui_selection_manager> uiSelectionManager;
				
				metadb_handle_ptr playingTrack;
				playbackControl->get_now_playing(playingTrack);
				//Check if last thing clicked is a track. Used for showing played track
				if (uiSelectionManager->get_selection_type() == contextmenu_item::caller_active_playlist_selection)
				{
					pfc::list_t<metadb_handle_ptr> selectedItems;
					playlistManager->activeplaylist_get_selected_items(selectedItems);
					if (selectedItems.get_size() <= 0 || selectedItems.get_item(0)->get_location().get_subsong_index() > playlistSize)
					{
						promisePlaylistIndex.set_value(0);
					}
					else
					{
						indexValue = selectedItems.get_item(0)->get_location().get_subsong_index();
						promisePlaylistIndex.set_value(indexValue);
					}
				}
				else if (playingTrack.get_ptr() != nullptr)
				{
					playingLink = playingTrack->get_path();
					if (playingLink.find("spotify") == 0) //Check if its a spotify link so it can get the playlist link and get the track
					{
						isPlaying = true;
						promisePlaylistIndex.set_value(playingTrack->get_location().get_subsong_index());
					}
					else
					{
						promisePlaylistIndex.set_value(0);
					}
				}
				else
				{
					promisePlaylistIndex.set_value(0);
				}
			}
			);
			//wait for the value to be avalible
			int playlistIndex = promisePlaylistIndex.get_future().get();

			SpotifyTrackPtr selectedTrack;
			SpotifyAlbumPtr tracksAlbum;
			//If isPlaying is true, then set the specific tracks cover
			if (isPlaying)
			{
				SpotifyLinkPtr spotifyLink = sp_link_create_from_string(playingLink.c_str());
				SpotifyPlaylistPtr spotifyPlaylist = sp_playlist_create(m_session, spotifyLink);
				selectedTrack.m_ptr = sp_playlist_track(spotifyPlaylist, playlistIndex);
				tracksAlbum.m_ptr = sp_track_album(selectedTrack);
			}
			else
			{
				selectedTrack.m_ptr = sp_playlist_track(m_playlist, playlistIndex);
				tracksAlbum.m_ptr = sp_track_album(selectedTrack);
			}

			const byte * image_id = sp_album_cover(tracksAlbum, SP_IMAGE_SIZE_LARGE);

			if (image_id != nullptr)
			{
				return load_image_locked(image_id, lock, p_abort);
			}
		}
		else
		{
			console::formatter() << "Unsupported image type for Spotify playlist link";
		}

		throw exception_album_art_not_found();
	}
};

class album_art_extractor_spotify : public album_art_extractor
{
public:
	//! Returns whether the specified file is one of formats supported by our album_art_extractor implementation.
	//! @param p_path Path to file being queried.
	//! @param p_extension Extension of file being queried (also present in p_path parameter) - provided as a separate parameter for performance reasons.
	virtual bool is_our_path(const char * p_path, const char * p_extension)
	{
		return !strncmp(p_path, "spotify:", strlen("spotify:"));
	}

	//! Instantiates album_art_extractor_instance providing access to album art stored in a specified media file. \n
	//! Throws one of I/O exceptions on failure; exception_album_art_not_found when the file has no album art record at all.
	//! @param p_filehint Optional; specifies a file interface to use for accessing the specified file; can be null - in that case, the implementation will open and close the file internally.
	virtual album_art_extractor_instance::ptr open(file_ptr p_filehint, const char * p_path, abort_callback & p_abort)
	{
		console::formatter() << "Opening track for loading album art from Spotify: " << p_path;

		sp_session *session = SpotifySession::instance().get(p_abort);
		SpotifyLockScope lock;

		SpotifyLinkPtr link;
		link.Attach(sp_link_create_from_string(p_path));

		service_ptr_t<album_art_extractor_instance_spotify> instance;


		if (link)
		{
			switch (sp_link_type(link))
			{
			case SP_LINKTYPE_ALBUM:
				console::formatter() << "Creating album art extractor for Spotify album link";
				instance = new service_impl_t<album_art_extractor_instance_spotify_album>(sp_link_as_album(link), session);
				break;

			case SP_LINKTYPE_ARTIST:
				console::formatter() << "Creating album art extractor for Spotify artist link";
				instance = new service_impl_t<album_art_extractor_instance_spotify_artist>(sp_link_as_artist(link), session);
				break;

			case SP_LINKTYPE_TRACK:
				console::formatter() << "Creating album art extractor for Spotify track link";
				instance = new service_impl_t<album_art_extractor_instance_spotify_track>(sp_link_as_track(link), session);
				break;

			case SP_LINKTYPE_PLAYLIST:
				console::formatter() << "Creating album art extractor for Spotify playlist link";
				instance = new service_impl_t<album_art_extractor_instance_spotify_playlist>(link, session);
				break;

			default:
				console::formatter() << "Unsupported type of Spotify link";
				break;
			}
		}
		else
		{
			console::formatter() << "Not a valid Spotify link";
		}

		if (instance.is_valid())
		{
			console::formatter() << "Initializing album art extractor for Spotify link";
			instance->initialize(lock, p_abort);
			return instance;
		}
		else
		{
			throw exception_album_art_not_found();
		}
	}
};

static service_factory_single_t<album_art_extractor_spotify> g_album_art_extractory_spotify_factory;
