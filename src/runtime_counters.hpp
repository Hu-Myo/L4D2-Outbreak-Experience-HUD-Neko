#pragma once
#include <cstdint>

namespace neh {
void AbortMemoryQueries();
struct RuntimeCounters {
    std::uint64_t virtualQueries=0;
    std::uint64_t traversedNodes=0;
    std::uint64_t nativeDiscoveries=0;
    std::uint64_t visualDiscoveries=0;
    std::uint64_t selfDiscoveries=0;
    std::uint64_t expiredPanelRefs=0;
    std::uint64_t nativeHealthReads=0,resourceHealthReads=0,temporaryHealthSamples=0;
    std::uint64_t themeReads=0,themeCaptures=0,themeFallbackCards=0;
    std::uint64_t themeTextureQueries=0,themeTextureMisses=0;
    std::uint64_t themeAdaptedStateCards=0,themeItemFallbacks=0;
    std::uint64_t modelPortraitInspections=0,modelPortraitLookups=0,modelPortraitCacheHits=0;
    std::uint64_t modelPortraitLoadAttempts=0,modelPortraitDraws=0;
};
inline thread_local RuntimeCounters g_runtimeCounters;
inline RuntimeCounters TakeRuntimeCounters() {
    const auto result=g_runtimeCounters;
    g_runtimeCounters={};
    return result;
}
} // namespace neh
