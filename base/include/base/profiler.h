#pragma once

// Thin wrapper over Tracy. With PAPER_PROFILER undefined every macro compiles
// to nothing, so call sites cost nothing in normal builds.
#ifdef PAPER_PROFILER
#include <tracy/Tracy.hpp>
// Names must be string literals. Several zones may share a scope.
#define PAPER_ZONE_CAT2(a, b) a##b
#define PAPER_ZONE_CAT(a, b) PAPER_ZONE_CAT2(a, b)
#define PAPER_ZONE(name) ZoneNamedN(PAPER_ZONE_CAT(paperZone, __LINE__), name, true)
#define PAPER_FRAME_MARK() FrameMark
#else
#define PAPER_ZONE(name) ((void)0)
#define PAPER_FRAME_MARK() ((void)0)
#endif
