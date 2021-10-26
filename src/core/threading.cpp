#include "pch.h"
#include "threading.h"
#include "math.h"
#include <intrin.h>


template <typename T, uint32 capacity>
struct thread_safe_ring_buffer
{
	bool push_back(const T& t)
	{
		bool result = false;
		mutex.lock();
		uint32 next = (nextItemToWrite + 1) % capacity;
		if (next != nextItemToRead)
		{
			data[nextItemToWrite] = t;
			nextItemToWrite = next;
			result = true;
		}
		mutex.unlock();
		return result;
	}

	bool pop_front(T& t)
	{
		bool result = false;
		mutex.lock();
		if (nextItemToRead != nextItemToWrite)
		{
			t = data[nextItemToRead];
			nextItemToRead = (nextItemToRead + 1) % capacity;
			result = true;
		}
		mutex.unlock();
		return result;
	}

	uint32 nextItemToRead = 0;
	uint32 nextItemToWrite = 0;
	T data[capacity];

	std::mutex mutex;
};


struct work_queue_entry
{
	std::function<void()> callback;
	thread_job_context* context;
};

static thread_safe_ring_buffer< work_queue_entry, 256> queue;
static HANDLE semaphoreHandle;

static bool performWork()
{
	work_queue_entry entry;
	if (queue.pop_front(entry))
	{
		entry.callback();
		atomicDecrement(entry.context->numJobs);

		return true;
	}

	return false;
}

static void workerThreadProc()
{
	while (true)
	{
		if (!performWork())
		{
			WaitForSingleObjectEx(semaphoreHandle, INFINITE, FALSE);
		}
	}
}

void initializeJobSystem()
{
	HANDLE handle = GetCurrentThread();
	SetThreadAffinityMask(handle, 1);
	SetThreadPriority(handle, THREAD_PRIORITY_HIGHEST);
	CloseHandle(handle);


	uint32 numThreads = std::thread::hardware_concurrency();
	numThreads = clamp(numThreads, 1u, 8u);
	semaphoreHandle = CreateSemaphoreEx(0, 0, numThreads, 0, 0, SEMAPHORE_ALL_ACCESS);

	for (uint32 i = 0; i < numThreads; ++i)
	{
		std::thread thread(workerThreadProc);

		HANDLE handle = (HANDLE)thread.native_handle();

		uint64 affinityMask = 1ull << (i + 1); // 1 is the main thread.
		SetThreadAffinityMask(handle, affinityMask);

		//SetThreadPriority(handle, THREAD_PRIORITY_HIGHEST);
		SetThreadDescription(handle, L"Worker thread");

		thread.detach();
	}
}

void thread_job_context::addWork(const std::function<void()>& cb)
{
	work_queue_entry entry;
	entry.callback = cb;
	entry.context = this;
	atomicIncrement(numJobs);

	while (!queue.push_back(entry))
	{
		performWork();
	}

	ReleaseSemaphore(semaphoreHandle, 1, 0);
}

void thread_job_context::waitForWorkCompletion()
{
	while (numJobs)
	{
		if (!performWork())
		{
			while (numJobs) {}
			break;
		}
	}
}
