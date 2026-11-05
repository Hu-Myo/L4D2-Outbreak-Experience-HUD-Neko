#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace neh {
constexpr int kMaxPlayers = 32;
struct Rect { int x=0, y=0, w=0, h=0; };
struct Color { int r=255, g=255, b=255, a=255; };
enum class HudTheme:int { Current=0, GameHud=1 };
struct Settings {
    bool enabled = true;
    bool overflow = false;
    bool debug = false;
    float scale = 0.85f;
    int gap = 6;
    int rowGap = 8;
    int offsetX = 0;
    int offsetY = 0;
    HudTheme theme = HudTheme::Current;
    bool singlePlayerTest = false;
    bool replaceL4d1Team = true;
};
struct ModelPortrait {
    // Nonzero only for a currently applied, validated Neko model. Changes on
    // model/map/feature lifetime transitions; contains copied values only.
    std::uint64_t revision=0;
    std::uint32_t modelId=0;
    int normalId=0,incapId=0;
    std::string normalMaterial,incapMaterial;
};
struct Player {
    int index=0;
    std::string name;
    // Canonical portrait/policy ID: Nick, Rochelle, Coach, Ellis, Bill, Zoey,
    // Francis, Louis. Unknown survivor-set reads never imply the L4D2 set.
    int character=-1;
    bool alive=false, incapacitated=false, hanging=false, blackWhite=false;
    // health is the displayed total. temporaryHealth is the temporary
    // portion already included in that total, never an additional amount.
    int health=0, maxHealth=100;
    float temporaryHealth=0;
    int throwable=0, medical=0, pills=0;
    ModelPortrait modelPortrait;
    int rawCharacter=-1;
};
struct HudSlot {
    std::uint32_t panel=0;
    int playerIndex=0;
    Rect bounds;
    bool visible=false;
    Rect visibleBounds;
    bool hasVisibleBounds=false;
    bool usesVisualBounds=false;
};
struct Snapshot {
    std::vector<Player> players;
    int localIndex=0;
    int viewIndex=0;
    bool inGame=false;
    Rect selfHudBounds;
    bool hasSelfHudBounds=false;
    int survivorSet=0; // 1=L4D1, 2=L4D2, 0=unavailable or not yet initialized.
};
struct Placement {
    int playerIndex=0;
    int row=0;
    int ordinal=0;
    Rect bounds;
};
struct Layout {
    std::vector<Placement> cards;
    int hiddenOverflow=0;
    int nativeCount=0;
    bool suppressed=false;
    bool replacesNative=false;
    std::string reason;
    std::string detail;
};
Layout PlanLayout(const Snapshot&, const std::vector<HudSlot>&, const Settings&, int screenWidth, int screenHeight,
                  bool replaceNative=false);

struct NativeHudPaintState {
    bool enabled=true;
    std::uint32_t rootHandle=0xffffffffu;
};

// All engine calls are made on the game's UI thread. No game-owned pointers are
// retained inside Snapshot or Layout.
struct GameHudTheme;
struct ThemeIcon;
using ItemIconLookup=const ThemeIcon* (*)(int);
class Runtime {
public:
    bool Initialize(std::string& error);
    bool Ready() const;
    void* PanelInterface() const;
    void* SurfaceInterface() const;
    std::uint32_t RootPanel() const;
    bool IsTeamRoot(std::uint32_t panel) const;
    std::vector<HudSlot> ReadNativeSlots(std::uint32_t teamRoot, std::string& error);
    Snapshot ReadSnapshot(std::string& error);
    const GameHudTheme& ReadGameHudTheme(const Snapshot&,const std::vector<HudSlot>&,
                                       int screenWidth,int screenHeight,std::string& error);
    static const ThemeIcon* ReadGameHudItemIcon(int item);
    void ScreenSize(int& width, int& height) const;
    bool TeamHudVisible(std::uint32_t panel) const;
    bool ReadNativeHudPaintState(std::uint32_t panel,NativeHudPaintState& state) const;
    std::string Diagnostics() const;
};

class Renderer {
public:
    void Initialize(void* surface,void* materials=nullptr);
    bool Draw(const Snapshot&, const Layout&, const Settings&,const GameHudTheme* theme=nullptr,
              ItemIconLookup lookupItem=nullptr);
private:
    void* surface_=nullptr;
    unsigned long font_=0, smallFont_=0;
    int fontHeight_=0;
};
} // namespace neh
