#pragma once
#include <array>
#include <cstring>
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

// These leaf accessors are read, not invoked, on an unlisted client. Their
// function addresses may move when the DLL is relinked.
inline constexpr std::array<unsigned char,27> kEntityAtIndexCode{
    0x55,0x8b,0xec,0x8b,0x45,0x08,0x3d,0xff,0x07,0,0,0x77,0x08,
    0x8b,0x44,0xc1,0x28,0x5d,0xc2,0x04,0,0x33,0xc0,0x5d,0xc2,0x04,0
};
inline constexpr std::array<unsigned char,4> kHighestEntityCode{0x8b,0x41,0x24,0xc3};
inline constexpr std::array<unsigned char,4> kDataTableBaseCode{0x8d,0x41,0xf8,0xc3};

// Native TeamDisplayPlayer update reads the same player index twice. It
// bounds it to 32, then passes it to the resource connection and team queries.
// Wildcards are the two jump displacements and independent scratch field.
inline constexpr int kTeammateLookupCode[]{
    0x8b,0x86,-1,-1,-1,-1,0xc6,0x86,-1,-1,-1,-1,0x00,0x83,0xf8,0x20,0x0f,0x8f,-1,-1,-1,-1,
    0x8b,0x93,0x38,0x06,0,0,0x81,0xc3,0x38,0x06,0,0,0x50,0x8b,0x42,0x10,0x8b,0xcb,0xff,0xd0,
    0x84,0xc0,0x0f,0x84,-1,-1,-1,-1,0x8b,0x13,0x8b,0x86,-1,-1,-1,-1,
    0x8b,0x52,0x34,0x50,0x8b,0xcb,0xff,0xd2
};

inline int FindNativeTeammateOffset(const unsigned char* code,std::size_t size) {
    int result=-1;
    constexpr auto length=sizeof(kTeammateLookupCode)/sizeof(*kTeammateLookupCode);
    for (std::size_t at=0;size>=length && at<=size-length;++at) {
        const auto* entry=code+at;
        if (entry[0]!=0x8b || entry[1]!=0x86 || entry[13]!=0x83) continue;
        bool matches=true;
        for (std::size_t i=2;i<length;++i) {
            if (kTeammateLookupCode[i]>=0 && entry[i]!=kTeammateLookupCode[i]) {
                matches=false;break;
            }
        }
        if (!matches) continue;
        int first=0,second=0;
        std::memcpy(&first,entry+2,sizeof(first));
        std::memcpy(&second,entry+54,sizeof(second));
        if (first!=second || first<0x100 || first>=0x1000 || first%4) continue;
        if (result>=0) return -1;
        result=first;
    }
    return result;
}
} // namespace neh
