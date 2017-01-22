#pragma once

#define MYVERSION "0.0.4"

#define _WIN32_WINNT 0x0600

#include <windows.h>
#include "boost/noncopyable.hpp"
#include <string>
#include <sstream>
#include <foobar2000.h>

struct win32exception : std::exception {
	std::string makeMsg(const std::string &cause, DWORD err) {
		std::stringstream ss;
		ss << cause << ", win32: " << err << " (" << std::hex << err << "): " << format_win32_error(err);
		return ss.str();
	}

	win32exception(std::string cause) : std::exception(makeMsg(cause, GetLastError()).c_str()) {
	}

	win32exception(std::string cause, DWORD err) : std::exception(makeMsg(cause, err).c_str()) {
	}
};

struct Gentry {
	void *data;
    size_t size;
	int sampleRate;
	int channels;
};

struct Event : boost::noncopyable {
	HANDLE handle;

	Event(BOOL manualReset, BOOL initialState) : handle(NULL) {
		handle = CreateEvent(NULL, manualReset, initialState, NULL);
		if (handle == NULL)
			throw win32exception("failed to create event");
	}

	~Event() {
		if (handle != NULL)
			CloseHandle(handle);
	}

	HANDLE duplicateHandle() const {
		SetLastError(ERROR_SUCCESS);
		HANDLE h = NULL;
		BOOL result = DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &h, 0, FALSE, DUPLICATE_SAME_ACCESS);
		if (!result)
			throw win32exception("could not copy event");
		return h;
	}
};

struct CriticalSection : boost::noncopyable {
	CRITICAL_SECTION cs;
	CriticalSection() {
		InitializeCriticalSection(&cs);
	}

	~CriticalSection() {
		DeleteCriticalSection(&cs);
	}
};

struct LockedCS : boost::noncopyable {
	CRITICAL_SECTION &cs;
	LockedCS(CriticalSection &o) : cs(o.cs) {
		EnterCriticalSection(&cs);
	}

	~LockedCS() {
		LeaveCriticalSection(&cs);
	}

	void dropAndReacquire(DWORD wait = 0) {
		LeaveCriticalSection(&cs);
		Sleep(wait);
		EnterCriticalSection(&cs);
	}

	void wait(abort_callback &abort, DWORD timeoutMillis);
	bool waitForEvent(Event &ev, abort_callback &abort, DWORD timeoutMillis = INFINITE);
};

struct UnlockedCS : boost::noncopyable {
	CRITICAL_SECTION &cs;

	UnlockedCS(const LockedCS &lock) : cs(lock.cs) {
		LeaveCriticalSection(&cs);
	}

	~UnlockedCS() {
		EnterCriticalSection(&cs);
	}
};

/** A fixed sized, thread-safe queue with blocking take(), but without an efficient blocking add implementation.
 * Expected use: Main producer, secondary notification producers, single consumer. 
 * Main producer is expected to back-off when queue is full.
 *
 * In hindsight, a dual-lock queue would've been simpler.  Originally there wern't multiple producers..
 */
struct Buffer : boost::noncopyable {

	size_t entries;
	size_t ptr;

	static const size_t MAX_ENTRIES = 255;
	static const size_t SPACE_FOR_UTILITY_MESSAGES = 5;

	Gentry *entry[MAX_ENTRIES];

	CONDITION_VARIABLE bufferNotEmpty;
	CriticalSection bufferLock;

	Buffer();
	~Buffer();
	void add(void *data, size_t size, int sampleRate, int channels);
	bool isFull();
	void flush();
	Gentry *take(abort_callback *p_abort);
	void free(Gentry *e);
};
