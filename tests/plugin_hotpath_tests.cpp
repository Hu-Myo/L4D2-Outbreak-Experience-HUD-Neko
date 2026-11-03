#include "../src/plugin.cpp"
#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {
neh::Snapshot Roster(unsigned count=4) {
    neh::Snapshot snapshot;snapshot.inGame=true;snapshot.localIndex=snapshot.viewIndex=1;
    for(unsigned n=1;n<=count;++n) {
        neh::Player player;player.index=static_cast<int>(n);player.name="Player "+std::to_string(n);
        player.alive=true;player.character=static_cast<int>((n-1)%8);snapshot.players.push_back(player);
    }
    return snapshot;
}
struct MockRuntimeState {
    bool ready=true,visible=true;
    std::uint32_t root=10;
    unsigned calls=0,classifications=0,visibilityChecks=0,snapshots=0,themeReads=0,nativeReads=0;
    bool layoutHasCards=false;
    bool nativeStateValid=false,nativeEnabled=true,secondStateValid=true;
    bool changeHandle=false,changeEnabled=false,unsupportedOnSecond=false;
    bool surfaceReady=true,paintRootReady=true,layoutSuppressed=false,allowReplacementLayout=true;
    bool snapshotFault=false,themeFault=false,renderFault=false,pushFault=false,popFault=false,disableDuringSnapshot=false;
    std::uint32_t rootHandle=100;
    unsigned stateReads=0,stateReadsThisFrame=0,draws=0,pushes=0,pops=0;
    bool renderedReplacement=false,renderSubmitted=true;
    neh::Snapshot snapshot=Roster();
    std::vector<bool> plannedModes;
    std::vector<std::string> events;
} g_mock;
struct MockSurface {void** table=nullptr;};
std::array<void*,10> g_surfaceTable{};
MockSurface g_surface;
unsigned g_originalCalls=0,g_checks=0;
void Check(bool condition,const char* message) {
    if(!condition){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}
    ++g_checks;
}
void __fastcall Original(void*,void*,std::uint32_t,bool,bool){++g_originalCalls;g_mock.events.push_back("original");}
void __fastcall Push(void*,void*,std::uint32_t root,bool insets) {
    ++g_mock.calls;++g_mock.pushes;g_mock.events.push_back("push");
    if(root!=1||insets)RaiseException(0xE0010020,0,0,nullptr);
    if(g_mock.pushFault)RaiseException(0xE0010021,0,0,nullptr);
}
void __fastcall Pop(void*,void*,std::uint32_t root) {
    ++g_mock.calls;++g_mock.pops;g_mock.events.push_back("pop");
    if(root!=1||g_mock.popFault)RaiseException(0xE0010022,0,0,nullptr);
}
void ResetFixture() {
    g_mock={};g_originalCalls=0;g_stop.store(false);
    g_nativeSkipSupported.store(false);g_replacementPolicy.Reset();g_policyUsed=false;
    g_original=reinterpret_cast<PaintFn>(&Original);
    g_teamRootCache={};g_perf={};neh::TakeRuntimeCounters();
    g_surfaceTable.fill(nullptr);g_surfaceTable[8]=reinterpret_cast<void*>(&Push);
    g_surfaceTable[9]=reinterpret_cast<void*>(&Pop);g_surface.table=g_surfaceTable.data();
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    const std::wstring path(executable);g_log=path.substr(0,path.find_last_of(L"\\/"))+L"\\plugin_hotpath_test.log";
    neh::Settings settings;settings.enabled=false;settings.debug=false;PublishSettings(settings);
}
void Paint(std::uint32_t panel){g_mock.stateReadsThisFrame=0;PaintHook(nullptr,nullptr,panel,false,true);}
void EnableReplacementFixture(unsigned count=9) {
    ResetFixture();g_mock.snapshot=Roster(count);g_mock.layoutHasCards=true;g_mock.nativeStateValid=true;
    g_nativeSkipSupported.store(true);
    neh::Settings settings;settings.enabled=true;settings.debug=false;PublishSettings(settings);
}
}

// These replace every engine-facing Runtime/Renderer entry point. No game
// module, SDK lifecycle callback, UI hook installation, menu, or file setting
// loader is invoked by this test executable.
namespace neh {
void AbortMemoryQueries(){}
bool Runtime::Initialize(std::string&){++g_mock.calls;return g_mock.ready;}
bool Runtime::Ready()const{++g_mock.calls;return g_mock.ready;}
void* Runtime::PanelInterface()const{++g_mock.calls;return nullptr;}
void* Runtime::SurfaceInterface()const{++g_mock.calls;return g_mock.surfaceReady?&g_surface:nullptr;}
std::uint32_t Runtime::RootPanel()const{++g_mock.calls;return g_mock.paintRootReady?1u:0u;}
bool Runtime::IsTeamRoot(std::uint32_t panel)const {
    ++g_mock.calls;++g_mock.classifications;return panel==g_mock.root;
}
bool Runtime::TeamHudVisible(std::uint32_t panel)const {
    ++g_mock.calls;++g_mock.visibilityChecks;return g_mock.visible&&panel==g_mock.root;
}
std::vector<HudSlot> Runtime::ReadNativeSlots(std::uint32_t,std::string& error) {
    ++g_mock.calls;++g_mock.nativeReads;g_mock.events.push_back("native");error.clear();return {};
}
Snapshot Runtime::ReadSnapshot(std::string& error) {
    ++g_mock.calls;++g_mock.snapshots;g_mock.events.push_back("snapshot");error.clear();
    if(g_mock.snapshotFault)RaiseException(0xE0010023,0,0,nullptr);
    if(g_mock.disableDuringSnapshot) {auto settings=CurrentSettings();settings.enabled=false;PublishSettings(settings);}
    return g_mock.snapshot;
}
bool Runtime::ReadNativeHudPaintState(std::uint32_t panel,NativeHudPaintState& state)const {
    ++g_mock.calls;++g_mock.stateReads;++g_mock.stateReadsThisFrame;g_mock.events.push_back("paint_state");
    const bool second=g_mock.stateReadsThisFrame>1;
    state.enabled=second&&g_mock.changeEnabled?!g_mock.nativeEnabled:g_mock.nativeEnabled;
    state.rootHandle=g_mock.rootHandle+(second&&g_mock.changeHandle?1u:0u);
    if(second&&g_mock.unsupportedOnSecond)g_nativeSkipSupported.store(false);
    return panel==g_mock.root&&g_mock.nativeStateValid&&(!second||g_mock.secondStateValid);
}
const GameHudTheme& Runtime::ReadGameHudTheme(const Snapshot&,const std::vector<HudSlot>&,int,int,std::string& error) {
    ++g_mock.calls;++g_mock.themeReads;g_mock.events.push_back("theme");error.clear();
    if(g_mock.themeFault)RaiseException(0xE0010024,0,0,nullptr);
    static const GameHudTheme theme;return theme;
}
void Runtime::ScreenSize(int& width,int& height)const{++g_mock.calls;width=1920;height=1080;}
std::string Runtime::Diagnostics()const{++g_mock.calls;return "fixture";}
void Renderer::Initialize(void*,void*){}
bool Renderer::Draw(const Snapshot&,const Layout& layout,const Settings&,const GameHudTheme*) {
    ++g_mock.calls;++g_mock.draws;g_mock.events.push_back("draw");g_mock.renderedReplacement=layout.replacesNative;
    if(g_mock.renderFault)RaiseException(0xE0010025,0,0,nullptr);
    return g_mock.renderSubmitted;
}
Layout PlanLayout(const Snapshot& snapshot,const std::vector<HudSlot>&,const Settings&,int,int,bool replacement) {
    ++g_mock.calls;g_mock.plannedModes.push_back(replacement);g_mock.events.push_back("layout");
    Layout layout;layout.reason="fixture";layout.suppressed=g_mock.layoutSuppressed||!snapshot.inGame;
    layout.replacesNative=replacement&&g_mock.allowReplacementLayout;
    if(g_mock.layoutHasCards&&(!replacement||g_mock.allowReplacementLayout)&&snapshot.inGame)
        layout.cards.push_back({2,0,5,{700,900,249,111}});
    return layout;
}
}

namespace {
void CheckAutomaticReplacementHook() {
    EnableReplacementFixture(4);Paint(g_mock.root);
    Check(g_originalCalls==1&&g_mock.draws==1&&!g_mock.renderedReplacement&&
          g_mock.plannedModes==std::vector<bool>{false},
          "a healthy unique roster preserves native painting and renders only its normal additions");
    Check(g_mock.events==std::vector<std::string>{"native","snapshot","paint_state","layout","original","push","draw","pop"},
          "normal root preparation runs once before original painting and rendering uses a balanced context afterward");

    EnableReplacementFixture(8);
    for(auto& player:g_mock.snapshot.players)player.name="Same nickname";
    Paint(g_mock.root);
    Check(g_originalCalls==1&&!g_mock.renderedReplacement&&g_mock.plannedModes==std::vector<bool>{false},
          "duplicate player nicknames do not select automatic replacement for eight unique survivor characters");
    for(auto& player:g_mock.snapshot.players) {
        player.name="Player "+std::to_string(player.index);player.modelPortrait.revision=1;player.modelPortrait.modelId=1234;
    }
    Paint(g_mock.root);
    Check(g_originalCalls==2&&!g_mock.renderedReplacement&&g_mock.plannedModes==std::vector<bool>({false,false}),
          "a shared Neko model or portrait ID does not select replacement when survivor character IDs are unique");

    EnableReplacementFixture();Paint(g_mock.root);
    Check(g_originalCalls==0&&g_mock.renderedReplacement&&g_mock.draws==1&&g_mock.pushes==1&&g_mock.pops==1,
          "a trusted nine-survivor root skips only native painting and draws its prepared replacement");
    Check(g_mock.nativeReads==1&&g_mock.snapshots==1&&g_mock.plannedModes==std::vector<bool>{true}&&
          g_mock.stateReads==2&&g_mock.themeReads==0,
          "automatic replacement reads native data and the roster once and requires two fresh paint-state observations");
    Check(g_mock.events==std::vector<std::string>{"native","snapshot","paint_state","layout","paint_state","push","draw","pop"},
          "replacement preparation completes before a single push-draw-pop sequence without calling original");
    const auto snapshots=g_mock.snapshots,stateReads=g_mock.stateReads;
    Paint(999);
    Check(g_originalCalls==1&&g_mock.snapshots==snapshots&&g_mock.stateReads==stateReads&&g_policyUsed,
          "unrelated panels retain their original drawing and cannot reset the active replacement decision");

    EnableReplacementFixture(4);g_mock.snapshot.players[2].character=0;Paint(g_mock.root);
    Check(g_originalCalls==0&&g_mock.renderedReplacement&&g_mock.draws==1,
          "duplicate survivor character IDs trigger replacement even with fewer than nine survivors");
    EnableReplacementFixture();g_mock.rootHandle=0;Paint(g_mock.root);
    Check(g_originalCalls==0&&g_mock.renderedReplacement,
          "serial-numbered HPanel zero remains a valid fresh replacement identity");

    EnableReplacementFixture();g_nativeSkipSupported.store(false);Paint(g_mock.root);
    Check(g_originalCalls==1&&!g_mock.renderedReplacement&&g_mock.plannedModes==std::vector<bool>{false},
          "an unverified original hook chain is always called and cannot authorize native suppression");
    EnableReplacementFixture();Paint(g_mock.root);g_mock.nativeStateValid=false;Paint(g_mock.root);
    Check(g_originalCalls==1&&!g_mock.renderedReplacement&&g_mock.plannedModes==std::vector<bool>({true,false}),
          "a cached raw root cannot suppress another branch after its fresh native identity fails");
    for(int failure=0;failure<4;++failure) {
        EnableReplacementFixture();
        if(failure==0)g_mock.secondStateValid=false;
        if(failure==1)g_mock.changeHandle=true;
        if(failure==2)g_mock.changeEnabled=true;
        if(failure==3)g_mock.unsupportedOnSecond=true;
        Paint(g_mock.root);
        Check(g_originalCalls==1&&g_mock.draws==0&&g_mock.pushes==0&&g_mock.stateReads==2&&!g_stop.load(),
              "changed second identity, menu state or original-chain trust fails open without drawing a full duplicate layout");
    }

    EnableReplacementFixture(4);g_mock.nativeEnabled=false;g_nativeSkipSupported.store(false);Paint(g_mock.root);
    Check(g_originalCalls==1&&g_mock.renderedReplacement&&g_mock.draws==1&&g_mock.stateReads==2&&
          !g_mock.nativeEnabled&&!g_nativeSkipSupported.load(),
          "the user's native-HUD menu OFF setting draws the full replacement while retaining the original wrapper and setting");
    g_mock.nativeEnabled=true;Paint(g_mock.root);
    Check(g_originalCalls==2&&g_mock.draws==2&&!g_mock.renderedReplacement,
          "restoring the native menu on a healthy unique roster returns immediately to native painting and normal additions");

    for(int unavailable=0;unavailable<4;++unavailable) {
        EnableReplacementFixture();
        if(unavailable==0)g_mock.surfaceReady=false;
        if(unavailable==1)g_mock.paintRootReady=false;
        if(unavailable==2)g_mock.layoutHasCards=false;
        if(unavailable==3)g_mock.layoutSuppressed=true;
        Paint(g_mock.root);
        Check(g_originalCalls==1&&g_mock.draws==0&&g_mock.pushes==0&&!g_stop.load(),
              "missing paint context, empty layout or suppressed layout cannot hide the original native branch");
    }
    EnableReplacementFixture();g_mock.allowReplacementLayout=false;Paint(g_mock.root);
    Check(g_originalCalls==1&&g_mock.draws==1&&!g_mock.renderedReplacement&&
          g_mock.nativeReads==1&&g_mock.snapshots==1&&g_mock.plannedModes==std::vector<bool>({true,false}),
          "an unavailable full layout falls back to normal additions without rescanning native data or survivor state");
    EnableReplacementFixture();g_mock.ready=false;Paint(g_mock.root);
    Check(g_originalCalls==1&&g_mock.calls==1&&g_mock.classifications==0&&g_mock.snapshots==0,
          "an unready runtime leaves original painting intact without classification or preparation");
    EnableReplacementFixture();g_mock.snapshot.inGame=false;Paint(g_mock.root);
    Check(g_originalCalls==1&&g_mock.draws==0&&g_mock.plannedModes==std::vector<bool>{false},
          "a non-game snapshot never suppresses original UI or draws a replacement");

    EnableReplacementFixture();
    auto settings=CurrentSettings();settings.theme=neh::HudTheme::GameHud;PublishSettings(settings);Paint(g_mock.root);
    Check(g_originalCalls==0&&g_mock.themeReads==1&&g_mock.nativeReads==1&&g_mock.snapshots==1&&
          g_mock.plannedModes.size()==1&&g_mock.draws==1,
          "game-theme replacement samples its theme once in the same prepared frame");

    EnableReplacementFixture();Paint(g_mock.root);
    g_mock.snapshot=Roster(8);g_mock.snapshot.players[2].alive=false;Paint(g_mock.root);
    Check(g_originalCalls==0&&g_mock.draws==2&&g_mock.renderedReplacement,
          "a dead survivor cannot immediately release an automatic replacement and expose the buggy native branch");
    g_mock.snapshot.players[2].alive=true;g_mock.snapshot.players[2].character=-1;Paint(g_mock.root);
    Check(g_originalCalls==0&&g_mock.draws==3&&g_mock.renderedReplacement,
          "an unknown character observation holds the prior replacement decision");
    g_mock.snapshot=Roster(8);
    g_replacementPolicy.Observe(g_mock.snapshot,g_mock.rootHandle,GetTickCount()-2001u);Paint(g_mock.root);
    Check(g_originalCalls==1&&g_mock.draws==4&&!g_mock.renderedReplacement,
          "two continuous healthy unique seconds release native suppression without sleeping in the test");
    EnableReplacementFixture();Paint(g_mock.root);g_mock.snapshot=Roster(8);++g_mock.rootHandle;Paint(g_mock.root);
    Check(g_originalCalls==1&&!g_mock.renderedReplacement,
          "a fresh HPanel generation resets the previous map's held replacement policy");

    for(int fault=0;fault<2;++fault) {
        EnableReplacementFixture();
        if(fault==0)g_mock.snapshotFault=true;
        else {g_mock.themeFault=true;settings=CurrentSettings();settings.theme=neh::HudTheme::GameHud;PublishSettings(settings);}
        Paint(g_mock.root);
        Check(g_originalCalls==1&&g_mock.draws==0&&g_mock.pushes==0&&g_stop.load(),
              "a preparation fault calls original in the same frame and disables only this extension");
        const auto calls=g_mock.calls;Paint(g_mock.root);
        Check(g_originalCalls==2&&g_mock.calls==calls&&!g_policyUsed,
              "after a preparation fault original painting continues with no more engine work");
    }
    for(int fault=0;fault<3;++fault) {
        EnableReplacementFixture();
        if(fault==0)g_mock.pushFault=true;
        if(fault==1)g_mock.renderFault=true;
        if(fault==2)g_mock.popFault=true;
        Paint(g_mock.root);
        Check(g_originalCalls==1&&g_stop.load()&&g_mock.pushes==1&&g_mock.draws==(fault==0?0u:1u)&&
              g_mock.pops==(fault==0?0u:1u)&&g_mock.events.back()=="original",
              "a replacement context or draw fault restores the original exactly once after the failed render");
        const auto calls=g_mock.calls;Paint(g_mock.root);Paint(999);
        Check(g_originalCalls==3&&g_mock.calls==calls&&!g_policyUsed,
              "subsequent root and unrelated paints remain original-only after a render fault");
    }
    EnableReplacementFixture(4);g_mock.renderFault=true;Paint(g_mock.root);
    Check(g_originalCalls==1&&g_stop.load()&&g_mock.draws==1&&g_mock.pops==1,
          "a normal-additions render fault never paints original twice after its earlier original call");

    EnableReplacementFixture();g_mock.renderSubmitted=false;
    settings=CurrentSettings();settings.debug=true;PublishSettings(settings);Paint(g_mock.root);
    Check(g_originalCalls==1&&g_mock.draws==1&&g_mock.pushes==1&&g_mock.pops==1&&!g_stop.load()&&
          g_perf.active&&g_perf.nativeSuppressed==0&&g_perf.draws==0&&g_mock.events.back()=="original",
          "a silent renderer rejection restores native in the same frame without stopping or counting successful suppression");
    g_mock.renderSubmitted=true;Paint(g_mock.root);
    Check(g_originalCalls==1&&g_mock.draws==2&&g_mock.pushes==2&&g_mock.pops==2&&!g_stop.load()&&
          g_perf.nativeSuppressed==1&&g_perf.draws==1,
          "the next successful frame can replace native after a silent rejection and counts only its submitted replacement");

    EnableReplacementFixture();Paint(g_mock.root);settings=CurrentSettings();settings.enabled=false;PublishSettings(settings);
    const auto disabledCalls=g_mock.calls;Paint(g_mock.root);Paint(999);
    Check(g_originalCalls==2&&g_mock.calls==disabledCalls&&!g_policyUsed,
          "OFF restores original immediately and clears a latched policy without any Runtime work");
    Check(g_replacementPolicy.Observe(Roster(8),g_mock.rootHandle,GetTickCount())==neh::ReplacementReason::None,
          "OFF clears the pure policy state rather than leaving a delayed release behind");
    settings.enabled=true;PublishSettings(settings);g_mock.snapshot=Roster(8);Paint(g_mock.root);
    Check(g_originalCalls==3&&!g_mock.renderedReplacement,
          "re-enabling after OFF cannot revive the former automatic suppression on a healthy unique roster");
    EnableReplacementFixture();g_mock.disableDuringSnapshot=true;Paint(g_mock.root);
    Check(g_originalCalls==1&&g_mock.draws==0&&!g_extensionEnabled.load(),
          "an OFF change during preparation is checked again before original suppression or generated drawing");
    const auto changedCalls=g_mock.calls;Paint(g_mock.root);
    Check(g_originalCalls==2&&g_mock.calls==changedCalls&&!g_policyUsed,
          "the following OFF paint also clears mid-preparation policy state with zero engine calls");
}
}

int main() {
    static_assert(sizeof(void*)==4,"The game hook regression must run as x86.");
    ResetFixture();
    Paint(10);Paint(200);Paint(0);
    Check(g_originalCalls==3,"OFF still chains every original paint call");
    Check(g_mock.calls==0,"OFF calls no Runtime entry point, including Ready and IsTeamRoot");
    Check(!g_perf.active&&g_teamRootCache.panel==0,"OFF starts no profiling or root discovery");

    neh::Settings settings;settings.enabled=true;settings.debug=false;PublishSettings(settings);
    Paint(200);
    Check(g_mock.classifications==1&&g_mock.snapshots==0,"first unrelated panel is classified without drawing");
    Paint(10);
    Check(g_teamRootCache.panel==10&&g_mock.snapshots==1,"first team root is cached and its current snapshot is read");
    const auto originalCalls=g_originalCalls;
    const auto classifications=g_mock.classifications;
    for(unsigned panel=200;panel<1200;++panel)Paint(panel);
    Check(g_originalCalls==originalCalls+1000,"cached filtering preserves all original unrelated paints");
    Check(g_mock.classifications==classifications,"one thousand unrelated panels avoid engine classification after discovery");
    Check(g_mock.snapshots==1,"unrelated panels never refresh the survivor roster");
    const auto visibilityChecks=g_mock.visibilityChecks;
    Paint(10);
    Check(g_mock.visibilityChecks==visibilityChecks+1&&g_mock.snapshots==2,
          "cached team root still validates visibility and reads current survivor data");

    g_mock.visible=false;
    Paint(10);
    Check(g_teamRootCache.panel==0,"a hidden old root releases the positive cache");
    g_mock.root=20;g_mock.visible=true;
    Paint(20);
    Check(g_teamRootCache.panel==20&&g_mock.snapshots==3,"a visible replacement root is discovered after hiding the former root");

    settings.enabled=false;PublishSettings(settings);
    const auto runtimeCalls=g_mock.calls;
    g_perf.active=true;g_perf.hooks=123;
    neh::g_runtimeCounters.virtualQueries=321;
    Paint(20);Paint(250);
    Check(g_mock.calls==runtimeCalls,"turning OFF immediately stops all engine work on the very next paint");
    Check(g_perf.hooks==123&&neh::g_runtimeCounters.virtualQueries==321,
          "OFF performs no profile clock, counter reset, or profile bookkeeping");
    settings.enabled=true;settings.debug=false;PublishSettings(settings);
    const auto beforeReenable=g_mock.classifications;
    Paint(250);
    Check(g_mock.classifications==beforeReenable+1&&g_teamRootCache.panel==0,
          "OFF/ON revision invalidates a cached root before selecting a new map HUD");
    Check(!g_perf.active,"debug OFF avoids profile timing and drops the previous diagnostic window");

    // Deterministic production-cache checks use a supplied clock and identity
    // classifier, rather than sleeping or relying on the fixture's real clock.
    TeamRootCache cache;
    unsigned queried=0;
    std::uint32_t validRoot=10;
    auto classify=[&](std::uint32_t panel){++queried;return panel==validRoot;};
    Check(cache.Matches(10,100,1,classify)&&queried==1,"cold cache classifies its root once");
    Check(!cache.Matches(200,800,1,classify)&&queried==1,"unrelated panels are filtered within the one-second observation window");
    validRoot=20;
    Check(!cache.Matches(200,1100,1,classify)&&queried==2&&cache.panel==0,
          "one second without observing a root opens discovery for a replacement");
    Check(cache.Matches(20,1100,1,classify)&&queried==3,"replacement is accepted in the same discovery window");
    Check(cache.Matches(20,1599,1,classify)&&queried==3,"cached root is reused before its validation deadline");
    validRoot=30;
    Check(!cache.Matches(20,1600,1,classify)&&queried==4&&cache.panel==0,
          "identity failure at the half-second deadline drops a stale reused handle");
    Check(cache.Matches(30,1600,1,classify)&&queried==5,"fresh identity can be found immediately after a stale handle fails");
    Check(!cache.Matches(200,1601,2,classify)&&queried==6&&cache.panel==0,
          "settings revision opens discovery without waiting for the old root to expire");
    Check(!cache.Matches(0,1601,2,classify)&&queried==6,"null panels never reach engine classification");

    cache={};validRoot=10;queried=0;
    Check(cache.Matches(10,0xffffff00UL,1,classify),"root can be discovered immediately before the DWORD clock wraps");
    Check(cache.Matches(10,0xfffffff0UL,1,classify)&&queried==1,"pre-wrap validation deadline remains in the future");
    validRoot=20;
    Check(!cache.Matches(10,0x100UL,1,classify)&&queried==2,
          "post-wrap deadline still validates and rejects a stale identity");

    cache={};validRoot=10;queried=0;
    Check(!cache.Matches(200,100,1,classify)&&queried==1,"missing-root discovery records failed identity");
    for(unsigned i=0;i<1000;++i)cache.Matches(200,200,1,classify);
    Check(queried==1,"repeated menu panels avoid one thousand redundant engine identity probes");
    validRoot=200;
    Check(!cache.Matches(200,1099,1,classify)&&queried==1,
          "a recycled negative handle never authorizes an unverified draw");
    Check(cache.Matches(200,1100,1,classify)&&queried==2,
          "negative handle reuse is detected at its one-second deadline");
    cache={};validRoot=10;queried=0;
    cache.Matches(200,100,1,classify);
    validRoot=200;
    Check(cache.Matches(200,101,2,classify)&&queried==2,
          "settings revision immediately clears negative identities");
    cache={};validRoot=10;queried=0;
    cache.Matches(200,0xffffff00UL,1,classify);
    validRoot=200;
    Check(!cache.Matches(200,0x200UL,1,classify)&&queried==1,
          "negative expiry remains in the future across DWORD wrap");
    Check(cache.Matches(200,0x2e8UL,1,classify)&&queried==2,
          "negative cache revalidates exactly after wrap-safe deadline");
    cache={};validRoot=90000;queried=0;
    for(unsigned panel=1;panel<9000;++panel)cache.Matches(panel,100,1,classify);
    Check(cache.rejected.size()<=8192,"negative identity storage is bounded");
    Check(cache.Matches(90000,100,1,classify),"a new root can be found after negative capacity is exceeded");

    settings.enabled=true;settings.theme=neh::HudTheme::Current;PublishSettings(settings);
    g_mock.layoutHasCards=true;
    Paint(g_mock.root);
    Check(g_mock.themeReads==0,"current design never invokes live game HUD collection");
    Check(std::strstr(BuildMenu(),"Theme: Current design")!=nullptr,
          "menu exposes the preserved current design as the default theme");
    settings.theme=neh::HudTheme::GameHud;PublishSettings(settings);
    Paint(g_mock.root);
    Check(g_mock.themeReads==1,"selected game HUD theme collects only when extra cards are present");
    Check(std::strstr(BuildMenu(),"Theme: In-game HUD")!=nullptr,
          "menu reports the selected game HUD theme");
    g_mock.layoutHasCards=false;Paint(g_mock.root);
    Check(g_mock.themeReads==1,"empty layouts avoid all theme collection work");
    settings.enabled=false;PublishSettings(settings);Paint(g_mock.root);
    Check(g_mock.themeReads==1,"OFF skips theme collection even when game HUD is selected");

    Check(!settings.singlePlayerTest&&std::strstr(BuildMenu(),"Single-player test: OFF")!=nullptr,
          "single-player testing is opt-in and its menu shows OFF by default");
    settings.singlePlayerTest=true;PublishSettings(settings);
    Check(std::strstr(BuildMenu(),"Single-player test: ON")!=nullptr,
          "single-player menu reflects the published setting");
    const auto callsBeforeTestOff=g_mock.calls;
    Paint(g_mock.root);
    Check(g_mock.calls==callsBeforeTestOff,
          "single-player testing never bypasses the extension OFF fast gate");

    CheckAutomaticReplacementHook();
    g_original=nullptr;
    std::cout<<"Plugin hot path: "<<g_checks<<" checks passed\n";
    return 0;
}
