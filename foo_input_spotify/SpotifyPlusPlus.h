#pragma once

#include <libspotify/api.h>
#include "SpotifySession.h"
#include "util.h"

class SpotifyLockScope : public LockedCS
{
public:
	SpotifyLockScope(SpotifySession & session = SpotifySession::instance())
		: LockedCS(session.getSpotifyCS())
	{
	}
};

template <typename T>
class SpotifyPtr;

typedef SpotifyPtr<sp_album> SpotifyAlbumPtr;
typedef SpotifyPtr<sp_albumbrowse> SpotifyAlbumBrowsePtr;
typedef SpotifyPtr<sp_artist> SpotifyArtistPtr;
typedef SpotifyPtr<sp_artistbrowse> SpotifyArtistBrowsePtr;
typedef SpotifyPtr<sp_image> SpotifyImagePtr;
typedef SpotifyPtr<sp_link> SpotifyLinkPtr;
typedef SpotifyPtr<sp_playlist> SpotifyPlaylistPtr;
typedef SpotifyPtr<sp_search> SpotifySearchPtr;
typedef SpotifyPtr<sp_track> SpotifyTrackPtr;

template <typename T>
struct SpotifyTraits
{
};

template <>
struct SpotifyTraits<sp_album>
{
	static void AddRef(sp_album * album) {
		sp_album_add_ref(album);
	}

	static void Release(sp_album * album) {
		sp_album_release(album);
	}

	static bool IsLoaded(sp_album * album) {
		return sp_album_is_loaded(album);
	}
};

template <>
struct SpotifyTraits<sp_albumbrowse>
{
	static void AddRef(sp_albumbrowse * albumbrowse) {
		sp_albumbrowse_add_ref(albumbrowse);
	}

	static void Release(sp_albumbrowse * albumbrowse) {
		sp_albumbrowse_release(albumbrowse);
	}

	static bool IsLoaded(sp_albumbrowse * albumbrowse) {
		return sp_albumbrowse_is_loaded(albumbrowse);
	}
};

template <>
struct SpotifyTraits<sp_artist>
{
	static void AddRef(sp_artist * artist) {
		sp_artist_add_ref(artist);
	}

	static void Release(sp_artist * artist) {
		sp_artist_release(artist);
	}

	static bool IsLoaded(sp_artist * artist) {
		return sp_artist_is_loaded(artist);
	}
};

template <>
struct SpotifyTraits<sp_artistbrowse>
{
	static void AddRef(sp_artistbrowse * artistbrowse) {
		sp_artistbrowse_add_ref(artistbrowse);
	}

	static void Release(sp_artistbrowse * artistbrowse) {
		sp_artistbrowse_release(artistbrowse);
	}

	static bool IsLoaded(sp_artistbrowse * artistbrowse) {
		return sp_artistbrowse_is_loaded(artistbrowse);
	}
};

template <>
struct SpotifyTraits<sp_image>
{
	static void AddRef(sp_image * image) {
		sp_image_add_ref(image);
	}

	static void Release(sp_image * image) {
		sp_image_release(image);
	}

	static bool IsLoaded(sp_image * image) {
		return sp_image_is_loaded(image);
	}
};

template <>
struct SpotifyTraits<sp_link>
{
	static void AddRef(sp_link * link) {
		sp_link_add_ref(link);
	}

	static void Release(sp_link * link) {
		sp_link_release(link);
	}
};

template <>
struct SpotifyTraits<sp_playlist>
{
	static void AddRef(sp_playlist * playlist) {
		sp_playlist_add_ref(playlist);
	}

	static void Release(sp_playlist * playlist) {
		sp_playlist_release(playlist);
	}

	static bool IsLoaded(sp_playlist * playlist) {
		return sp_playlist_is_loaded(playlist);
	}
};

template <>
struct SpotifyTraits<sp_track>
{
	static void AddRef(sp_track * track) {
		sp_track_add_ref(track);
	}

	static void Release(sp_track * track) {
		sp_track_release(track);
	}

	static bool IsLoaded(sp_track * track) {
		return sp_track_is_loaded(track);
	}
};

template <>
struct SpotifyTraits<sp_search>
{
	static void AddRef(sp_search * search) {
		sp_search_add_ref(search);
	}

	static void Release(sp_search * search) {
		sp_search_release(search);
	}

	static bool IsLoaded(sp_search * search) {
		return sp_search_is_loaded(search);
	}
};

template <typename T>
void SpotifyAddRef(T * ptr)
{
	SpotifyLockScope lock;
	SpotifyTraits<T>::AddRef(ptr);
}

template <typename T>
void SpotifyRelease(T * ptr)
{
	SpotifyLockScope lock;
	SpotifyTraits<T>::Release(ptr);
}

template <typename T>
void SpotifyAwaitLoaded(T * ptr, LockedCS &lock, abort_callback &abort)
{
	while (!SpotifyTraits<T>::IsLoaded(ptr))
	{
		lock.wait(abort, 100);
	}
}

template <typename T>
class SpotifyPtr
{
public:
	typedef T * PtrType;

	PtrType m_ptr;

	SpotifyPtr()
	{
		m_ptr = nullptr;
	}

	SpotifyPtr(PtrType ptr)
	{
		m_ptr = ptr;
		if (m_ptr != nullptr)
		{
			SpotifyAddRef(m_ptr);
		}
	}

	SpotifyPtr(const SpotifyPtr<PtrType> & other)
	{
		m_ptr = other.m_ptr;
		if (m_ptr != nullptr)
		{
			SpotifyAddRef(m_ptr);
		}
	}

	SpotifyPtr(SpotifyPtr<PtrType> && other)
	{
		m_ptr = other.m_ptr;
		other.m_ptr = nullptr;
	}

	void Release()
	{
		PtrType ptr = m_ptr;
		m_ptr = nullptr;
		if (ptr != null)
		{
			SpotifyRelease(ptr);
		}
	}

	void Attach(PtrType ptr)
	{
		if (m_ptr != nullptr)
		{
			SpotifyRelease(ptr);
		}
		m_ptr = ptr;
	}

	operator bool() const
	{
		return m_ptr != nullptr;
	}

	bool operator !() const
	{
		return m_ptr == nullptr;
	}

	PtrType operator =(PtrType ptr)
	{
		Release();
		m_ptr = ptr;
		if (m_ptr != nullptr)
		{
			SpotifyAddRef(m_ptr);
		}
		return m_ptr;
	}

	operator PtrType() const
	{
		return m_ptr;
	}
};
