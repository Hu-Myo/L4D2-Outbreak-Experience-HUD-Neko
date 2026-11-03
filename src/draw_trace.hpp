#pragma once
#include <cstdint>

namespace neh {
struct DrawTrace {
    const char* stage="idle";
    int surfaceSlot=-1;
    std::uintptr_t function=0;
    int playerIndex=0;
    std::uint32_t runtimePanel=0;
    int runtimeSlot=-1;
    std::uintptr_t runtimeFunction=0;
};
inline thread_local DrawTrace g_drawTrace;
inline void TraceRuntimeMethod(int slot,void* function) {
    g_drawTrace.runtimeSlot=slot;
    g_drawTrace.runtimeFunction=reinterpret_cast<std::uintptr_t>(function);
}
inline void TraceRuntimePanel(std::uint32_t panel,int slot) {
    g_drawTrace.stage="runtime.panel_query";
    g_drawTrace.runtimePanel=panel;
    g_drawTrace.runtimeSlot=slot;
}
inline void TraceSurface(void* surface,int slot,const char* stage) {
    g_drawTrace.stage=stage;
    g_drawTrace.surfaceSlot=slot;
    g_drawTrace.function=reinterpret_cast<std::uintptr_t>((*reinterpret_cast<void***>(surface))[slot]);
}
} // namespace neh
