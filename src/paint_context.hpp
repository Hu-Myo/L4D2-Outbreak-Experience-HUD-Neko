#pragma once
#include <windows.h>
#include <cstdint>
#include "draw_trace.hpp"

namespace neh {
// Keep this wrapper free of destructible C++ locals: __finally must pop the
// engine surface even if a guarded native call raises a structured exception.
inline void WithPaintContext(void* surface,std::uint32_t root,
                             void (*draw)(void*),void* data) {
    using Push=void(__thiscall*)(void*,std::uint32_t,bool);
    using Pop=void(__thiscall*)(void*,std::uint32_t);
    auto table=*reinterpret_cast<void***>(surface);
    bool pushed=false;
    __try {
        TraceSurface(surface,8,"push_context");
        reinterpret_cast<Push>(table[8])(surface,root,false);
        pushed=true;
        draw(data);
    } __finally {
        if(pushed) {
            TraceSurface(surface,9,"pop_context");
            reinterpret_cast<Pop>(table[9])(surface,root);
        }
    }
}
} // namespace neh
