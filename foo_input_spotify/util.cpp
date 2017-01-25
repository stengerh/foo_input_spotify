#include "pch.h"
#include "util.h"

void LockedCS::wait(abort_callback &abort, DWORD timeoutMillis) {
	UnlockedCS unlocked(*this);

	SetLastError(ERROR_SUCCESS);
	DWORD result = WaitForSingleObject(abort.get_handle(), timeoutMillis);
	switch (result) {
	case WAIT_TIMEOUT:
		break;
	case WAIT_OBJECT_0:
		throw exception_aborted();
	case WAIT_ABANDONED_0:
		throw win32exception("WAIT_ABANDONED (abort handle)");
	case WAIT_FAILED:
		throw win32exception("WAIT_FAILED");
	default:
		throw win32exception("unexpected wait result");
	}
}

bool LockedCS::waitForEvent(Event &ev, abort_callback &abort, DWORD timeoutMillis) {
	UnlockedCS unlocked(*this);

	HANDLE handles[2] = { ev.handle, abort.get_handle() };
	SetLastError(ERROR_SUCCESS);
	DWORD result = WaitForMultipleObjects(2, handles, FALSE, timeoutMillis);
	switch (result) {
	case WAIT_TIMEOUT:
		return false;
	case WAIT_OBJECT_0:
		return true;
	case WAIT_OBJECT_0 + 1:
		throw exception_aborted();
	case WAIT_ABANDONED_0:
		throw win32exception("WAIT_ABANDONED (event handle)");
	case WAIT_ABANDONED_0 + 1:
		throw win32exception("WAIT_ABANDONED (abort handle)");
	case WAIT_FAILED:
		throw win32exception("WAIT_FAILED");
	default:
		throw win32exception("unexpected wait result");
	}
}

Buffer::Buffer() : entries(0), ptr(0) {
	InitializeConditionVariable(&bufferNotEmpty);
}

Buffer::~Buffer() {
	flush();
}

void Buffer::add(void *data, size_t size, int sampleRate, int channels) {
	Gentry *e = new Gentry;
	e->data = data;
	e->size = size;
	e->sampleRate = sampleRate;
	e->channels = channels;

	{
		LockedCS lock(bufferLock);

		// Yes, this is spinlock.  See the class comment.
		while (entries >= MAX_ENTRIES)
			lock.dropAndReacquire();

		entry[(ptr + entries) % MAX_ENTRIES] = e;
		++entries;
	}
	WakeConditionVariable(&bufferNotEmpty);
}

bool Buffer::isFull() {
	return entries >= MAX_ENTRIES - SPACE_FOR_UTILITY_MESSAGES;
}

void Buffer::flush() {
	while (entries > 0)
		free(take(NULL));
}

Gentry *Buffer::take(abort_callback *p_abort) {
	LockedCS lock(bufferLock);
	while (entries == 0) {
		SleepConditionVariableCS(&bufferNotEmpty, &bufferLock.cs, 200);
		if (p_abort)
			p_abort->check();
	}

	Gentry *e = entry[ptr++];
	--entries;
	if (MAX_ENTRIES == ptr)
		ptr = 0;

	return e;
}

void Buffer::free(Gentry *e) {
	delete[] e->data;
	delete e;
}
