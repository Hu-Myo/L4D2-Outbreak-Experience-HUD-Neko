#pragma once
#include "types.hpp"
#include <array>

namespace neh {
// These contain copied values and public rendering handles, never Panel* or
// IImage*. Rectangles are relative to the current native card's visible bounds.
struct ThemeTexture {
    int id=0;
    float u0=0,v0=0,u1=1,v1=1;
    Color tint{255,255,255,255};
    std::string material;
};
struct ThemeWidget {
    bool present=false;
    Rect rect;
    Color color{255,255,255,255};
    unsigned long font=0;
    int z=0,order=0;
    int alignment=0,insetX=0,insetY=0;
};
struct ThemeImage {
    ThemeWidget widget;
    ThemeTexture texture;
    Color fill{0,0,0,0};
    bool characterIncap=false;
};
struct ThemeIcon {
    int kind=0,item=0;
    ThemeTexture texture;
    bool usingFont=false,scale=true;
    unsigned long font=0;
    wchar_t glyph=0;
    Color color{255,255,255,255};
    int width=0,height=0;
};
struct ThemeHealthBar {
    bool present=false,newMaterial=false,monochrome=false;
    // critical, low, medium, high, grey; all IDs come from the loaded control.
    std::array<ThemeTexture,5> fill,ticks;
    ThemeTexture white,grey,outline;
    // Loaded HealthPanel palette: critical/low/medium/high.
    std::array<Color,4> palette={Color{161,25,25,255},Color{161,25,25,255},
                               Color{216,146,12,255},Color{10,177,50,255}};
    Color monochromeColor{255,255,255,255};
    float lowThreshold=0.25f,mediumThreshold=0.4f;
    int inset=0;
};
struct GameHudCardStyle {
    bool ready=false;
    int width=0,height=0;
    ThemeWidget portrait,name,number,status,health,healthIcon;
    std::wstring healthGlyph;
    std::array<ThemeWidget,3> items;
    std::vector<ThemeImage> images;
    ThemeHealthBar bar;
};
struct GameHudTheme {
    bool available=false;
    // normal, incapacitated, hanging, dead
    std::array<GameHudCardStyle,4> states;
    // Value-only layouts prepared at map entry, independent of roster state.
    GameHudCardStyle preparedIncap,preparedDead;
    // Normal-state numeric colors observed in the loaded HUD: low/medium/high/BW.
    std::array<Color,4> numberPalette={Color{235,56,48,255},Color{235,196,39,255},
                                     Color{135,231,44,255},Color{225,225,225,255}};
    std::array<bool,4> numberPaletteObserved{};
    std::vector<ThemeIcon> icons;
    std::string status="waiting for the current game HUD";
};
inline std::size_t ThemeStateIndex(const Player& player) {
    return !player.alive?3u:player.hanging?2u:player.incapacitated?1u:0u;
}
} // namespace neh
