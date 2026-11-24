#pragma once
#include <string_view>

namespace neh {
struct ClientHudBuild {
    std::string_view sha256;
    int healthPanelVtable;
    int iconPanelVtable;
};

inline constexpr ClientHudBuild kPreviousClientBuild{
    "c0a8e1e88f7312db11bcfd4b0ec574d224a34b2d9a615d6088b28335eeb4af1f",
    0x5c8cbc, 0x590654
};

inline constexpr ClientHudBuild kSteam26770911ClientBuild{
    "4d01ed9fb856d57ce549b8587c386f361083ee581b001295fac0eff4c835d52d",
    0x5c8cc4, 0x59065c
};

constexpr const ClientHudBuild* FindClientHudBuild(std::string_view sha256) {
    if (sha256 == kSteam26770911ClientBuild.sha256) return &kSteam26770911ClientBuild;
    if (sha256 == kPreviousClientBuild.sha256) return &kPreviousClientBuild;
    return nullptr;
}
} // namespace neh
