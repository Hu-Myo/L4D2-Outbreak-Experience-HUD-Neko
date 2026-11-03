#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <atomic>
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>
#include "types.hpp"
#include "game_hud_theme.hpp"
#include "hud_policy.hpp"
#include "paint_context.hpp"
#include "runtime_counters.hpp"
#pragma warning(push)
#pragma warning(disable:4100)
#include "../sdk/l4n_plugin.h"
#pragma warning(pop)

namespace {
constexpr char kSupportedHash[] = "f6f80f514088deb8ccfdecd7cd9c2eb9b43fedafd7c529eb1fc9f7300c0d2b50";
constexpr char kSupportedClientHash[] = "c0a8e1e88f7312db11bcfd4b0ec574d224a34b2d9a615d6088b28335eeb4af1f";
constexpr char kSupportedSurfaceHash[] = "3f2e2d74b3934a482caa8c3d5aa8d1157e04d719185b019ffaa1f9f7b4bb1d58";
constexpr char kSupportedVguiHash[] = "3188733e5cb87a2c72e616160e7c5298da065dcaa3248021f4abd5694eb701c1";
constexpr char kSupportedMaterialHash[] = "09fd324cd977d5d4e2a216ee831c07ff8b882efd024137c27de4155fbdfcf0ba";
constexpr std::size_t kPaintTraverseSlot = 41; // actual VGUI_Panel009, includes GetChildren
using PaintFn = void (__thiscall*)(void*, std::uint32_t, bool, bool);
neh::Runtime g_runtime;
neh::Renderer g_renderer;
std::mutex g_settingsMutex, g_logMutex;
neh::Settings g_settings;
std::wstring g_ini, g_log;
std::atomic<bool> g_stop{false}, g_started{false}, g_hooked{false};
std::atomic<bool> g_pathsReady{false};
std::atomic<bool> g_extensionEnabled{false},g_debugEnabled{false};
std::atomic<bool> g_nativeSkipSupported{false};
std::atomic<unsigned> g_settingsRevision{0};
PaintFn g_original = nullptr;
void** g_hookSlot = nullptr;
std::atomic<unsigned long> g_paints{0};
struct TeamRootCache {
    std::uint32_t panel=0;
    DWORD lastSeen=0,nextValidation=0;
    unsigned revision=0;
    // Only suppress failed identity probes. A reused negative handle is
    // rechecked within one second; it can never authorize a draw.
    std::unordered_map<std::uint32_t,DWORD> rejected;
    void Clear() { panel=0;lastSeen=nextValidation=0; }
    template<class Classify>
    bool Matches(std::uint32_t candidate,DWORD now,unsigned settingsRevision,Classify classify) {
        if(revision!=settingsRevision) { Clear();rejected.clear();revision=settingsRevision; }
        if(!candidate)return false;
        if(panel && candidate!=panel) {
            // A missing/destroyed map HUD cannot keep excluding a replacement.
            if(now-lastSeen<1000)return false;
            Clear();
        }
        if(candidate==panel && static_cast<LONG>(now-nextValidation)<0) {
            lastSeen=now;return true;
        }
        const auto negative=rejected.find(candidate);
        if(negative!=rejected.end()) {
            if(static_cast<LONG>(now-negative->second)<0)return false;
            rejected.erase(negative);
        }
        if(!classify(candidate)) {
            if(candidate==panel)Clear();
            if(rejected.size()>=8192)rejected.clear();
            rejected.emplace(candidate,now+1000);
            return false;
        }
        rejected.erase(candidate);
        panel=candidate;lastSeen=now;nextValidation=now+500;
        return true;
    }
};
thread_local TeamRootCache g_teamRootCache;
thread_local neh::ReplacementPolicy g_replacementPolicy;
thread_local bool g_policyUsed=false;
struct DrawFault {
    DWORD code=0;
    std::uintptr_t address=0;
    ULONG_PTR accessKind=0,accessTarget=0;
    bool hasAccessInfo=false;
    neh::DrawTrace trace;
};
thread_local DrawFault g_drawFault;
int CaptureDrawFault(EXCEPTION_POINTERS* info) {
    neh::AbortMemoryQueries();
    g_drawFault={};
    g_drawFault.trace=neh::g_drawTrace;
    if(info && info->ExceptionRecord) {
        const auto& record=*info->ExceptionRecord;
        g_drawFault.code=record.ExceptionCode;
        g_drawFault.address=reinterpret_cast<std::uintptr_t>(record.ExceptionAddress);
        if(record.ExceptionCode==EXCEPTION_ACCESS_VIOLATION && record.NumberParameters>=2) {
            g_drawFault.hasAccessInfo=true;
            g_drawFault.accessKind=record.ExceptionInformation[0];
            g_drawFault.accessTarget=record.ExceptionInformation[1];
        }
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
std::string ModuleAddress(std::uintptr_t address) {
    std::ostringstream out;
    HMODULE module=nullptr;
    if(address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   reinterpret_cast<LPCWSTR>(address),&module)) {
        char path[32768]{};GetModuleFileNameA(module,path,32768);
        const std::string full(path);const auto pos=full.find_last_of("\\/");
        out<<(pos==std::string::npos?full:full.substr(pos+1))<<"+0x"<<std::hex
           <<address-reinterpret_cast<std::uintptr_t>(module);
    } else out<<"0x"<<std::hex<<address;
    return out.str();
}
std::string DrawFaultMessage() {
    std::ostringstream out;
    out<<"native access fault: extension disabled; code=0x"<<std::hex<<g_drawFault.code<<std::dec
       <<" stage="<<g_drawFault.trace.stage<<" surface_slot="<<g_drawFault.trace.surfaceSlot
       <<" player="<<g_drawFault.trace.playerIndex<<" fault="<<ModuleAddress(g_drawFault.address)
       <<" call="<<ModuleAddress(g_drawFault.trace.function)
       <<" runtime_slot="<<g_drawFault.trace.runtimeSlot
       <<" runtime_panel=0x"<<std::hex<<g_drawFault.trace.runtimePanel<<std::dec
       <<" runtime_call="<<ModuleAddress(g_drawFault.trace.runtimeFunction);
    if(g_drawFault.hasAccessInfo)out<<" access="<<g_drawFault.accessKind<<" target=0x"<<std::hex<<g_drawFault.accessTarget;
    return out.str();
}

std::wstring GameRoot() {
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    std::wstring result(path);
    const auto pos = result.find_last_of(L"\\/");
    return pos == std::wstring::npos ? L"." : result.substr(0, pos);
}
void PreparePaths() {
    const auto dir = GameRoot() + L"\\left4dead2\\neko";
    CreateDirectoryW((GameRoot()+L"\\left4dead2").c_str(), nullptr);
    CreateDirectoryW(dir.c_str(), nullptr);
    g_ini = dir + L"\\extra_hud.ini";
    g_log = dir + L"\\extra_hud.log";
}
void Log(const std::string& message) {
    std::lock_guard<std::mutex> guard(g_logMutex);
    if (g_log.empty()) return;
    HANDLE file = CreateFileW(g_log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ,
                              nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    SYSTEMTIME t{}; GetLocalTime(&t);
    char stamp[64]{};
    wsprintfA(stamp, "%04u-%02u-%02u %02u:%02u:%02u ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond);
    const std::string line = std::string(stamp) + message + "\r\n";
    DWORD written=0; WriteFile(file,line.data(),static_cast<DWORD>(line.size()),&written,nullptr);
    CloseHandle(file);
}
enum class PerfStage:unsigned {Classify,Native,Snapshot,Layout,Theme,Diagnostic,Render,Total,Count};
struct PerfMetric {
    LONGLONG ticks=0,maxTicks=0;
    std::uint64_t samples=0;
    void Add(LONGLONG elapsed) {
        ticks+=elapsed;maxTicks=(std::max)(maxTicks,elapsed);++samples;
    }
};
struct PerfWindow {
    bool active=false;
    unsigned revision=0;
    DWORD started=0;
    LONGLONG frequency=0;
    std::uint64_t hooks=0,classifications=0,rootHits=0,draws=0,nativeSuppressed=0;
    PerfMetric stages[static_cast<unsigned>(PerfStage::Count)];
};
thread_local PerfWindow g_perf;
LONGLONG PerfNow() {
    LARGE_INTEGER value{};QueryPerformanceCounter(&value);return value.QuadPart;
}
void RecordPerf(PerfStage stage,LONGLONG started) {
    g_perf.stages[static_cast<unsigned>(stage)].Add(PerfNow()-started);
}
void BeginPerfWindow(bool enabled,DWORD now,unsigned revision) {
    if(!enabled) {
        if(g_perf.active) { g_perf={};neh::TakeRuntimeCounters(); }
        return;
    }
    if(g_perf.active && g_perf.revision==revision)return;
    g_perf={};g_perf.active=true;g_perf.started=now;g_perf.revision=revision;
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);g_perf.frequency=frequency.QuadPart;
    neh::TakeRuntimeCounters();
}
void MaybeLogPerf(DWORD now) {
    if(!g_perf.active || now-g_perf.started<5000 || g_perf.frequency<=0)return;
    const auto counters=neh::TakeRuntimeCounters();
    std::ostringstream out;
    out<<std::fixed<<std::setprecision(3)<<"profile: window_ms="<<(now-g_perf.started)
       <<" hooks="<<g_perf.hooks<<" classifications="<<g_perf.classifications
       <<" root_hits="<<g_perf.rootHits<<" draws="<<g_perf.draws
       <<" native_suppressed="<<g_perf.nativeSuppressed;
    const char* names[]={"classify","native","snapshot","layout","theme","diagnostic","render","total"};
    for(unsigned i=0;i<static_cast<unsigned>(PerfStage::Count);++i) {
        const auto& metric=g_perf.stages[i];
        const double msPerTick=1000.0/static_cast<double>(g_perf.frequency);
        out<<' '<<names[i]<<"_samples="<<metric.samples
           <<' '<<names[i]<<"_ms_sum="<<metric.ticks*msPerTick
           <<' '<<names[i]<<"_ms_avg="<<(metric.samples?metric.ticks*msPerTick/metric.samples:0.0)
           <<' '<<names[i]<<"_ms_max="<<metric.maxTicks*msPerTick;
    }
    out<<" vq="<<counters.virtualQueries<<" nodes="<<counters.traversedNodes
       <<" native_discoveries="<<counters.nativeDiscoveries
       <<" visual_discoveries="<<counters.visualDiscoveries
       <<" self_discoveries="<<counters.selfDiscoveries
       <<" expired_panels="<<counters.expiredPanelRefs
       <<" health_native="<<counters.nativeHealthReads
       <<" health_resource="<<counters.resourceHealthReads
       <<" temp_samples="<<counters.temporaryHealthSamples
       <<" theme_reads="<<counters.themeReads
       <<" theme_captures="<<counters.themeCaptures
       <<" theme_fallback_cards="<<counters.themeFallbackCards
       <<" theme_texture_queries="<<counters.themeTextureQueries
       <<" theme_texture_misses="<<counters.themeTextureMisses
       <<" theme_adapted_state_cards="<<counters.themeAdaptedStateCards
       <<" theme_item_fallbacks="<<counters.themeItemFallbacks
       <<" model_portrait_inspections="<<counters.modelPortraitInspections
       <<" model_portrait_lookups="<<counters.modelPortraitLookups
       <<" model_portrait_cache_hits="<<counters.modelPortraitCacheHits
       <<" model_portrait_load_attempts="<<counters.modelPortraitLoadAttempts
       <<" model_portrait_draws="<<counters.modelPortraitDraws;
    Log(out.str());
    const auto revision=g_perf.revision;
    g_perf={};g_perf.active=true;g_perf.started=now;g_perf.revision=revision;
    LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);g_perf.frequency=frequency.QuadPart;
}
neh::Settings CurrentSettings() {
    std::lock_guard<std::mutex> guard(g_settingsMutex);
    return g_settings;
}
void PublishSettings(const neh::Settings& settings) {
    {std::lock_guard<std::mutex> guard(g_settingsMutex);g_settings=settings;}
    g_debugEnabled.store(settings.debug,std::memory_order_release);
    g_extensionEnabled.store(settings.enabled,std::memory_order_release);
    g_settingsRevision.fetch_add(1,std::memory_order_release);
}
void WriteSettings(const neh::Settings& s) {
    auto put = [&](const wchar_t* key, int value) {
        const auto text=std::to_wstring(value);
        WritePrivateProfileStringW(L"hud",key,text.c_str(),g_ini.c_str());
    };
    put(L"enabled",s.enabled); put(L"show_overflow",s.overflow); put(L"debug_log",s.debug);
    put(L"scale_percent",static_cast<int>(s.scale*100.0f+0.5f));
    put(L"gap",s.gap); put(L"row_gap",s.rowGap); put(L"offset_x",s.offsetX); put(L"offset_y",s.offsetY);
    put(L"theme",static_cast<int>(s.theme));
    put(L"single_player_test",s.singlePlayerTest);
}
void LoadSettings() {
    neh::Settings s;
    auto get=[&](const wchar_t* key,int def) {return static_cast<int>(GetPrivateProfileIntW(L"hud",key,def,g_ini.c_str()));};
    s.enabled=get(L"enabled",1)!=0; s.overflow=get(L"show_overflow",0)!=0; s.debug=get(L"debug_log",1)!=0;
    s.scale=std::clamp(get(L"scale_percent",85),50,125)/100.0f;
    s.gap=std::clamp(get(L"gap",6),0,60); s.rowGap=std::clamp(get(L"row_gap",8),0,80);
    s.offsetX=std::clamp(get(L"offset_x",0),-2000,2000); s.offsetY=std::clamp(get(L"offset_y",0),-2000,2000);
    s.theme=get(L"theme",0)==1?neh::HudTheme::GameHud:neh::HudTheme::Current;
    s.singlePlayerTest=get(L"single_player_test",0)!=0;
    PublishSettings(s);
    WriteSettings(s);
    Log(std::string("settings: enabled=")+(s.enabled?"1":"0")+" overflow="+(s.overflow?"1":"0"));
}
std::string FileHash(const wchar_t* path) {
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return {};
    BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    DWORD objectSize=0, read=0, ignored=0; unsigned char buffer[4096], digest[32]{};
    bool ok=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(ok)ok=BCryptGetProperty(algorithm,BCRYPT_OBJECT_LENGTH,reinterpret_cast<PUCHAR>(&objectSize),sizeof(objectSize),&ignored,0)>=0;
    std::vector<unsigned char> object(objectSize);
    if(ok)ok=BCryptCreateHash(algorithm,&hash,object.data(),objectSize,nullptr,0,0)>=0;
    while(ok) {
        if(!ReadFile(file,buffer,sizeof(buffer),&read,nullptr)){ok=false;break;}
        if(!read)break;
        ok=BCryptHashData(hash,buffer,read,0)>=0;
    }
    if(ok)ok=BCryptFinishHash(hash,digest,sizeof(digest),0)>=0;
    if(hash)BCryptDestroyHash(hash); if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0); CloseHandle(file);
    if(!ok)return {};
    std::ostringstream out; out<<std::hex<<std::setfill('0');
    for(auto byte:digest)out<<std::setw(2)<<static_cast<unsigned>(byte);
    return out.str();
}
struct PreparedFrame {
    neh::Settings settings;
    neh::Snapshot snapshot;
    std::vector<neh::HudSlot> slots;
    neh::Layout layout;
    const neh::GameHudTheme* theme=nullptr;
    neh::NativeHudPaintState nativePaint;
    neh::ReplacementReason reason=neh::ReplacementReason::None;
    std::uint32_t root=0;
    void* surface=nullptr;
    bool nativePaintValid=false,drawable=false,suppressNative=false,submitted=false;
};
void PrepareExtras(std::uint32_t panel,PreparedFrame& frame,DWORD now) {
    if(g_stop.load())return;
    frame.settings=CurrentSettings();
    const auto& settings=frame.settings;
    if(!settings.enabled)return;
    if(!g_runtime.TeamHudVisible(panel)) {
        // A hidden former root must not exclude a visible replacement forever.
        g_teamRootCache.Clear();
        return;
    }
    const bool profile=settings.debug && g_perf.active;
    auto mark=profile?PerfNow():0;
    std::string nativeError,snapshotError;
    neh::g_drawTrace.stage="runtime.native";
    auto& slots=frame.slots;slots=g_runtime.ReadNativeSlots(panel,nativeError);
    if(profile)RecordPerf(PerfStage::Native,mark);
    mark=profile?PerfNow():0;
    neh::g_drawTrace.stage="runtime.snapshot";
    auto& snapshot=frame.snapshot;snapshot=g_runtime.ReadSnapshot(snapshotError);
    if(profile)RecordPerf(PerfStage::Snapshot,mark);
    mark=profile?PerfNow():0;
    int width=0,height=0; g_runtime.ScreenSize(width,height);
    neh::g_drawTrace.stage="layout";
    frame.nativePaintValid=g_runtime.ReadNativeHudPaintState(panel,frame.nativePaint);
    if(frame.nativePaintValid && snapshot.inGame) {
        g_policyUsed=true;
        frame.reason=g_replacementPolicy.Observe(snapshot,frame.nativePaint.rootHandle,now);
        if(!frame.nativePaint.enabled)frame.reason=neh::ReplacementReason::NativeMenuOff;
    }
    const bool replacementRequested=frame.nativePaintValid && frame.reason!=neh::ReplacementReason::None &&
        (!frame.nativePaint.enabled || g_nativeSkipSupported.load(std::memory_order_acquire));
    auto& layout=frame.layout;
    layout=neh::PlanLayout(snapshot,slots,settings,width,height,replacementRequested);
    if(replacementRequested && !layout.replacesNative && frame.nativePaint.enabled)
        layout=neh::PlanLayout(snapshot,slots,settings,width,height,false);
    if(profile)RecordPerf(PerfStage::Layout,mark);
    auto& theme=frame.theme;
    std::string themeError;
    if(settings.theme==neh::HudTheme::GameHud && !layout.cards.empty()) {
        mark=profile?PerfNow():0;
        neh::g_drawTrace.stage="runtime.theme";
        theme=&g_runtime.ReadGameHudTheme(snapshot,slots,width,height,themeError);
        if(profile)RecordPerf(PerfStage::Theme,mark);
    }
    if(settings.debug) {
        neh::g_drawTrace.stage="diagnostic";
        mark=profile?PerfNow():0;
        static std::string previous;
        std::ostringstream description;
        description<<(!nativeError.empty()?nativeError:!snapshotError.empty()?snapshotError:layout.reason)
                   <<" survivors="<<snapshot.players.size()<<" native=[";
        for(const auto& slot:slots)if(slot.visible)description<<slot.playerIndex<<',';
        description<<"] extras=[";for(const auto& card:layout.cards)description<<card.playerIndex<<':'<<card.row<<',';
        description<<"] hidden="<<layout.hiddenOverflow<<" overflow="<<settings.overflow;
        description<<" theme="<<(settings.theme==neh::HudTheme::GameHud?"game_hud":"current");
        description<<" single_player_test="<<settings.singlePlayerTest;
        description<<" replacement="<<layout.replacesNative
                   <<" replacement_reason="<<neh::ReplacementReasonText(frame.reason)
                   <<" native_paint_valid="<<frame.nativePaintValid;
        if(theme)description<<" theme_status=["<<theme->status<<']';
        if(!themeError.empty())description<<" theme_note=["<<themeError<<']';
        description<<" screen="<<width<<'x'<<height<<" local="<<snapshot.localIndex<<" view="<<snapshot.viewIndex;
        if(!layout.detail.empty())description<<" detail=["<<layout.detail<<']';
        description<<" roster=[";
        for(const auto& player:snapshot.players)description<<player.index<<',';
        description<<"] survivor_set="<<snapshot.survivorSet<<" raw_characters=[";
        for(const auto& player:snapshot.players)description<<player.index<<':'<<player.rawCharacter<<',';
        description<<"] characters=[";
        for(const auto& player:snapshot.players)description<<player.index<<':'<<player.character<<',';
        description<<"] model_portraits=[";
        for(const auto& player:snapshot.players)if(player.modelPortrait.revision)
            description<<player.index<<':'<<player.modelPortrait.modelId<<':'
                       <<player.modelPortrait.normalId<<':'<<player.modelPortrait.incapId<<',';
        description<<"] bounds=[";
        auto rect=[&](const neh::Rect& r){description<<r.x<<','<<r.y<<','<<r.w<<','<<r.h;};
        for(const auto& slot:slots) {
            description<<slot.playerIndex<<":(";rect(slot.bounds);description<<')';
            if(slot.hasVisibleBounds){description<<"->(";rect(slot.visibleBounds);description<<')'
                <<(slot.usesVisualBounds?"[content]":"[container]");}
            description<<';';
        }
        description<<"] selfHud=";
        if(snapshot.hasSelfHudBounds){description<<'(';rect(snapshot.selfHudBounds);description<<')';}
        else description<<"unavailable";
        const auto diagnostic=description.str();
        if(diagnostic!=previous){Log("layout: "+diagnostic);previous=diagnostic;}
        if(profile)RecordPerf(PerfStage::Diagnostic,mark);
    }
    if(layout.cards.empty() || layout.suppressed)return;
    frame.root=g_runtime.RootPanel();frame.surface=g_runtime.SurfaceInterface();
    if(!frame.root || !frame.surface)return;
    if(layout.replacesNative) {
        // A cached VPANEL is discovery only. Hiding is authorized solely by a
        // fresh exact class/ClientDLL owner/Neko wrapper/HPanel check after
        // all source reads, and only when this frame can draw its replacement.
        neh::NativeHudPaintState current;
        if(!frame.nativePaintValid || !g_runtime.ReadNativeHudPaintState(panel,current) ||
           current.rootHandle!=frame.nativePaint.rootHandle || current.enabled!=frame.nativePaint.enabled)return;
        frame.suppressNative=current.enabled && g_nativeSkipSupported.load(std::memory_order_acquire);
        if(current.enabled && !frame.suppressNative)return;
    }
    frame.drawable=true;
}
bool GuardedPrepare(std::uint32_t panel,PreparedFrame& frame,DWORD now) {
    neh::g_drawTrace={};
    __try { PrepareExtras(panel,frame,now);return true; }
    __except(CaptureDrawFault(GetExceptionInformation())) { return false; }
}
void RenderExtras(PreparedFrame& frame) {
    const bool profile=frame.settings.debug && g_perf.active;
    const auto mark=profile?PerfNow():0;
    neh::WithPaintContext(frame.surface,frame.root,[](void* ptr) {
        auto& d=*static_cast<PreparedFrame*>(ptr);d.submitted=g_renderer.Draw(d.snapshot,d.layout,d.settings,d.theme);
    },&frame);
    if(profile && frame.submitted)++g_perf.draws;
    if(profile)RecordPerf(PerfStage::Render,mark);
}
bool GuardedRender(PreparedFrame& frame) {
    __try {RenderExtras(frame);return true;}
    __except(CaptureDrawFault(GetExceptionInformation())) {return false;}
}
void __fastcall PaintHook(void* self,void*,std::uint32_t panel,bool force,bool allow) {
    const auto original=g_original;
    // OFF performs no panel lookup, mutex acquisition, clock, or profiling.
    if(g_stop.load() || !g_extensionEnabled.load(std::memory_order_acquire)) {
        if(g_policyUsed) {g_replacementPolicy.Reset();g_policyUsed=false;}
        if(original)original(self,panel,force,allow);
        return;
    }
    if(!g_runtime.Ready()) {if(original)original(self,panel,force,allow);return;}
    const auto now=GetTickCount();
    const auto revision=g_settingsRevision.load(std::memory_order_acquire);
    const bool profile=g_debugEnabled.load(std::memory_order_acquire);
    BeginPerfWindow(profile,now,revision);
    if(profile)++g_perf.hooks;
    const bool root=g_teamRootCache.Matches(panel,now,revision,[profile](std::uint32_t candidate) {
        const auto started=profile?PerfNow():0;
        if(profile)++g_perf.classifications;
        const bool match=g_runtime.IsTeamRoot(candidate);
        if(profile)RecordPerf(PerfStage::Classify,started);
        return match;
    });
    if(!root) {if(original)original(self,panel,force,allow);MaybeLogPerf(now);return;}
    if(profile)++g_perf.rootHits;
    ++g_paints;
    // Prepare once before the native branch so its actual survivor identities,
    // not its transient down/dead paint, choose the automatic replacement.
    PreparedFrame frame;
    const auto started=profile?PerfNow():0;
    const bool prepared=GuardedPrepare(panel,frame,now);
    LONGLONG pluginTicks=profile?PerfNow()-started:0;
    if(!prepared) {
        g_stop.store(true);
        Log(DrawFaultMessage());
    }
    if(g_stop.load() || !g_extensionEnabled.load(std::memory_order_acquire)) {
        frame.suppressNative=false;frame.drawable=false;
    }
    // The pinned IPanel41 original is a pure ClientPanel::PaintTraverse
    // forwarder. Skipping just this freshly verified CHudTeamDisplay branch
    // matches Neko's existing team-HUD paint switch, without changing it.
    if(!frame.suppressNative) {if(original)original(self,panel,force,allow);}
    const auto renderStarted=profile?PerfNow():0;
    bool renderSucceeded=true;
    if(prepared && frame.drawable && !(renderSucceeded=GuardedRender(frame))) {
        g_stop.store(true);Log(DrawFaultMessage());
    }
    if(frame.suppressNative) {
        if(!renderSucceeded || !frame.submitted) {
            // Font setup can fail without an exception. Restore the native
            // branch unless a replacement card was actually submitted.
            if(original)original(self,panel,force,allow);
        } else if(profile)++g_perf.nativeSuppressed;
    }
    if(profile)g_perf.stages[static_cast<unsigned>(PerfStage::Total)].Add(pluginTicks+PerfNow()-renderStarted);
    MaybeLogPerf(now);
}
bool InstallHook() {
    auto panel=g_runtime.PanelInterface(); if(!panel)return false;
    auto slot=&(*reinterpret_cast<void***>(panel))[kPaintTraverseSlot];
    DWORD old=0;
    if(!VirtualProtect(slot,sizeof(void*),PAGE_READWRITE,&old))return false;
    g_original=reinterpret_cast<PaintFn>(*slot); g_hookSlot=slot;
    g_nativeSkipSupported.store(reinterpret_cast<std::uintptr_t>(g_original)==
        reinterpret_cast<std::uintptr_t>(GetModuleHandleW(L"vgui2.dll"))+0x1ebe0u,std::memory_order_release);
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(slot),reinterpret_cast<void*>(&PaintHook));
    DWORD ignored=0; VirtualProtect(slot,sizeof(void*),old,&ignored);
    g_hooked.store(true); return true;
}
DWORD WINAPI InitializeWorker(void*) {
    PreparePaths(); LoadSettings();
    g_pathsReady.store(true);
    Log("NekoExtraHUD 0.1.16 experimental x86: starting; game playtest required.");
    for(int attempt=0;attempt<600&&!g_stop.load();++attempt) {
        const auto neko=GetModuleHandleW(L"left4neko.dll");
        const auto client=GetModuleHandleW(L"client.dll");
        const auto surface=GetModuleHandleW(L"vguimatsurface.dll");
        const auto vgui=GetModuleHandleW(L"vgui2.dll");
        if(neko && client && GetModuleHandleW(L"engine.dll") &&
            vgui && surface) {
            wchar_t file[32768]{}; GetModuleFileNameW(neko,file,32768);
            const auto hash=FileHash(file);
            Log("Neko SHA256="+hash);
            if(hash!=kSupportedHash) {
                Log("unsupported Neko build: no native hook installed. This test build supports the inspected 2.51.0 DLL only.");
                return 0;
            }
            wchar_t clientFile[32768]{};
            GetModuleFileNameW(client,clientFile,32768);
            const auto clientHash=FileHash(clientFile);
            Log("client SHA256="+clientHash);
            if(clientHash!=kSupportedClientHash) {
                Log("unsupported client build: no native hook installed. This test build requires the inspected L4D2 client DLL.");
                return 0;
            }
            wchar_t surfaceFile[32768]{};
            GetModuleFileNameW(surface,surfaceFile,32768);
            const auto surfaceHash=FileHash(surfaceFile);
            Log("surface SHA256="+surfaceHash);
            if(surfaceHash!=kSupportedSurfaceHash) {
                Log("unsupported surface build: no native hook installed. This test build requires the inspected Surface031 DLL.");
                return 0;
            }
            wchar_t vguiFile[32768]{};
            GetModuleFileNameW(vgui,vguiFile,32768);
            const auto vguiHash=FileHash(vguiFile);
            Log("vgui SHA256="+vguiHash);
            if(vguiHash!=kSupportedVguiHash) {
                Log("unsupported vgui build: no native hook installed. Safe panel handles require the inspected IVGui008 DLL.");
                return 0;
            }
            std::string error;
            if(g_runtime.Initialize(error)) {
                // This optional interface is used only to load one portrait
                // for an already-applied Neko model. Other HUD paths retain
                // their existing four-module profile when it is unavailable.
                void* materials=nullptr;
                const auto materialModule=GetModuleHandleW(L"materialsystem.dll");
                wchar_t materialFile[32768]{};
                if(materialModule && GetModuleFileNameW(materialModule,materialFile,32768)) {
                    const auto materialHash=FileHash(materialFile);
                    Log("materialsystem SHA256="+materialHash);
                    if(materialHash==kSupportedMaterialHash) {
                        using Factory=void*(__cdecl*)(const char*,int*);
                        const auto factory=reinterpret_cast<Factory>(GetProcAddress(materialModule,"CreateInterface"));
                        if(factory)materials=factory("VMaterialSystem080",nullptr);
                    }
                }
                Log(materials?"Neko portrait material loading: supported; automatic with applied model":
                              "Neko portrait material loading: unavailable; existing loaded textures only");
                g_renderer.Initialize(g_runtime.SurfaceInterface(),materials);
                if(InstallHook()) {
                    Log("hook installed; waiting for visible CHudTeamDisplay and validated teammate targets.");
                    Log(g_nativeSkipSupported.load()?"automatic native replacement: supported PaintTraverse forwarder":
                        "automatic native replacement: original forwarder changed; suppression unavailable");
                    return 0;
                }
                Log("cannot install UI hook"); return 0;
            }
            if(attempt%50==0)Log("waiting: "+error);
        }
        Sleep(100);
    }
    if(!g_stop.load())Log("startup timed out: required game UI interfaces not available");
    return 0;
}
void Start() {
    bool expected=false; if(!g_started.compare_exchange_strong(expected,true))return;
    // The Neko SDK has no unload notification. Keep callbacks and hook code
    // mapped until process exit; installing/updating requires a game restart.
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
                          reinterpret_cast<LPCWSTR>(&Start),&pinned)) {
        g_started.store(false);return;
    }
    HANDLE worker=CreateThread(nullptr,0,InitializeWorker,nullptr,0,nullptr);
    if(worker)CloseHandle(worker); else g_started.store(false);
}
void Detach() {
    g_stop.store(true);
    if(!g_hooked.load()||!g_hookSlot)return;
    MEMORY_BASIC_INFORMATION info{};
    if(!VirtualQuery(g_hookSlot,&info,sizeof(info)) || info.State!=MEM_COMMIT)return;
    DWORD old=0;
    if(VirtualProtect(g_hookSlot,sizeof(void*),PAGE_READWRITE,&old)) {
        InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(g_hookSlot),reinterpret_cast<void*>(g_original),reinterpret_cast<void*>(&PaintHook));
        DWORD ignored=0;VirtualProtect(g_hookSlot,sizeof(void*),old,&ignored);
    }
}
enum class Action:std::uintptr_t {Enabled=1,Overflow,Debug,Reload,Diagnostics,Theme,SinglePlayerTest};
const char* BuildMenu();
const char* __cdecl MenuAction(void* user) {
    if(!g_pathsReady.load())return nullptr;
    const auto action=static_cast<Action>(reinterpret_cast<std::uintptr_t>(user));
    if(action==Action::Reload)LoadSettings();
    else if(action==Action::Diagnostics)Log("diagnostics: paints="+std::to_string(g_paints.load())+" "+
        (g_hooked.load()?g_runtime.Diagnostics():"not hooked: initializing or unsupported build; see startup log"));
    else {
        auto settings=CurrentSettings();
        if(action==Action::Enabled)settings.enabled=!settings.enabled;
        if(action==Action::Overflow)settings.overflow=!settings.overflow;
        if(action==Action::Debug)settings.debug=!settings.debug;
        if(action==Action::Theme)settings.theme=settings.theme==neh::HudTheme::Current?
            neh::HudTheme::GameHud:neh::HudTheme::Current;
        if(action==Action::SinglePlayerTest)settings.singlePlayerTest=!settings.singlePlayerTest;
        PublishSettings(settings);
        WriteSettings(settings);
    }
    return BuildMenu();
}
const char* BuildMenu() {
    static std::string menu;
    const auto settings=CurrentSettings();
    std::ostringstream out;
    out<<"\"Extra Survivor HUD\" { \"_refresh\" \"1\" ";
    auto item=[&](const std::string& label,Action action) {
        out<<'"'<<label<<"\" { \"callback\" \"0x"<<std::hex<<reinterpret_cast<std::uintptr_t>(&MenuAction)
           <<"\" \"user_data\" \"0x"<<static_cast<std::uintptr_t>(action)<<"\" } ";
    };
    item(std::string("Extension: ")+(settings.enabled?"ON":"OFF"),Action::Enabled);
    item(std::string("8th+ teammate cards: ")+(settings.overflow?"ON":"OFF"),Action::Overflow);
    item(std::string("Theme: ")+(settings.theme==neh::HudTheme::GameHud?"In-game HUD":"Current design"),Action::Theme);
    item(std::string("Single-player test: ")+(settings.singlePlayerTest?"ON":"OFF"),Action::SinglePlayerTest);
    item(std::string("Diagnostic logging: ")+(settings.debug?"ON":"OFF"),Action::Debug);
    item("Reload extra_hud.ini",Action::Reload); item("Write diagnostic snapshot",Action::Diagnostics);
    out<<'}';menu=out.str();return menu.c_str();
}
class ExtraHudPlugin final:public IL4NPlugin {
public:
    const char* GetName() override{return "NekoExtraHUD";}
    const char* GetVersion() override{return "0.1.16-test";}
    void OnGameLaunch() override{Start();}
    void OnModuleLoaded(const char*,std::uintptr_t) override{Start();}
    const char* RequestHudMenu(bool title) override{return title?"Extra Survivor HUD":BuildMenu();}
};
} // namespace
extern "C" __declspec(dllexport) IL4NPlugin* GetL4NPluginInstance() {
    static ExtraHudPlugin plugin; return &plugin;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved) {
    if(reason==DLL_PROCESS_ATTACH)DisableThreadLibraryCalls(instance);
    if(reason==DLL_PROCESS_DETACH && reserved==nullptr)Detach();
    return TRUE;
}
