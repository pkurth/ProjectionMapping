#pragma once

#include "core/threading.h"
#include "profiling_internal.h"

extern bool cpuProfilerWindowOpen;


// Things the CPU profiler currently doesn't support (but probably should):
// - Thread names
// - Display of multiple threads
// - Filtering?


#if ENABLE_CPU_PROFILING

#define CPU_PROFILE_BLOCK_(counter, name) cpu_profile_block_recorder COMPOSITE_VARNAME(__PROFILE_BLOCK, counter)(name)
#define CPU_PROFILE_BLOCK(name) CPU_PROFILE_BLOCK_(__COUNTER__, name)

#define MAX_NUM_CPU_PROFILE_BLOCKS 16384
#define MAX_NUM_CPU_PROFILE_EVENTS (MAX_NUM_CPU_PROFILE_BLOCKS * 2) // One for start and end.


#define recordProfileEvent(type_, name_) \
	extern profile_event cpuProfileEvents[2][MAX_NUM_CPU_PROFILE_EVENTS]; \
	extern std::atomic<uint32> cpuProfileArrayAndEventIndex; \
	extern std::atomic<uint32> cpuProfileEventsCompletelyWritten[2]; \
	uint32 arrayAndEventIndex = cpuProfileArrayAndEventIndex++; \
	uint32 eventIndex = arrayAndEventIndex & ((1u << 31) - 1); \
	uint32 arrayIndex = arrayAndEventIndex >> 31; \
	assert(eventIndex < MAX_NUM_CPU_PROFILE_EVENTS); \
	profile_event* e = cpuProfileEvents[arrayIndex] + eventIndex; \
	e->threadID = getThreadIDFast(); \
	e->name = name_; \
	e->type = type_; \
	QueryPerformanceCounter((LARGE_INTEGER*)&e->timestamp); \
	cpuProfileEventsCompletelyWritten[arrayIndex].fetch_add(1, std::memory_order_release); // Mark this event as written. Release means that the compiler may not reorder the previous writes after this.


struct cpu_profile_block_recorder
{
	const char* name;

	cpu_profile_block_recorder(const char* name)
		: name(name)
	{
		recordProfileEvent(profile_event_begin_block, name);
	}

	~cpu_profile_block_recorder()
	{
		recordProfileEvent(profile_event_end_block, name);
	}
};

inline void cpuProfilingFrameEndMarker()
{
	recordProfileEvent(profile_event_frame_marker, 0);
}

// Currently there must not be any profile events between calling cpuProfilingFrameEndMarker and cpuProfilingResolveTimeStamps.

void cpuProfilingResolveTimeStamps();

#undef recordProfileEvent

#else

#define CPU_PROFILE_BLOCK(...)

#define cpuProfilingFrameEndMarker(...)
#define cpuProfilingResolveTimeStamps(...)

#endif

