#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "types.hpp"
#include "survivor_character.hpp"
#include "game_hud_theme.hpp"
#include "runtime_counters.hpp"
#include "draw_trace.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace neh {
namespace {
static_assert(sizeof(void*) == 4, "Neko 2.51.0 is an x86 process; build Win32.");

// VGUI_Panel009 used by the inspected Neko build includes GetChildren at 19.
// The older hl2sdk/l4d2 header omits that method; every later slot is shifted.
// These slots match Valve's modern IPanel.h AND Neko's actual instructions:
// GetChildren=0x4c, GetName=0x90, GetPanel=0xdc.
namespace pi {
constexpr int pos=3,size=5,absPos=10,clipRect=11,inset=13,visible=15,childCount=17,
              child=18, parent=20, fullyVisible=26, name=36, className=37,
              getPanel=55;
}
// Verified against client.dll SHA256 c0a8e1e88f7312db11bcfd4b0ec574d224a34b2d9a615d6088b28335eeb4af1f.
// This game's IClientNetworkable omits the Release method present in the old
// public header. The plugin checks the client hash before initializing here.
namespace ni { constexpr int clientClass=1, dataTableBase=10; }

// Query metadata is a hint within one UI read pass, never a cached object,
// vtable, function pointer, or player value. Actual access remains guarded.
struct CachedMemoryRegion {
    std::uintptr_t begin=0,end=0;
    MEMORY_BASIC_INFORMATION info{};
};
struct MemoryQueryCache {
    std::array<CachedMemoryRegion,128> regions{};
    std::size_t count=0,lastHit=0,nextEviction=0;
    unsigned depth=0;
    std::uint64_t generation=0;
    void ClearEntries() { count=lastHit=nextEviction=0; }
    void Abort() { ClearEntries();depth=0;++generation; }
};
thread_local MemoryQueryCache g_memoryQueries;
void InvalidateMemoryQueries() { g_memoryQueries.ClearEntries(); }
class MemoryQueryScope {
public:
    explicit MemoryQueryScope(bool freshPass=false) {
        if(freshPass || !g_memoryQueries.depth)g_memoryQueries.Abort();
        generation_=g_memoryQueries.generation;
        ++g_memoryQueries.depth;
    }
    ~MemoryQueryScope() {
        if(generation_!=g_memoryQueries.generation || !g_memoryQueries.depth)return;
        if(!--g_memoryQueries.depth)g_memoryQueries.ClearEntries();
    }
    MemoryQueryScope(const MemoryQueryScope&)=delete;
    MemoryQueryScope& operator=(const MemoryQueryScope&)=delete;
private:
    std::uint64_t generation_=0;
};
bool QueryMemoryRegion(const void* address,MEMORY_BASIC_INFORMATION& page) {
    const auto value=reinterpret_cast<std::uintptr_t>(address);
    if(!value)return false;
    auto& cache=g_memoryQueries;
    if(cache.depth && cache.count) {
        const auto contains=[value](const CachedMemoryRegion& region) {
            return value>=region.begin && value<region.end;
        };
        if(contains(cache.regions[cache.lastHit])) {
            page=cache.regions[cache.lastHit].info;return true;
        }
        for(std::size_t i=0;i<cache.count;++i)if(i!=cache.lastHit && contains(cache.regions[i])) {
            cache.lastHit=i;page=cache.regions[i].info;return true;
        }
    }
    ++g_runtimeCounters.virtualQueries;
    if(!VirtualQuery(address,&page,sizeof(page))) {
        InvalidateMemoryQueries();return false;
    }
    // Do not retain negative/reserved/guard results: the engine may allocate
    // or commit a new object before this pass ends.
    const auto begin=reinterpret_cast<std::uintptr_t>(page.BaseAddress);
    const auto end=begin+page.RegionSize;
    const DWORD protection=page.Protect&0xff;
    if(cache.depth && page.State==MEM_COMMIT && !(page.Protect&PAGE_GUARD) &&
       protection && protection!=PAGE_NOACCESS && end>begin && value>=begin && value<end) {
        const auto index=cache.count<cache.regions.size()?cache.count++:cache.nextEviction;
        if(cache.count==cache.regions.size())cache.nextEviction=(index+1)%cache.regions.size();
        cache.regions[index]={begin,end,page};cache.lastHit=index;
    }
    return true;
}

bool Readable(const void* address, std::size_t size) {
    auto current = reinterpret_cast<std::uintptr_t>(address);
    if (!current || !size || current + size < current) return false;
    const auto end = current + size;
    while (current < end) {
        MEMORY_BASIC_INFORMATION page{};
        if (!QueryMemoryRegion(reinterpret_cast<void*>(current),page)) return false;
        if (page.State != MEM_COMMIT || (page.Protect & PAGE_GUARD) ||
            (page.Protect & 0xff) == PAGE_NOACCESS) {InvalidateMemoryQueries();return false;}
        const auto next = reinterpret_cast<std::uintptr_t>(page.BaseAddress) + page.RegionSize;
        if (next <= current) {InvalidateMemoryQueries();return false;}
        current = next;
    }
    return true;
}
bool Executable(const void* address) {
    MEMORY_BASIC_INFORMATION page{};
    if (!address) return false;
    if (!QueryMemoryRegion(address,page)) return false;
    const DWORD p = page.Protect & 0xff;
    const bool executable=page.State == MEM_COMMIT && !(page.Protect & PAGE_GUARD) &&
        (p == PAGE_EXECUTE || p == PAGE_EXECUTE_READ || p == PAGE_EXECUTE_READWRITE || p == PAGE_EXECUTE_WRITECOPY);
    if(!executable)InvalidateMemoryQueries();
    return executable;
}
bool CopyBytes(const void* source, void* destination, std::size_t size) {
    if (!Readable(source, size)) return false;
    __try { std::memcpy(destination, source, size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { InvalidateMemoryQueries();return false; }
}
template<class T> bool Read(const void* source, T& value) {
    return CopyBytes(source, &value, sizeof(value));
}
const unsigned char* Add(const void* base, int offset) {
    return reinterpret_cast<const unsigned char*>(base) + offset;
}
void* Method(void* object, int slot) {
    void** table = nullptr;
    void* fn = nullptr;
    if (!Read(object, table) || slot < 0 || slot > 512 || !Read(table + slot, fn) || !Executable(fn)) return nullptr;
    TraceRuntimeMethod(slot,fn);
    return fn;
}
template<class R, class... A> bool Call(void* object, int slot, R& output, A... args) {
    void* fn = Method(object, slot);
    if (!fn) return false;
    __try {
        output = reinterpret_cast<R(__thiscall*)(void*, A...)>(fn)(object, args...);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { InvalidateMemoryQueries();return false; }
}
template<class... A> bool CallVoid(void* object, int slot, A... args) {
    void* fn = Method(object, slot);
    if (!fn) return false;
    __try {
        reinterpret_cast<void(__thiscall*)(void*, A...)>(fn)(object, args...);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { InvalidateMemoryQueries();return false; }
}
bool CopyString(const char* text, char* output, std::size_t capacity) {
    if (!text || capacity < 2 || !Readable(text, 1)) return false;
    __try {
        std::size_t i = 0;
        for (; i + 1 < capacity && text[i]; ++i) output[i] = text[i];
        output[i] = 0;
        return i + 1 < capacity;
    } __except (EXCEPTION_EXECUTE_HANDLER) { InvalidateMemoryQueries();output[0]=0;return false; }
}
std::string StringAt(const char* text) {
    char buffer[256]{};
    return CopyString(text, buffer, sizeof(buffer)) ? std::string(buffer) : std::string();
}
void* Create(HMODULE module, const char* name) {
    using Factory = void*(__cdecl*)(const char*, int*);
    auto factory = module ? reinterpret_cast<Factory>(GetProcAddress(module, "CreateInterface")) : nullptr;
    if (!factory || !Executable(reinterpret_cast<void*>(factory))) return nullptr;
    __try { return factory(name, nullptr); }
    __except(EXCEPTION_EXECUTE_HANDLER) { InvalidateMemoryQueries();return nullptr; }
}

// Exact x86 data table layouts from the Source public SDK. Numeric property
// offsets are discovered from the live RecvTables, never from a server dump.
struct RecvProp {
    const char* name;
    int type, flags, stringCapacity;
    bool insideArray;
    char padding[3];
    void* extra;
    void* arrayProp;
    void* arrayLengthProxy;
    void* proxy;
    void* dataTableProxy;
    void* dataTable;
    int offset, stride, elements;
    const char* parentArrayName;
};
struct RecvTable { void* props; int count; void* decoder; const char* name; };
struct ClientClass { void* create; void* createEvent; const char* name; void* table; void* next; int id; };
static_assert(sizeof(RecvProp)==60 && offsetof(RecvProp, offset)==44, "x86 RecvProp ABI changed");

struct Field {
    int offset=-1, stride=0, elements=0;
    bool Valid() const { return offset>=0 && offset<0x20000; }
};
Field FindField(void* tablePointer, const char* target, int base=0, int depth=0) {
    if (!tablePointer || depth>16) return {};
    RecvTable table{};
    if (!Read(tablePointer, table) || table.count<0 || table.count>4096 || !Readable(table.props, sizeof(RecvProp)*table.count)) return {};
    for (int i=0; i<table.count; ++i) {
        RecvProp prop{};
        if (!Read(Add(table.props, i*sizeof(RecvProp)), prop) || prop.offset<0 || prop.offset>0x20000) continue;
        if (StringAt(prop.name)==target) {
            Field result{base+prop.offset, prop.stride, prop.elements};
            RecvTable children{};
            // RecvPropArray3 exposes numeric children (000,001,...). Derive
            // the actual in-memory element stride, including one-byte bools.
            if (prop.dataTable && Read(prop.dataTable, children) && children.count>1 && children.count<=4096) {
                RecvProp first{}, second{};
                if (Read(children.props, first) && Read(Add(children.props,sizeof(RecvProp)),second) &&
                    StringAt(first.name)=="000" && StringAt(second.name)=="001" &&
                    second.offset>first.offset && second.offset-first.offset<=16) {
                    result.offset += first.offset;
                    result.stride = second.offset-first.offset;
                    result.elements = children.count;
                }
            }
            return result;
        }
    }
    for (int i=0; i<table.count; ++i) {
        RecvProp prop{};
        if (!Read(Add(table.props,i*sizeof(RecvProp)),prop) || !prop.dataTable || prop.offset<0 || prop.offset>0x20000) continue;
        auto result = FindField(prop.dataTable,target,base+prop.offset,depth+1);
        if (result.Valid()) return result;
    }
    return {};
}
template<class T> bool Scalar(void* base, const Field& field, T& value) {
    return base && field.Valid() && Read(Add(base,field.offset),value);
}
template<class T> bool Array(void* base, const Field& field, int index, T& value) {
    if (!base || !field.Valid() || index<0 || index>kMaxPlayers) return false;
    if (field.elements>0 && index>=field.elements) return false;
    int stride=field.stride>0 ? field.stride : static_cast<int>(sizeof(T));
    if (stride<static_cast<int>(sizeof(T)) || stride>16) return false;
    return Read(Add(base,field.offset+index*stride),value);
}
struct ResourceFields {
    Field connected, team, alive, health, maxHealth, incap, grenade, medical, pills;
    explicit ResourceFields(void* table=nullptr) {
        connected=FindField(table,"m_bConnected"); team=FindField(table,"m_iTeam");
        alive=FindField(table,"m_bAlive"); health=FindField(table,"m_iHealth");
        maxHealth=FindField(table,"m_maxHealth"); incap=FindField(table,"m_isIncapacitated");
        grenade=FindField(table,"m_grenade"); medical=FindField(table,"m_firstAidSlot"); pills=FindField(table,"m_pillsSlot");
    }
    bool Valid() const { return connected.Valid()&&team.Valid()&&alive.Valid()&&health.Valid()&&maxHealth.Valid()&&incap.Valid(); }
    std::string Missing() const {
        const Field* fields[]={&connected,&team,&alive,&health,&maxHealth,&incap};
        const char* names[]={"m_bConnected","m_iTeam","m_bAlive","m_iHealth","m_maxHealth","m_isIncapacitated"};
        std::string result;
        for (int i=0;i<6;++i) if (!fields[i]->Valid()) {
            if (!result.empty()) result+=", ";
            result+=names[i];
        }
        return result;
    }
};
struct PlayerFields {
    Field character, team, health, hanging, thirdStrike, observer, observerMode, tempBuffer;
    explicit PlayerFields(void* table=nullptr) {
        character=FindField(table,"m_survivorCharacter"); team=FindField(table,"m_iTeamNum");
        health=FindField(table,"m_iHealth");
        hanging=FindField(table,"m_isHangingFromLedge"); thirdStrike=FindField(table,"m_bIsOnThirdStrike");
        observer=FindField(table,"m_hObserverTarget"); observerMode=FindField(table,"m_iObserverMode");
        tempBuffer=FindField(table,"m_healthBuffer");
    }
};
bool ReadSurvivorHealth(void* base,const ClientClass& klass,const PlayerFields& fields,
                        int& permanent,int& total) {
    if (!base || !klass.table || !fields.character.Valid() || !fields.team.Valid() ||
        !fields.health.Valid() || !fields.tempBuffer.Valid()) return false;
    const auto name=StringAt(klass.name);
    if (name!="CTerrorPlayer" && name!="SurvivorBot") return false;
    // Verified for the pinned client.dll: networkable slot 10 returns the full
    // entity base. Slots 116/117 return permanent/total integer HP respectively.
    // The latter applies curtime, buffer time and pain_pills_decay_rate itself,
    // then truncates the positive total exactly as the game's HUD does.
    int currentPermanent=0,currentTotal=0;
    if (!Call(base,0x1d0/4,currentPermanent) || !Call(base,0x1d4/4,currentTotal) ||
        currentPermanent<0 || currentPermanent>100000 || currentTotal<currentPermanent || currentTotal>100000)
        return false;
    permanent=currentPermanent;total=currentTotal;return true;
}
struct SafePanel {
    std::uint32_t raw=0,handle=0xffffffffu;
};
struct NativePanelCache {
    void* panelInterface=nullptr;
    void* vguiInterface=nullptr;
    std::uint32_t root=0;
    int width=0,height=0;
    DWORD nextScan=0;
    bool valid=false;
    SafePanel rootRef;
    std::vector<std::uint32_t> directChildren,panels;
    std::vector<SafePanel> panelRefs;
};
struct VisualPanelCache {
    void* panelInterface=nullptr;
    void* vguiInterface=nullptr;
    DWORD nextScan=0;
    bool valid=false;
    SafePanel ownerRef;
    std::vector<std::uint32_t> directChildren,backgrounds;
    std::vector<SafePanel> backgroundRefs;
};
enum class SelfDiscoveryPhase {Idle,Tree,Verified,Roots,Named,Commit};
struct SelfDiscoveryNode {
    SafePanel panel;
    bool inspected=false;
    int childCount=0,nextChild=0;
};
struct SelfDiscovery {
    void* panelInterface=nullptr;
    void* vguiInterface=nullptr;
    std::uint32_t root=0,teamRoot=0;
    SafePanel rootRef;
    int width=0,height=0,viewIndex=0;
    bool keyed=false,pending=false;
    SelfDiscoveryPhase phase=SelfDiscoveryPhase::Idle;
    std::vector<SelfDiscoveryNode> queue;
    std::unordered_map<std::uint32_t,std::uint32_t> seen;
    std::vector<SafePanel> verifiedCandidates,localRoots,namedCandidates;
    std::size_t cursor=0,finalCursor=0,visited=0;
    int verifiedCount=0,rootCount=0,namedCount=0;
    SafePanel verifiedPanel,localRoot,namedPanel,selection;
    bool selectionNamed=false;
    LONGLONG frequency=0;
    const char* lastResult="not_started";
};
struct HudThemeSource {
    SafePanel panel;
    std::uint32_t scheme=0;
    int playerIndex=0;
    std::size_t state=0;
    std::array<int,3> items{};
};
struct HudThemeObservation {
    SafePanel panel;
    std::size_t state=0;
    GameHudCardStyle accepted,pending;
    unsigned confirmations=0;
    DWORD pendingSince=0;
};
struct HudThemeCache {
    void* panelInterface=nullptr;
    void* vguiInterface=nullptr;
    void* surfaceInterface=nullptr;
    HMODULE clientModule=nullptr,vguiModule=nullptr;
    SafePanel root;
    int width=0,height=0;
    DWORD nextCapture=0;
    DWORD noSourcesSince=0;
    bool noSources=false,retryPending=false;
    std::vector<HudThemeSource> sources;
    std::vector<SafePanel> generationSources;
    std::vector<HudThemeObservation> observations;
    std::vector<std::uint32_t> schemes;
    std::array<DWORD,4> failureSince{};
    std::array<bool,4> failed{};
    GameHudTheme theme;
};
constexpr int kWeaponInfoIdCount=56;
struct CachedGameItemIcon {
    ThemeIcon icon;
    DWORD nextLookup=0;
    bool attempted=false,ready=false;
};
// These addresses are comparison keys only. Every access starts from the
// current entity again; no model/header/registry node is dereferenced later.
struct ModelPortraitIdentity {
    std::uintptr_t model=0,header=0;
    std::array<char,260> modelName{};
    std::uint32_t checksum=0,modelId=0;
    int length=0,extension=0;
    unsigned char flag=0;
    bool operator==(const ModelPortraitIdentity& other) const {
        return model==other.model && header==other.header && modelName==other.modelName && checksum==other.checksum &&
               modelId==other.modelId && length==other.length && extension==other.extension && flag==other.flag;
    }
};
struct CachedModelPortrait {
    ModelPortraitIdentity identity;
    ModelPortrait portrait;
    bool used=false;
};
struct ModelPortraitCache {
    HMODULE neko=nullptr,client=nullptr;
    void* surface=nullptr;
    bool enabled=false;
    std::vector<CachedModelPortrait> models;
};
// Kept outside State so cache/map resets cannot recycle a renderer cache key.
std::uint64_t g_modelPortraitRevision=0;
struct State {
    void* engine=nullptr;
    void* entities=nullptr;
    void* panel=nullptr;
    void* vgui=nullptr;
    void* surface=nullptr;
    HMODULE neko=nullptr;
    HMODULE client=nullptr,vguiModule=nullptr;
    bool ready=false;
    int resourceIndex=-1;
    void* resourceTable=nullptr;
    ResourceFields resourceFields;
    std::unordered_map<void*,PlayerFields> playerFields;
    DWORD nextResourceScan=0, nextSelfScan=0;
    std::uint32_t selfPanel=0;
    SafePanel selfRef;
    bool selfPanelIsNamed=false;
    SelfDiscovery selfDiscovery;
    NativePanelCache nativePanels;
    std::unordered_map<std::uint32_t,VisualPanelCache> visualPanels;
    HudThemeCache gameTheme;
    std::array<CachedGameItemIcon,kWeaponInfoIdCount> itemIcons;
    ModelPortraitCache modelPortraits;
    std::string status="Not initialized";
    std::string resourceError="CTerrorPlayerResource scan pending";
} g;

void ResetPanelCaches() {
    g.nativePanels={};
    g.visualPanels.clear();
    g.gameTheme={};
    g.itemIcons={};
    g.modelPortraits={};
    g.selfDiscovery={};g.selfPanel=0;g.selfRef={};g.selfPanelIsNamed=false;g.nextSelfScan=0;
}

// IVGui008's serial-numbered HPanel is the lifetime token. Convert only a
// panel obtained from the current root/GetChild call, never an expired hint.
bool CaptureSafePanel(std::uint32_t raw,SafePanel& saved) {
    saved={};
    std::uint32_t handle=0xffffffffu,current=0;
    if (!raw || !g.vgui || !Call(g.vgui,15,handle,raw) || handle==0xffffffffu ||
        !Call(g.vgui,16,current,handle) || current!=raw) return false;
    saved={raw,handle};return true;
}
std::uint32_t ResolveSafePanel(const SafePanel& saved) {
    std::uint32_t current=0;
    if (saved.raw && saved.handle!=0xffffffffu && g.vgui &&
        Call(g.vgui,16,current,saved.handle) && current==saved.raw) return current;
    if (saved.raw) ++g_runtimeCounters.expiredPanelRefs;
    return 0;
}

std::string PanelString(std::uint32_t panel,int slot) {
    TraceRuntimePanel(panel,slot);
    const char* text=nullptr;
    return Call(g.panel,slot,text,panel) ? StringAt(text) : std::string();
}
bool Bounds(std::uint32_t panel,Rect& bounds) {
    if (!CallVoid(g.panel,pi::absPos,panel,&bounds.x,&bounds.y) ||
        !CallVoid(g.panel,pi::size,panel,&bounds.w,&bounds.h)) return false;
    return bounds.w>0 && bounds.h>0 && bounds.w<32768 && bounds.h<32768 &&
        bounds.x>-32768 && bounds.y>-32768 && bounds.x<32768 && bounds.y<32768;
}
bool ClippedBounds(std::uint32_t panel,const Rect& bounds,Rect& clipped) {
    int left=0,top=0,right=0,bottom=0,width=0,height=0;
    if (!CallVoid(g.engine,5,&width,&height) || width<=0 || height<=0 ||
        !CallVoid(g.panel,pi::clipRect,panel,&left,&top,&right,&bottom)) return false;
    left=(std::max)(left,(std::max)(bounds.x,0));
    top=(std::max)(top,(std::max)(bounds.y,0));
    right=(std::min)(right,(std::min)(bounds.x+bounds.w,width));
    bottom=(std::min)(bottom,(std::min)(bounds.y+bounds.h,height));
    if (right<=left || bottom<=top) return false;
    clipped={left,top,right-left,bottom-top};
    return true;
}
bool Visible(std::uint32_t panel) {
    bool visible=false;
    if (!panel || !Call(g.panel,pi::fullyVisible,visible,panel) || !visible) return false;
    std::uint32_t node=panel;
    for (int depth=0; node && depth<64; ++depth) {
        if (!Call(g.panel,pi::visible,visible,node) || !visible) return false;
        std::uint32_t parent=0;
        if (!Call(g.panel,pi::parent,parent,node) || parent==node) return false;
        node=parent;
    }
    if (node) return false;
    Rect bounds{},clipped{};
    return Bounds(panel,bounds) && ClippedBounds(panel,bounds,clipped);
}
void Traverse(std::uint32_t root,std::vector<std::uint32_t>& result,int limit) {
    std::vector<std::uint32_t> queue;
    std::unordered_set<std::uint32_t> seen;
    if (root) queue.push_back(root);
    for (std::size_t n=0;n<queue.size() && result.size()<static_cast<std::size_t>(limit);++n) {
        const auto node=queue[n];
        if (!node || !seen.insert(node).second) continue;
        result.push_back(node);
        ++g_runtimeCounters.traversedNodes;
        int count=0;
        if (!Call(g.panel,pi::childCount,count,node) || count<0 || count>4096) continue;
        for (int i=0;i<count && queue.size()<static_cast<std::size_t>(limit*2);++i) {
            std::uint32_t child=0;
            if (Call(g.panel,pi::child,child,node,i) && child) queue.push_back(child);
        }
    }
}
bool TraverseSafe(const SafePanel& root,std::vector<SafePanel>& result,std::size_t limit,std::size_t edgeLimit=0) {
    if (!root.raw || !limit) return false;
    std::vector<SafePanel> queue{root};
    std::unordered_map<std::uint32_t,std::uint32_t> seen{{root.raw,root.handle}};
    std::size_t edges=0;
    for (std::size_t n=0;n<queue.size();++n) {
        const auto node=ResolveSafePanel(queue[n]);
        if (!node) return false;
        result.push_back(queue[n]);++g_runtimeCounters.traversedNodes;
        int count=0;
        if (!Call(g.panel,pi::childCount,count,node) || count<0 || count>4096) return false;
        for (int i=0;i<count;++i) {
            if (edgeLimit && edges++>=edgeLimit) return false;
            std::uint32_t child=0;
            if (!Call(g.panel,pi::child,child,node,i)) return false;
            if (!child) continue;
            SafePanel saved;
            if (!CaptureSafePanel(child,saved)) return false;
            const auto prior=seen.find(child);
            if (prior!=seen.end()) {
                if (prior->second!=saved.handle) return false;
                continue;
            }
            if (queue.size()>=limit) return false;
            seen.emplace(child,saved.handle);queue.push_back(saved);
        }
    }
    return result.size()<limit;
}
bool CaptureDirectChildren(std::uint32_t panel,std::vector<std::uint32_t>& children) {
    children.clear();
    int count=0;
    if (!Call(g.panel,pi::childCount,count,panel) || count<0 || count>512) return false;
    children.reserve(static_cast<std::size_t>(count));
    for (int i=0;i<count;++i) {
        std::uint32_t child=0;
        if (!Call(g.panel,pi::child,child,panel,i)) return false;
        children.push_back(child);
    }
    return true;
}
bool DirectChildrenMatch(std::uint32_t panel,const std::vector<std::uint32_t>& children) {
    int count=0;
    if (!Call(g.panel,pi::childCount,count,panel) || count<0 ||
        static_cast<std::size_t>(count)!=children.size()) return false;
    for (int i=0;i<count;++i) {
        std::uint32_t child=0;
        if (!Call(g.panel,pi::child,child,panel,i) || child!=children[static_cast<std::size_t>(i)]) return false;
    }
    return true;
}
bool DescendantOf(std::uint32_t panel,std::uint32_t root) {
    for (int depth=0;panel && depth<64;++depth) {
        if (panel==root) return true;
        std::uint32_t parent=0;
        if (!Call(g.panel,pi::parent,parent,panel) || parent==panel) return false;
        panel=parent;
    }
    return false;
}
const std::vector<std::uint32_t>& NativePanelCandidates(std::uint32_t root,int width,int height,DWORD now) {
    auto& cache=g.nativePanels;
    bool rediscover=!cache.valid || cache.panelInterface!=g.panel || cache.vguiInterface!=g.vgui || cache.root!=root ||
        cache.width!=width || cache.height!=height || static_cast<LONG>(now-cache.nextScan)>=0 ||
        !ResolveSafePanel(cache.rootRef) || cache.panelRefs.size()!=cache.panels.size();
    if (!rediscover && !DirectChildrenMatch(root,cache.directChildren)) rediscover=true;
    if (!rediscover) for (const auto& saved:cache.panelRefs) {
        const auto panel=ResolveSafePanel(saved);
        if (!panel || PanelString(panel,pi::className)!="TeamDisplayPlayer" || !DescendantOf(panel,root)) {
            rediscover=true;
            break;
        }
    }
    if (rediscover) {
        // VPANEL handles are only discovery hints. Never retain a ClientPanel
        // pointer; current class, ancestry, target and bounds are revalidated.
        cache={};
        cache.panelInterface=g.panel;cache.vguiInterface=g.vgui;cache.root=root;cache.width=width;cache.height=height;
        cache.nextScan=now+500;
        g.visualPanels.clear();
        ++g_runtimeCounters.nativeDiscoveries;
        std::vector<SafePanel> nodes;
        if (!CaptureSafePanel(root,cache.rootRef) || !TraverseSafe(cache.rootRef,nodes,512)) return cache.panels;
        for (const auto& saved:nodes) {
            const auto panel=ResolveSafePanel(saved);
            if (!panel) {cache.panels.clear();cache.panelRefs.clear();return cache.panels;}
            if (PanelString(panel,pi::className)=="TeamDisplayPlayer" && DescendantOf(panel,root)) {
                cache.panels.push_back(panel);cache.panelRefs.push_back(saved);
            }
        }
        cache.valid=CaptureDirectChildren(root,cache.directChildren);
        if (!cache.valid) {cache.panels.clear();cache.panelRefs.clear();}
    }
    return cache.panels;
}
bool Contains(const Rect& outer,const Rect& inner) {
    return outer.w>0 && outer.h>0 && inner.w>0 && inner.h>0 &&
        inner.x>=outer.x && inner.y>=outer.y &&
        static_cast<long long>(inner.x)+inner.w<=static_cast<long long>(outer.x)+outer.w &&
        static_cast<long long>(inner.y)+inner.h<=static_cast<long long>(outer.y)+outer.h;
}
bool OwnedByTeamPanel(std::uint32_t panel,std::uint32_t teamPanel) {
    for (int depth=0;panel && depth<64;++depth) {
        if (panel==teamPanel) return true;
        if (PanelString(panel,pi::className)=="TeamDisplayPlayer") return false;
        std::uint32_t parent=0;
        if (!Call(g.panel,pi::parent,parent,panel) || parent==panel) return false;
        panel=parent;
    }
    return false;
}
bool VisualCardBounds(std::uint32_t panel,const Rect& container,Rect& bounds) {
    ++g_runtimeCounters.visualDiscoveries;
    std::vector<std::uint32_t> children;
    Traverse(panel,children,128);
    if (children.size()>=128) return false;
    std::uint32_t found=0;
    Rect candidate{};
    for (auto child:children) {
        if (child==panel || PanelString(child,pi::name)!="BackgroundImage" ||
            !OwnedByTeamPanel(child,panel) || !Visible(child)) continue;
        Rect raw{},clipped{};
        if (!Bounds(child,raw) || !ClippedBounds(child,raw,clipped) ||
            clipped.w<24 || clipped.h<16 || !Contains(container,clipped)) continue;
        // More than one visible background is ambiguous (for example during
        // a state transition). Keep the conservative container in that case.
        if (found) return false;
        found=child;
        candidate=clipped;
    }
    if (!found) return false;
    bounds=candidate;
    return true;
}
void DiscoverVisualPanels(std::uint32_t panel,VisualPanelCache& cache,DWORD now) {
    cache={};
    cache.panelInterface=g.panel;cache.vguiInterface=g.vgui;cache.nextScan=now+500;
    ++g_runtimeCounters.visualDiscoveries;
    std::vector<SafePanel> nodes;
    if (!CaptureSafePanel(panel,cache.ownerRef) || !TraverseSafe(cache.ownerRef,nodes,128)) return;
    for (const auto& saved:nodes) {
        const auto child=ResolveSafePanel(saved);
        if (!child) return;
        if (child!=panel && PanelString(child,pi::name)=="BackgroundImage" && OwnedByTeamPanel(child,panel)) {
            cache.backgrounds.push_back(child);cache.backgroundRefs.push_back(saved);
        }
    }
    cache.valid=CaptureDirectChildren(panel,cache.directChildren);
}
bool CachedVisualCardBounds(std::uint32_t panel,const Rect& container,Rect& bounds,DWORD now) {
    auto& cache=g.visualPanels[panel];
    bool rediscover=!cache.valid || cache.panelInterface!=g.panel || cache.vguiInterface!=g.vgui ||
        static_cast<LONG>(now-cache.nextScan)>=0 || !ResolveSafePanel(cache.ownerRef) ||
        cache.backgroundRefs.size()!=cache.backgrounds.size() || !DirectChildrenMatch(panel,cache.directChildren);
    if (!rediscover) for (const auto& saved:cache.backgroundRefs) {
        const auto child=ResolveSafePanel(saved);
        if (!child || PanelString(child,pi::name)!="BackgroundImage" || !OwnedByTeamPanel(child,panel)) {
            rediscover=true;
            break;
        }
    }
    if (rediscover) DiscoverVisualPanels(panel,cache,now);
    if (!cache.valid) return false;
    bool found=false;
    Rect candidate{};
    // Hidden candidates stay in the list, so normal/incap visibility changes
    // take effect immediately without another tree discovery.
    for (const auto& saved:cache.backgroundRefs) {
        const auto child=ResolveSafePanel(saved);
        if (!child) {cache.valid=false;return false;}
        if (!Visible(child)) continue;
        Rect raw{},clipped{};
        if (!Bounds(child,raw) || !ClippedBounds(child,raw,clipped) ||
            clipped.w<24 || clipped.h<16 || !Contains(container,clipped)) continue;
        if (found) return false;
        found=true;candidate=clipped;
    }
    if (!found) return false;
    bounds=candidate;
    return true;
}
bool IsLocalHudRoot(std::uint32_t panel) {
    return PanelString(panel,pi::className)=="CHudLocalPlayerDisplay" ||
           PanelString(panel,pi::name)=="CHudLocalPlayerDisplay";
}
bool LocalHudAncestor(std::uint32_t panel,std::uint32_t& root) {
    root=0;
    for (int depth=0;panel && depth<64;++depth) {
        if (PanelString(panel,pi::className)=="TeamDisplayPlayer") return false;
        if (IsLocalHudRoot(panel)) {
            if (root) return false;
            root=panel;
        }
        std::uint32_t parent=0;
        if (!Call(g.panel,pi::parent,parent,panel) || parent==panel) return false;
        panel=parent;
    }
    return panel==0 && root!=0;
}
bool NamedSelfPanelBounds(std::uint32_t panel,Rect& bounds,std::uint32_t* owner=nullptr) {
    std::uint32_t root=0;
    if (!panel || PanelString(panel,pi::name)!="LocalPlayer" || !Visible(panel) ||
        !LocalHudAncestor(panel,root)) return false;
    Rect raw{};
    if (!Bounds(panel,raw) || !ClippedBounds(panel,raw,bounds)) return false;
    if (owner) *owner=root;
    return true;
}
bool FindNamedSelfPanel(const std::vector<std::uint32_t>& panels,std::uint32_t& found,Rect& bounds) {
    found=0;
    if (panels.size()>=4096) return false;
    std::uint32_t root=0;
    for (auto panel:panels) if (IsLocalHudRoot(panel) && Visible(panel)) {
        if (root && root!=panel) return false;
        root=panel;
    }
    if (!root) return false;
    Rect candidate{};
    for (auto panel:panels) {
        std::uint32_t owner=0;
        Rect clipped{};
        if (!NamedSelfPanelBounds(panel,clipped,&owner) || owner!=root) continue;
        if (found && found!=panel) { found=0; return false; }
        found=panel;
        candidate=clipped;
    }
    if (!found) return false;
    bounds=candidate;
    return true;
}
void* ClientPanel(std::uint32_t panel) {
    void* object=nullptr;
    if (!Call(g.panel,pi::getPanel,object,panel,"ClientDLL") || !Readable(object,sizeof(void*))) return nullptr;
    return object;
}
bool EntityInfo(int index, void*& base, ClientClass& klass) {
    void* networkable=nullptr;
    void* classPointer=nullptr;
    base=nullptr;
    if (!Call(g.entities,0,networkable,index) || !networkable ||
        !Call(networkable,ni::clientClass,classPointer) || !Read(classPointer,klass) ||
        !klass.table || !Call(networkable,ni::dataTableBase,base) || !Readable(base,4)) return false;
    return true;
}
const void* ModelAddress(const void* base,std::size_t offset,std::size_t size) {
    const auto start=reinterpret_cast<std::uintptr_t>(base),limit=~std::uintptr_t(0);
    if (!start || !size || offset>limit-start || size-1>limit-(start+offset)) return nullptr;
    return reinterpret_cast<const void*>(start+offset);
}
template<class T> bool ModelRead(const void* base,std::size_t offset,T& value) {
    return Read(ModelAddress(base,offset,sizeof(value)),value);
}
int ReadSurvivorSet() {
    // The plugin hash-gates client.dll before initialization. Also constrain
    // this fixed RVA to its current PE32 image before the single value read.
    constexpr std::size_t offset=0x7c46f0;
    IMAGE_DOS_HEADER dos{};
    if (!Read(g.client,dos) || dos.e_magic!=IMAGE_DOS_SIGNATURE ||
        dos.e_lfanew<static_cast<LONG>(sizeof(dos)) || dos.e_lfanew>1024*1024) return 0;
    IMAGE_NT_HEADERS32 nt{};
    if (!ModelRead(g.client,static_cast<std::size_t>(dos.e_lfanew),nt) ||
        nt.Signature!=IMAGE_NT_SIGNATURE || nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_I386 ||
        nt.FileHeader.SizeOfOptionalHeader<sizeof(IMAGE_OPTIONAL_HEADER32) ||
        nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        nt.OptionalHeader.SizeOfImage<offset+sizeof(int) || nt.OptionalHeader.SizeOfImage>1024u*1024u*1024u ||
        static_cast<std::size_t>(dos.e_lfanew)>nt.OptionalHeader.SizeOfImage-sizeof(nt)) return 0;
    int set=0;
    if (!ModelRead(g.client,offset,set) || (set!=1 && set!=2)) return 0;
    return set;
}
bool ReadModelPortraitIdentity(void* entity,ModelPortraitIdentity& identity) {
    void* model=nullptr;void* wrapper=nullptr;void* header=nullptr;
    std::uint32_t prefix[3]{};int length=0,extension=0;
    std::array<char,260> name{};
    if (!ModelRead(entity,0x60,model) || !model || !ModelRead(entity,0x934,wrapper) ||
        !CopyString(static_cast<const char*>(ModelAddress(model,4,name.size())),name.data(),name.size()) ||
        !Read(wrapper,header) || !Read(header,prefix) || prefix[0]!=0x54534449u || prefix[1]!=49 ||
        !ModelRead(header,76,length) || !ModelRead(header,0x190,extension) ||
        length<0x194 || length>32*1024*1024 || extension<0x194 || extension>length-0x50-12) return false;
    unsigned char metadata[12]{};
    if (!Read(ModelAddress(header,static_cast<std::size_t>(extension)+0x50,sizeof(metadata)),metadata)) return false;
    void* currentModel=nullptr;void* currentWrapper=nullptr;void* currentHeader=nullptr;
    if (!ModelRead(entity,0x60,currentModel) || !ModelRead(entity,0x934,currentWrapper) ||
        currentModel!=model || currentWrapper!=wrapper || !Read(currentWrapper,currentHeader) || currentHeader!=header) return false;
    identity={};identity.model=reinterpret_cast<std::uintptr_t>(model);
    identity.header=reinterpret_cast<std::uintptr_t>(header);identity.modelName=name;identity.checksum=prefix[2];
    identity.length=length;identity.extension=extension;identity.flag=metadata[1];
    std::memcpy(&identity.modelId,metadata+8,sizeof(identity.modelId));
    return true;
}
bool ModelPortraitMaterials(const char* path,std::string& normal,std::string& incap) {
    std::string name(path);
    for (auto& c:name) {
        if (c=='\\') c='/';
        else if (c>='A' && c<='Z') c=static_cast<char>(c-'A'+'a');
    }
    constexpr const char* prefix="models/l4n/s/";
    constexpr std::size_t prefixLength=13;
    if (name.compare(0,prefixLength,prefix)!=0 || name.size()<prefixLength+6 ||
        name.compare(name.size()-4,4,".mdl")!=0) return false;
    const auto slash=name.find('/',prefixLength);
    if (slash==std::string::npos || slash==prefixLength || slash-prefixLength>128 || slash+1>=name.size()-4) return false;
    const auto pack=name.substr(prefixLength,slash-prefixLength);
    if (pack=="." || pack==".." || std::any_of(pack.begin(),pack.end(),[](unsigned char c) {
            return c<32 || c==127 || c==':' || c=='\\';
        })) return false;
    normal="vgui/l4n_s/"+pack;incap=normal+"_incap";return true;
}
bool ModelPortraitMaterials(const void* model,std::string& normal,std::string& incap) {
    char path[260]{};
    return CopyString(static_cast<const char*>(ModelAddress(model,4,sizeof(path))),path,sizeof(path)) &&
           ModelPortraitMaterials(path,normal,incap);
}
struct ModelPortraitRegistryHeader {
    std::uint32_t loadFactor=0,sentinel=0,size=0,buckets=0,end=0,capacity=0,mask=0,bucketCount=0;
};
std::uint32_t ModelPortraitHash(std::uint32_t id) {
    std::uint32_t hash=2166136261u;
    for (int i=0;i<4;++i) {hash^=(id>>(i*8))&0xffu;hash*=16777619u;}
    return hash;
}
bool LookupModelPortrait(std::uint32_t id,int& normal,int& incap) {
    ++g_runtimeCounters.modelPortraitLookups;
    normal=incap=0;
    ModelPortraitRegistryHeader before{},after{};
    const auto* registry=ModelAddress(g.neko,0x20c810,sizeof(before));
    if (!Read(registry,before) || !before.sentinel || before.size>4096 || !before.buckets ||
        before.mask>65535 || before.bucketCount!=before.mask+1 ||
        (before.bucketCount&(before.bucketCount-1)) || before.end<before.buckets || before.capacity<before.end ||
        before.end-before.buckets!=before.bucketCount*8u) return false;
    std::uint32_t bucket[2]{},currentBucket[2]{};
    const auto bucketOffset=static_cast<std::size_t>(ModelPortraitHash(id)&before.mask)*8;
    const auto* bucketAddress=ModelAddress(reinterpret_cast<void*>(before.buckets),bucketOffset,sizeof(bucket));
    if (!Read(bucketAddress,bucket) || (bucket[0]==before.sentinel)!=(bucket[1]==before.sentinel)) return false;
    std::array<std::uint32_t,64> seen{};std::size_t count=0;
    auto node=bucket[1];
    while (node!=before.sentinel && count<seen.size() && count<before.size) {
        if (!node || std::find(seen.begin(),seen.begin()+count,node)!=seen.begin()+count) return false;
        seen[count++]=node;
        std::uint32_t key=0,previous=0;
        const auto* nodeAddress=reinterpret_cast<void*>(node);
        if (!ModelRead(nodeAddress,8,key)) return false;
        if (key==id) {
            int textures[2]{},currentTextures[2]{};std::uint32_t currentKey=0;
            if (!Read(ModelAddress(nodeAddress,0x10+0x28,sizeof(textures)),textures) ||
                !ModelRead(nodeAddress,8,currentKey) || currentKey!=id ||
                !Read(ModelAddress(nodeAddress,0x10+0x28,sizeof(currentTextures)),currentTextures) ||
                std::memcmp(textures,currentTextures,sizeof(textures)) ||
                !Read(bucketAddress,currentBucket) || std::memcmp(bucket,currentBucket,sizeof(bucket)) ||
                !Read(registry,after) || std::memcmp(&before,&after,sizeof(before))) return false;
            normal=(std::max)(0,textures[0]);incap=(std::max)(0,textures[1]);return true;
        }
        if (node==bucket[0]) return false;
        if (!ModelRead(nodeAddress,4,previous) || previous==before.sentinel) return false;
        node=previous;
    }
    return false;
}
bool BeginModelPortraitPass() {
    auto& cache=g.modelPortraits;
    if (cache.neko!=g.neko || cache.client!=g.client || cache.surface!=g.surface) cache={};
    cache.neko=g.neko;cache.client=g.client;cache.surface=g.surface;
    cache.enabled=g.neko && g.client && g.surface;
    for (auto& entry:cache.models) entry.used=false;
    return cache.enabled;
}
ModelPortrait ReadModelPortrait(void* base,const ClientClass& klass,const PlayerFields& fields) {
    auto& cache=g.modelPortraits;
    if (!cache.enabled || !base || !klass.table || !fields.character.Valid() || !fields.team.Valid()) return {};
    const auto className=StringAt(klass.name);
    if (className!="CTerrorPlayer" && className!="SurvivorBot") return {};
    ModelPortraitIdentity identity;
    if (!ReadModelPortraitIdentity(base,identity)) return {};
    for (auto& entry:cache.models) if (entry.identity==identity) {
        entry.used=true;++g_runtimeCounters.modelPortraitCacheHits;return entry.portrait;
    }
    ++g_runtimeCounters.modelPortraitInspections;
    if (cache.models.size()>=64) return {};
    CachedModelPortrait entry;entry.identity=identity;entry.used=true;
    if (identity.flag && ModelPortraitMaterials(identity.modelName.data(),
                                               entry.portrait.normalMaterial,entry.portrait.incapMaterial)) {
        entry.portrait.modelId=identity.modelId;
        LookupModelPortrait(identity.modelId,entry.portrait.normalId,entry.portrait.incapId);
        ModelPortraitIdentity current;
        if (!ReadModelPortraitIdentity(base,current) || !(current==identity)) return {};
        if (!++g_modelPortraitRevision) ++g_modelPortraitRevision;
        entry.portrait.revision=g_modelPortraitRevision;
    }
    cache.models.push_back(entry);return entry.portrait;
}
void FinishModelPortraitPass() {
    auto& models=g.modelPortraits.models;
    models.erase(std::remove_if(models.begin(),models.end(),[](const CachedModelPortrait& entry) {
        return !entry.used;
    }),models.end());
}
bool ResourceFieldsReady() {
    if (g.resourceFields.Valid()) { g.resourceError.clear(); return true; }
    g.resourceError="CTerrorPlayerResource required RecvTable fields missing: "+g.resourceFields.Missing();
    return false;
}
bool Resource(void*& base) {
    ClientClass klass{};
    if (g.resourceIndex>=0 && EntityInfo(g.resourceIndex,base,klass) && StringAt(klass.name)=="CTerrorPlayerResource") {
        if (klass.table!=g.resourceTable) {
            g.resourceFields=ResourceFields(klass.table);
            g.resourceTable=klass.table;
        }
        return ResourceFieldsReady();
    }
    if (g.resourceIndex>=0) g.resourceError="CTerrorPlayerResource cached entity unavailable; waiting to rescan";
    g.resourceIndex=-1;
    const DWORD now=GetTickCount();
    if (static_cast<LONG>(now-g.nextResourceScan)<0) return false;
    g.nextResourceScan=now+1000;
    int highest=0;
    if (!Call(g.entities,6,highest) || highest<1 || highest>8192) {
        g.resourceError="CTerrorPlayerResource scan unavailable: invalid highest entity index";
        return false;
    }
    int readableEntities=0;
    for (int i=1;i<=highest;++i) {
        if (!EntityInfo(i,base,klass)) continue;
        ++readableEntities;
        if (StringAt(klass.name)!="CTerrorPlayerResource") continue;
        g.resourceIndex=i;
        g.resourceTable=klass.table;
        g.resourceFields=ResourceFields(klass.table);
        return ResourceFieldsReady();
    }
    g.resourceError="CTerrorPlayerResource entity not found: readable="+std::to_string(readableEntities)+
                    " highest="+std::to_string(highest);
    return false;
}
std::string PlayerName(int index) {
    // player_info_t starts with uint64 xuid, then the UTF-8 name. The generous
    // aligned buffer also covers variations in the remaining engine fields.
    alignas(16) unsigned char info[2048]{};
    bool ok=false;
    if (!Call(g.engine,8,ok,index,info) || !ok) return "Survivor " + std::to_string(index);
    char name[129]{};
    std::memcpy(name,info+8,128);
    for (char& c:name) if (static_cast<unsigned char>(c)<0x20 && c) c=' ';
    return name[0] ? std::string(name) : "Survivor " + std::to_string(index);
}
bool SelfPanel(std::uint32_t panel,int viewIndex,Rect& bounds) {
    if (!panel || PanelString(panel,pi::className)!="SurvivorStatsPanel" || !Visible(panel)) return false;
    auto parent=panel;
    for (int n=0;n<32 && parent;++n) {
        if (PanelString(parent,pi::className)=="TeamDisplayPlayer") return false;
        std::uint32_t next=0;
        if (!Call(g.panel,pi::parent,next,parent) || next==parent) return false;
        parent=next;
    }
    if (parent) return false;
    void* object=ClientPanel(panel);
    int index=0;
    Rect raw{};
    return object && Call(object,0x3b0/4,index) && index==viewIndex &&
        Bounds(panel,raw) && ClippedBounds(panel,raw,bounds);
}
void FinishSelfDiscovery(DWORD now,const char* result,bool clearCache=true,
                         SafePanel selected={},bool named=false) {
    auto& scan=g.selfDiscovery;
    scan.pending=false;scan.phase=SelfDiscoveryPhase::Idle;scan.lastResult=result;
    scan.queue.clear();scan.seen.clear();
    scan.verifiedCandidates.clear();scan.localRoots.clear();scan.namedCandidates.clear();
    if (selected.raw || clearCache) {
        g.selfPanel=selected.raw;g.selfRef=selected;g.selfPanelIsNamed=selected.raw && named;
    }
    g.nextSelfScan=now+30000;
}
bool SelfDiscoveryStep(std::uint32_t root,std::uint32_t teamRoot,int width,int height,
                       int viewIndex,DWORD now,Rect& bounds) {
    auto& scan=g.selfDiscovery;
    const bool changed=scan.keyed && (scan.panelInterface!=g.panel || scan.vguiInterface!=g.vgui ||
        scan.root!=root || scan.teamRoot!=teamRoot || scan.width!=width || scan.height!=height ||
        scan.viewIndex!=viewIndex || (scan.rootRef.raw && !ResolveSafePanel(scan.rootRef)));
    if (!scan.keyed || changed) {
        scan={};scan.keyed=true;scan.panelInterface=g.panel;scan.vguiInterface=g.vgui;
        scan.root=root;scan.teamRoot=teamRoot;
        scan.width=width;scan.height=height;scan.viewIndex=viewIndex;
        g.selfPanel=0;g.selfRef={};g.selfPanelIsNamed=false;
        if (changed) g.nextSelfScan=0;
    }
    const bool hadCached=g.selfPanel!=0;
    const auto cachedPanel=hadCached ? ResolveSafePanel(g.selfRef) : 0;
    bool cached=cachedPanel && (g.selfPanelIsNamed ? NamedSelfPanelBounds(cachedPanel,bounds) :
                                                  SelfPanel(cachedPanel,viewIndex,bounds));
    if (!cached && hadCached) {
        // A disappearing/replaced self HUD must not wait for the negative
        // retry interval. Discard an in-progress old-tree refresh as well.
        scan.pending=false;scan.phase=SelfDiscoveryPhase::Idle;
        g.selfPanel=0;g.selfRef={};g.selfPanelIsNamed=false;g.nextSelfScan=0;
    }
    // A node may span several frames. Never resume GetChild using an old
    // count after the UI has removed or inserted children in that interval.
    if (scan.pending && scan.phase==SelfDiscoveryPhase::Tree && scan.cursor<scan.queue.size()) {
        const auto& node=scan.queue[scan.cursor];
        const auto panel=ResolveSafePanel(node.panel);
        if (!panel) {FinishSelfDiscovery(now,"tree_changed",false);return cached;}
        if (node.inspected && node.nextChild<node.childCount) {
            int count=0;
            if (!Call(g.panel,pi::childCount,count,panel) || count!=node.childCount) {
                FinishSelfDiscovery(now,"tree_changed",false);return cached;
            }
        }
    }
    if (!scan.pending) {
        if (static_cast<LONG>(now-g.nextSelfScan)<0) return cached;
        if (!root || !g.panel || !g.vgui || width<=0 || height<=0) {
            FinishSelfDiscovery(now,"no_root");return false;
        }
        scan.queue.clear();scan.seen.clear();
        scan.verifiedCandidates.clear();scan.localRoots.clear();scan.namedCandidates.clear();
        scan.cursor=scan.finalCursor=scan.visited=0;
        scan.verifiedCount=scan.rootCount=scan.namedCount=0;
        scan.verifiedPanel=scan.localRoot=scan.namedPanel=scan.selection={};scan.selectionNamed=false;
        if (!CaptureSafePanel(root,scan.rootRef)) {FinishSelfDiscovery(now,"tree_changed",false);return cached;}
        scan.queue.push_back({scan.rootRef});scan.seen.emplace(root,scan.rootRef.handle);
        scan.pending=true;scan.phase=SelfDiscoveryPhase::Tree;scan.lastResult="pending";
        LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);scan.frequency=frequency.QuadPart;
        ++g_runtimeCounters.selfDiscoveries;
    }
    LARGE_INTEGER started{};QueryPerformanceCounter(&started);
    const LONGLONG budget=(std::max<LONGLONG>)(1,scan.frequency/1000);
    unsigned nodes=0,edges=0,validations=0;
    auto hasTime=[&]() {
        LARGE_INTEGER current{};QueryPerformanceCounter(&current);
        return current.QuadPart-started.QuadPart<budget;
    };
    auto collect=[&](std::vector<SafePanel>& candidates,const SafePanel& panel) {
        if (candidates.size()>=32) return false;
        candidates.push_back(panel);return true;
    };
    while (scan.pending && hasTime()) {
        if (scan.phase==SelfDiscoveryPhase::Tree) {
            if (scan.cursor==scan.queue.size()) {
                scan.phase=SelfDiscoveryPhase::Verified;scan.finalCursor=0;continue;
            }
            const auto saved=scan.queue[scan.cursor].panel;
            const auto panel=ResolveSafePanel(saved);
            if (!panel) {FinishSelfDiscovery(now,"tree_changed",false);break;}
            if (!scan.queue[scan.cursor].inspected) {
                if (nodes>=16) break;
                ++nodes;++scan.visited;++g_runtimeCounters.traversedNodes;
                const auto klass=PanelString(panel,pi::className), name=PanelString(panel,pi::name);
                if ((klass=="SurvivorStatsPanel" && !collect(scan.verifiedCandidates,saved)) ||
                    ((klass=="CHudLocalPlayerDisplay" || name=="CHudLocalPlayerDisplay") && !collect(scan.localRoots,saved)) ||
                    (name=="LocalPlayer" && !collect(scan.namedCandidates,saved))) {
                    FinishSelfDiscovery(now,"candidate_limit",false);break;
                }
                int count=0;
                if (!Call(g.panel,pi::childCount,count,panel) || count<0 || count>4096) {
                    FinishSelfDiscovery(now,"read_failed",false);break;
                }
                scan.queue[scan.cursor].inspected=true;scan.queue[scan.cursor].childCount=count;
            }
            if (scan.queue[scan.cursor].nextChild<scan.queue[scan.cursor].childCount) {
                if (edges>=64) break;
                const int childIndex=scan.queue[scan.cursor].nextChild++;
                std::uint32_t child=0;++edges;
                if (!Call(g.panel,pi::child,child,panel,childIndex)) {
                    FinishSelfDiscovery(now,"read_failed",false);break;
                }
                if (child) {
                    SafePanel childRef;
                    if (!CaptureSafePanel(child,childRef)) {FinishSelfDiscovery(now,"tree_changed",false);break;}
                    const auto inserted=scan.seen.emplace(child,childRef.handle);
                    if (!inserted.second) {
                        if (inserted.first->second!=childRef.handle) {FinishSelfDiscovery(now,"tree_changed",false);break;}
                        continue;
                    }
                    // A capped traversal is incomplete: never adopt one of
                    // its partial candidates, even if it looks plausible.
                    if (scan.seen.size()>=4096) {FinishSelfDiscovery(now,"node_limit",false);break;}
                    scan.queue.push_back({childRef});
                }
                continue;
            }
            ++scan.cursor;continue;
        }
        if (validations>=16) break;
        if (scan.phase==SelfDiscoveryPhase::Verified) {
            if (scan.finalCursor<scan.verifiedCandidates.size()) {
                ++validations;Rect candidate{};const auto saved=scan.verifiedCandidates[scan.finalCursor++];
                const auto panel=ResolveSafePanel(saved);
                if (!panel) {FinishSelfDiscovery(now,"tree_changed",false);break;}
                if (SelfPanel(panel,viewIndex,candidate)) {++scan.verifiedCount;scan.verifiedPanel=saved;}
                continue;
            }
            scan.finalCursor=0;
            if (scan.verifiedCount==1) {
                scan.selection=scan.verifiedPanel;scan.selectionNamed=false;scan.phase=SelfDiscoveryPhase::Commit;
            } else scan.phase=SelfDiscoveryPhase::Roots;
            continue;
        }
        if (scan.phase==SelfDiscoveryPhase::Roots) {
            if (scan.finalCursor<scan.localRoots.size()) {
                ++validations;const auto saved=scan.localRoots[scan.finalCursor++];
                const auto panel=ResolveSafePanel(saved);
                if (!panel) {FinishSelfDiscovery(now,"tree_changed",false);break;}
                if (IsLocalHudRoot(panel) && Visible(panel)) {++scan.rootCount;scan.localRoot=saved;}
                continue;
            }
            if (scan.rootCount!=1) {FinishSelfDiscovery(now,"not_found");break;}
            scan.finalCursor=0;scan.phase=SelfDiscoveryPhase::Named;continue;
        }
        if (scan.phase==SelfDiscoveryPhase::Named) {
            if (scan.finalCursor<scan.namedCandidates.size()) {
                ++validations;Rect candidate{};std::uint32_t owner=0;
                const auto saved=scan.namedCandidates[scan.finalCursor++];
                const auto panel=ResolveSafePanel(saved), localRoot=ResolveSafePanel(scan.localRoot);
                if (!panel || !localRoot) {FinishSelfDiscovery(now,"tree_changed",false);break;}
                if (NamedSelfPanelBounds(panel,candidate,&owner) && owner==localRoot) {
                    ++scan.namedCount;scan.namedPanel=saved;
                }
                continue;
            }
            if (scan.namedCount!=1) {FinishSelfDiscovery(now,"not_found");break;}
            scan.selection=scan.namedPanel;scan.selectionNamed=true;scan.phase=SelfDiscoveryPhase::Commit;continue;
        }
        if (scan.phase==SelfDiscoveryPhase::Commit) {
            ++validations;Rect candidate{};std::uint32_t owner=0;
            const auto panel=ResolveSafePanel(scan.selection);
            const auto localRoot=scan.selectionNamed ? ResolveSafePanel(scan.localRoot) : 0;
            if (!panel || (scan.selectionNamed && !localRoot)) {FinishSelfDiscovery(now,"tree_changed",false);break;}
            const bool valid=scan.selectionNamed ?
                (NamedSelfPanelBounds(panel,candidate,&owner) && owner==localRoot &&
                 IsLocalHudRoot(owner) && Visible(owner)) : SelfPanel(panel,viewIndex,candidate);
            if (!valid) {FinishSelfDiscovery(now,"not_found");break;}
            const auto selected=scan.selection;const bool named=scan.selectionNamed;
            FinishSelfDiscovery(now,"found",true,selected,named);bounds=candidate;cached=true;break;
        }
    }
    return g.selfPanel!=0 && cached;
}

bool ThemeObject(void* object,HMODULE module,int vtableRva) {
    void* table=nullptr;
    return object && module && Read(object,table) && table==Add(module,vtableRva);
}
bool ThemeColor(void* object,int offset,Color& color) {
    unsigned char rgba[4]{};
    if (!CopyBytes(Add(object,offset),rgba,sizeof(rgba))) return false;
    color={rgba[0],rgba[1],rgba[2],rgba[3]};return true;
}
Color ThemeAlpha(void* object,Color color) {
    float alpha=255;
    if (Read(Add(object,0x118),alpha) && std::isfinite(alpha) && alpha>=0 && alpha<=255)
        color.a=color.a*static_cast<int>(alpha)/255;
    return color;
}
struct ThemeTextureValue {
    bool valid=false;
    ThemeTexture texture;
};
struct ThemeCapturePass {
    std::unordered_map<int,ThemeTextureValue> textures;
    std::array<Color,4> palette={Color{161,25,25,255},Color{161,25,25,255},
                               Color{216,146,12,255},Color{10,177,50,255}};
    float medium=0.4f;
};
bool ThemeTextureFor(int id,ThemeCapturePass& pass,ThemeTexture& output) {
    if (id<=0) return false;
    const auto prior=pass.textures.find(id);
    if (prior!=pass.textures.end()) {
        if (prior->second.valid) output=prior->second.texture;
        return prior->second.valid;
    }
    ThemeTextureValue value;
    char material[512]{};bool found=false;int width=0,height=0;
    if (Call(g.surface,26,found,id,material,static_cast<int>(sizeof(material))) && found &&
        material[0] && material[sizeof(material)-1]==0 &&
        CallVoid(g.surface,30,id,&width,&height) && width>0 && height>0 && width<=32768 && height<=32768) {
        value.valid=true;value.texture.id=id;value.texture.material=material;
    }
    pass.textures.emplace(id,value);
    if (value.valid) output=value.texture;
    return value.valid;
}
void ThemePalette(ThemeCapturePass& pass) {
    void* parent=nullptr;int colorblind=0;
    if (g.client && Read(Add(g.client,0x7ccbec),parent) && Read(Add(parent,0x30),colorblind)) {
        const int table=colorblind==2?0x6e7f0c:0x6e7efc;
        std::array<Color,4> palette;
        bool valid=true;
        for (int n=0;n<4;++n) if (!ThemeColor(g.client,table+n*4,palette[n])) valid=false;
        if (valid) pass.palette=palette;
    }
    float limp=40;
    if (g.client && Read(Add(g.client,0x7c167c),parent) && Read(Add(parent,0x2c),limp) &&
        std::isfinite(limp) && limp>=0 && limp<=100000) pass.medium=limp/100;
}
bool ThemeWidgetFor(std::uint32_t panel,void* object,const Rect& container,int order,
                    ThemeWidget& widget,Rect& raw,Rect& visible,bool& shown,bool allowHiddenItem) {
    Rect clip;
    if (!Bounds(panel,raw)) return false;
    shown=Visible(panel);
    if (shown) {
        if (!ClippedBounds(panel,raw,clip)) return false;
    } else {
        std::uint32_t parent=0;Rect parentBounds;
        if (!allowHiddenItem || !Call(g.panel,pi::parent,parent,panel) || !parent || !Visible(parent) ||
            !Bounds(parent,parentBounds) || !ClippedBounds(parent,parentBounds,clip)) return false;
        // A hidden CIconPanel can keep absolute coordinates from an older
        // layout pass. Its resource position is relative to the visible parent.
        int x=0,y=0,insetX=0,insetY=0,insetRight=0,insetBottom=0;
        if (CallVoid(g.panel,pi::pos,panel,&x,&y) &&
            CallVoid(g.panel,pi::inset,parent,&insetX,&insetY,&insetRight,&insetBottom)) {
            const auto absX=static_cast<std::int64_t>(parentBounds.x)+x+insetX;
            const auto absY=static_cast<std::int64_t>(parentBounds.y)+y+insetY;
            if (absX<=-32768 || absX>=32768 || absY<=-32768 || absY>=32768) return false;
            raw.x=static_cast<int>(absX);raw.y=static_cast<int>(absY);
        }
        const int left=(std::max)(raw.x,clip.x),top=(std::max)(raw.y,clip.y);
        const int right=(std::min)(raw.x+raw.w,clip.x+clip.w),bottom=(std::min)(raw.y+raw.h,clip.y+clip.h);
        if (right<=left || bottom<=top) return false;
        clip={left,top,right-left,bottom-top};
    }
    const int left=(std::max)(container.x,clip.x),top=(std::max)(container.y,clip.y);
    const int right=(std::min)(container.x+container.w,clip.x+clip.w);
    const int bottom=(std::min)(container.y+container.h,clip.y+clip.h);
    if (right<=left || bottom<=top) return false;
    visible={left,top,right-left,bottom-top};
    widget.present=true;widget.rect={left-container.x,top-container.y,right-left,bottom-top};
    widget.order=order;
    int z=0;if (Call(g.panel,9,z,panel) && z>=-32768 && z<=32767) widget.z=z;
    Color color;
    if (ThemeColor(object,0x8c,color)) widget.color=ThemeAlpha(object,color);
    return true;
}
bool ThemeLabel(void* object,ThemeWidget& widget,std::wstring* glyph=nullptr) {
    void* image=nullptr;unsigned char flags=0;unsigned long font=0;
    if (!ThemeObject(object,g.client,0x65dfdc) || !Read(Add(object,0x158),image) ||
        !ThemeObject(image,g.client,0x66cb5c) || !Read(Add(image,0x34),flags) ||
        !Read(Add(image,(flags&8)?0x24:0x20),font) || !font) return false;
    widget.font=font;
    int alignment=0,insetX=0,insetY=0;
    if (Read(Add(object,0x154),alignment) && alignment>=0 && alignment<=8) widget.alignment=alignment;
    if (Read(Add(object,0x170),insetX) && insetX>=-4096 && insetX<=4096) widget.insetX=insetX;
    if (Read(Add(object,0x174),insetY) && insetY>=-4096 && insetY<=4096) widget.insetY=insetY;
    if (glyph) {
        const wchar_t* text=nullptr;
        if (Read(Add(image,0x18),text) && text) {
            std::wstring value;
            for (int n=0;n<5;++n) {
                wchar_t c=0;if (!Read(text+n,c)) {value.clear();break;}
                if (!c) {if (!value.empty()) *glyph=value;break;}
                if (n==4 || c<0x20 || c==L'%' || c==L'#' || (c>=0xd800 && c<=0xdfff)) break;
                value.push_back(c);
            }
        }
    }
    return true;
}
bool ThemeImageFor(void* object,const ThemeWidget& widget,const Rect& raw,const Rect& visible,
                   ThemeCapturePass& pass,ThemeImage& output) {
    if (!ThemeObject(object,g.client,0x65d98c)) return false;
    output.widget=widget;output.widget.color={255,255,255,255};
    Color draw{255,255,255,255},fill{0,0,0,0};
    ThemeColor(object,0x16c,draw);ThemeColor(object,0x168,fill);
    output.fill=ThemeAlpha(object,fill);
    unsigned char scale=0,tiles[3]{};int rotation=0;float amount=0;
    const bool simple=Read(Add(object,0x160),scale) && scale==1 &&
        CopyBytes(Add(object,0x161),tiles,sizeof(tiles)) && !tiles[0] && !tiles[1] && !tiles[2] &&
        Read(Add(object,0x164),amount) && std::isfinite(amount) && amount<=0 &&
        Read(Add(object,0x170),rotation) && rotation==0;
    void* image=nullptr;int id=0;
    if (simple && Read(Add(object,0x150),image) && ThemeObject(image,g.vguiModule,0x434f0) &&
        Read(Add(image,4),id) && ThemeTextureFor(id,pass,output.texture)) {
        output.texture.tint=ThemeAlpha(object,draw);
        output.texture.u0=static_cast<float>(visible.x-raw.x)/raw.w;
        output.texture.v0=static_cast<float>(visible.y-raw.y)/raw.h;
        output.texture.u1=static_cast<float>(visible.x+visible.w-raw.x)/raw.w;
        output.texture.v1=static_cast<float>(visible.y+visible.h-raw.y)/raw.h;
    }
    return output.texture.id>0 || output.fill.a>0;
}
bool CharacterIncapMaterial(std::string material) {
    for (char& c:material) {
        if (c=='\\') c='/';
        else if (c>='A' && c<='Z') c=static_cast<char>(c-'A'+'a');
    }
    if (material.compare(0,10,"materials/")==0) material.erase(0,10);
    if (material.size()>=4 && material.compare(material.size()-4,4,".vmt")==0) material.resize(material.size()-4);
    static const std::array<const char*,8> paths={"vgui/s_panel_gambler_incap","vgui/s_panel_producer_incap",
        "vgui/s_panel_coach_incap","vgui/s_panel_mechanic_incap","vgui/s_panel_namvet_incap",
        "vgui/s_panel_teenangst_incap","vgui/s_panel_biker_incap","vgui/s_panel_manager_incap"};
    return std::any_of(paths.begin(),paths.end(),[&](const char* path){return material==path;});
}
bool ThemeHealth(void* object,ThemeCapturePass& pass,ThemeHealthBar& bar) {
    unsigned char material=0;
    if (!ThemeObject(object,g.client,0x5c8cbc) || !Read(Add(object,0x17f),material) || material>1) return false;
    bar.present=true;bar.newMaterial=material!=0;bar.palette=pass.palette;bar.mediumThreshold=pass.medium;
    Color monochrome{0,0,0,0};
    if (ThemeColor(object,0x1c9,monochrome)) {bar.monochrome=monochrome.a!=0;bar.monochromeColor=monochrome;}
    int inset=0;if (Read(Add(object,0x1d0),inset) && inset>=0 && inset<=1024) bar.inset=inset;
    const Color tint=ThemeAlpha(object,{255,255,255,255});
    for (int n=0;n<5;++n) {
        int fill=-1,ticks=-1;
        Read(Add(object,n==4?0x1a0:0x180+n*4),fill);
        Read(Add(object,n==4?0x1a4:0x190+n*4),ticks);
        if (ThemeTextureFor(fill,pass,bar.fill[n])) bar.fill[n].tint=tint;
        if (ThemeTextureFor(ticks,pass,bar.ticks[n])) bar.ticks[n].tint=tint;
    }
    ThemeTexture* old[]={&bar.white,&bar.grey,&bar.outline};
    for (int n=0;n<3;++n) {int id=-1;if (Read(Add(object,0x1a8+n*4),id) && ThemeTextureFor(id,pass,*old[n])) old[n]->tint=tint;}
    return true;
}
bool DecodeThemeIcon(const void* texture,ThemeCapturePass& pass,ThemeIcon& icon) {
    unsigned char font=0;
    if (!texture || !Read(Add(texture,0x80),font) || font>1) return false;
    icon.usingFont=font!=0;
    int left=0,right=0,top=0,bottom=0;
    if (Read(Add(texture,0x9c),left) && Read(Add(texture,0xa0),right) &&
        Read(Add(texture,0xa4),top) && Read(Add(texture,0xa8),bottom) &&
        static_cast<std::int64_t>(right)-left>0 && static_cast<std::int64_t>(bottom)-top>0 &&
        static_cast<std::int64_t>(right)-left<=32768 && static_cast<std::int64_t>(bottom)-top<=32768) {
        icon.width=static_cast<int>(static_cast<std::int64_t>(right)-left);
        icon.height=static_cast<int>(static_cast<std::int64_t>(bottom)-top);
    }
    if (icon.usingFont) {
        signed char glyph=0;
        if (!Read(Add(texture,0x84),icon.font) || !icon.font || !Read(Add(texture,0x82),glyph) || !glyph) return false;
        icon.glyph=static_cast<wchar_t>(static_cast<short>(glyph));return true;
    }
    int id=-1;float uv[4]{};
    if (!Read(Add(texture,0x88),id) || !CopyBytes(Add(texture,0x8c),uv,sizeof(uv)) ||
        !ThemeTextureFor(id,pass,icon.texture)) return false;
    for (float value:uv) if (!std::isfinite(value) || value<0 || value>1) return false;
    if (uv[2]<=uv[0] || uv[3]<=uv[1] || icon.width<=0 || icon.height<=0) return false;
    icon.texture.u0=uv[0];icon.texture.v0=uv[1];icon.texture.u1=uv[2];icon.texture.v1=uv[3];return true;
}
bool ThemeInventoryIcon(void* object,int kind,int item,ThemeCapturePass& pass,ThemeIcon& icon) {
    void* texture=nullptr;unsigned char scale=0;
    if (item<=0 || !ThemeObject(object,g.client,0x590654) || !Read(Add(object,0x150),texture) ||
        !Read(Add(object,0x1d4),scale) || scale>1 || !ThemeColor(object,0x1d6,icon.color)) return false;
    icon.kind=kind;icon.item=item;icon.scale=scale!=0;icon.color=ThemeAlpha(object,icon.color);
    return DecodeThemeIcon(texture,pass,icon);
}
bool ReadGameItemArt(int item,ThemeIcon& icon) {
    // Read the pinned client's initialized weapon-info dictionary on the UI
    // thread. These are the same handles and icon field the team HUD reads.
    std::uint8_t initialized=0;
    std::uint16_t handle=0,capacity=0;
    void* records=nullptr;
    void* info=nullptr;
    void* texture=nullptr;
    if (!Read(Add(g.client,0x7b4830),initialized) || initialized!=1 ||
        !Read(Add(g.client,0x7b4838+item*2),handle) || handle==0xffff ||
        !Read(Add(g.client,0x6dfdf2),capacity) || handle>=capacity ||
        !Read(Add(g.client,0x6dfde4),records) || !records ||
        !Read(Add(records,handle*16+0xc),info) || !info ||
        !Read(Add(info,0x880),texture) || !texture) return false;
    ThemeCapturePass pass;
    icon.item=item;
    return DecodeThemeIcon(texture,pass,icon);
}
const ThemeIcon* GameHudItemIconStep(int item,DWORD now) {
    if (!g.ready || !g.client || !g.surface || item<=0 || item>=kWeaponInfoIdCount) return nullptr;
    auto& cached=g.itemIcons[static_cast<std::size_t>(item)];
    if (cached.attempted && static_cast<LONG>(now-cached.nextLookup)<0)
        return cached.ready?&cached.icon:nullptr;
    MemoryQueryScope memoryQueries;
    ++g_runtimeCounters.itemIconRetries;
    cached.attempted=true;
    cached.icon={};
    cached.ready=ReadGameItemArt(item,cached.icon);
    cached.nextLookup=now+(cached.ready?1000:250);
    return cached.ready?&cached.icon:nullptr;
}
void StoreThemeIcon(GameHudTheme& theme,const ThemeIcon& icon) {
    const auto found=std::find_if(theme.icons.begin(),theme.icons.end(),[&](const ThemeIcon& old){return old.kind==icon.kind && old.item==icon.item;});
    if (found!=theme.icons.end()) *found=icon;
    else if (theme.icons.size()<96) theme.icons.push_back(icon);
}
bool CaptureCardStyle(const SafePanel& source,const HudSlot& slot,const Player& player,
                      ThemeCapturePass& pass,GameHudTheme& theme,GameHudCardStyle& style,
                      GameHudCardStyle* resourceSignature=nullptr) {
    const Rect container=slot.hasVisibleBounds?slot.visibleBounds:slot.bounds;
    if (container.w<=0 || container.h<=0) return false;
    std::vector<SafePanel> nodes;if (!TraverseSafe(source,nodes,128,512)) return false;
    style.width=container.w;style.height=container.h;
    std::array<unsigned,9> count{};
    std::array<Rect,9> resourceRects{};
    std::unordered_map<std::string,unsigned> decorations;
    std::vector<std::pair<std::string,ThemeImage>> images;
    for (std::size_t order=0;order<nodes.size();++order) {
        const auto panel=ResolveSafePanel(nodes[order]);
        if (!panel) return false;
        if (panel==source.raw || !OwnedByTeamPanel(panel,source.raw)) continue;
        const auto name=PanelString(panel,pi::name);
        int role=name=="Head"?0:name=="Name"?1:name=="HealthNumber"?2:name=="Status"?3:
                 name=="Health"?4:name=="HealthIcon"?5:name=="IconForSlot_Grenade"?6:
                 name=="IconForSlot_First_Aid"?7:name=="IconForSlot_Pills"?8:-1;
        const bool decoration=name=="BackgroundImage" || name=="Dead" || name=="Frame" || name=="Border";
        if (role<0 && !decoration) continue;
        void* object=ClientPanel(panel);ThemeWidget widget;Rect raw,visible;bool shown=false;
        const int vtable=role==0 || decoration?0x65d98c:role==4?0x5c8cbc:role>=6?0x590654:0x65dfdc;
        const int sourceItem=role==6?player.throwable:role==7?player.medical:role==8?player.pills:0;
        if (!ThemeObject(object,g.client,vtable) ||
            !ThemeWidgetFor(panel,object,container,static_cast<int>(order),widget,raw,visible,shown,role>=6)) continue;
        if (role>=0) {
            ++count[role];
            resourceRects[role]={raw.x-slot.bounds.x,raw.y-slot.bounds.y,raw.w,raw.h};
        }
        if (role==0 && ThemeObject(object,g.client,0x65d98c)) {
            Color draw{255,255,255,255};ThemeColor(object,0x16c,draw);
            widget.color=ThemeAlpha(object,draw);style.portrait=widget;
        } else if (role>=1 && role<=3 && ThemeLabel(object,widget)) {
            (role==1?style.name:role==2?style.number:style.status)=widget;
        } else if (role==4 && ThemeHealth(object,pass,style.bar)) {
            // A clipped source bar needs an additional viewport transform;
            // leave this one element to the existing bar in that case.
            if (raw.x==visible.x && raw.y==visible.y && raw.w==visible.w && raw.h==visible.h) {
                widget.color={255,255,255,255};style.health=widget;
            }
        } else if (role==5 && ThemeLabel(object,widget,&style.healthGlyph)) style.healthIcon=widget;
        else if (role>=6) {
            const int kind=role-6;
            Color tint{255,255,255,255};ThemeColor(object,0x1d6,tint);
            widget.color=ThemeAlpha(object,tint);
            if (raw.x!=visible.x || raw.y!=visible.y || raw.w!=visible.w || raw.h!=visible.h) continue;
            style.items[kind]=widget;
            ThemeIcon icon;
            if (shown && sourceItem>0 && ThemeInventoryIcon(object,kind,sourceItem,pass,icon)) StoreThemeIcon(theme,icon);
        } else if (decoration) {
            ThemeImage image;
            if (ThemeImageFor(object,widget,raw,visible,pass,image)) {
                const auto state=ThemeStateIndex(player);
                image.characterIncap=name=="BackgroundImage" && (state==1 || state==2) &&
                                     CharacterIncapMaterial(image.texture.material);
                ++decorations[name];images.emplace_back(name,std::move(image));
            }
        }
    }
    ThemeWidget* widgets[]={&style.portrait,&style.name,&style.number,&style.status,&style.health,&style.healthIcon,
                            &style.items[0],&style.items[1],&style.items[2]};
    for (int n=0;n<9;++n) if (count[n]>1) widgets[n]->present=false;
    if (!style.health.present) style.bar.present=false;
    if (!style.healthIcon.present) style.healthGlyph.clear();
    bool deadDecoration=false;
    for (auto& image:images) if (decorations[image.first]==1 && style.images.size()<32) {
        if (image.first=="Dead" && image.second.texture.id>0) deadDecoration=true;
        style.images.push_back(std::move(image.second));
    }
    const auto state=ThemeStateIndex(player);
    style.ready=state==0?style.portrait.present && style.name.present && style.number.present && style.health.present:
        state==3?style.name.present && (style.portrait.present || deadDecoration):
                 style.name.present && style.number.present && style.health.present;
    if (style.ready && resourceSignature) {
        *resourceSignature=style;
        // Clipping, fades, inventory visibility and a character's artwork can
        // change without reloading resources. Only compare uncut core geometry,
        // fonts and health materials belonging to the same source/state.
        resourceSignature->width=slot.bounds.w;resourceSignature->height=slot.bounds.h;
        resourceSignature->name.rect=resourceRects[1];resourceSignature->number.rect=resourceRects[2];
        resourceSignature->health.rect=resourceRects[4];
        resourceSignature->portrait={};resourceSignature->status={};resourceSignature->healthIcon={};
        resourceSignature->items={};resourceSignature->images.clear();
    }
    return style.ready;
}
bool ThemeSkinChanged(const GameHudCardStyle& before,const GameHudCardStyle& after) {
    if (!before.ready || !after.ready) return false;
    const auto rect=[](const Rect& a,const Rect& b){return a.x==b.x && a.y==b.y && a.w==b.w && a.h==b.h;};
    const auto texture=[](const ThemeTexture& a,const ThemeTexture& b){return a.id==b.id && a.material==b.material;};
    if (before.width!=after.width || before.height!=after.height || before.images.size()!=after.images.size() ||
        before.bar.present!=after.bar.present || before.bar.newMaterial!=after.bar.newMaterial) return true;
    const ThemeWidget* a[]={&before.portrait,&before.name,&before.number,&before.status,&before.health,&before.healthIcon};
    const ThemeWidget* b[]={&after.portrait,&after.name,&after.number,&after.status,&after.health,&after.healthIcon};
    for (int n=0;n<6;++n) if (a[n]->present!=b[n]->present || a[n]->font!=b[n]->font ||
                              a[n]->alignment!=b[n]->alignment || a[n]->insetX!=b[n]->insetX || a[n]->insetY!=b[n]->insetY ||
                              (a[n]->present && !rect(a[n]->rect,b[n]->rect))) return true;
    for (std::size_t n=0;n<before.images.size();++n)
        if (before.images[n].characterIncap!=after.images[n].characterIncap ||
            (!before.images[n].characterIncap && !texture(before.images[n].texture,after.images[n].texture)) ||
            !rect(before.images[n].widget.rect,after.images[n].widget.rect)) return true;
    for (int n=0;n<5;++n) if (!texture(before.bar.fill[n],after.bar.fill[n]) || !texture(before.bar.ticks[n],after.bar.ticks[n])) return true;
    return !texture(before.bar.white,after.bar.white) || !texture(before.bar.grey,after.bar.grey) ||
           !texture(before.bar.outline,after.bar.outline);
}
bool ThemeCoreCompatible(const GameHudCardStyle& a,const GameHudCardStyle& b) {
    if (!a.ready || !b.ready || a.width!=b.width || a.height!=b.height) return false;
    const ThemeWidget* left[]={&a.portrait,&a.name,&a.number,&a.health};
    const ThemeWidget* right[]={&b.portrait,&b.name,&b.number,&b.health};
    for (int n=0;n<4;++n) {
        const auto& x=*left[n];const auto& y=*right[n];
        if (x.present!=y.present || x.rect.x!=y.rect.x || x.rect.y!=y.rect.y || x.rect.w!=y.rect.w ||
            x.rect.h!=y.rect.h || x.font!=y.font || x.alignment!=y.alignment ||
            x.insetX!=y.insetX || x.insetY!=y.insetY) return false;
    }
    return true;
}
bool ThemeItemGeometry(const ThemeWidget& widget) {
    return widget.present && widget.rect.w>0 && widget.rect.h>0;
}
void MergeThemeItems(GameHudCardStyle& target,const GameHudCardStyle& source,const Player& player,
                     std::array<unsigned,3>& priorities,std::array<bool,3>& anyHeld) {
    if (!ThemeCoreCompatible(target,source)) return;
    const int items[]={player.throwable,player.medical,player.pills};
    for (int n=0;n<3;++n) {
        const bool held=items[n]>0;anyHeld[n]=anyHeld[n] || held;
        const auto& candidate=source.items[n];
        if (!ThemeItemGeometry(candidate)) continue;
        const unsigned priority=held?2u:1u;
        auto& current=target.items[n];
        if (priority>priorities[n]) {
            const Color previous=current.color;const bool previousTint=ThemeItemGeometry(current) && previous.a>0;
            current=candidate;priorities[n]=priority;
            if (current.color.a<=0 && previousTint) current.color=previous;
        } else if (current.color.a<=0 && candidate.color.a>0) current.color=candidate.color;
    }
}
bool ThemeAppearanceDegraded(const GameHudCardStyle& before,const GameHudCardStyle& after) {
    if (!ThemeCoreCompatible(before,after)) return false;
    const auto faded=[](const ThemeWidget& a,const ThemeWidget& b) {
        return a.present && b.present && a.color.a>0 && b.color.a<=0;
    };
    if (faded(before.portrait,after.portrait) || faded(before.name,after.name) ||
        faded(before.number,after.number) || faded(before.healthIcon,after.healthIcon)) return true;
    for (std::size_t n=0;n<before.items.size();++n) if (faded(before.items[n],after.items[n])) return true;
    // Capturing during a fade or an unresolved optional bitmap is not evidence
    // of a new skin. Give the previous compatible value a short recovery window.
    for (const auto& image:before.images) {
        if (image.fill.a<=0 && (image.texture.id<=0 || image.texture.tint.a<=0)) continue;
        const auto found=std::find_if(after.images.begin(),after.images.end(),[&](const ThemeImage& next) {
            const auto& a=image.widget.rect;const auto& b=next.widget.rect;
            return image.widget.order==next.widget.order && image.widget.z==next.widget.z &&
                a.x==b.x && a.y==b.y && a.w==b.w && a.h==b.h;
        });
        if (found==after.images.end() ||
            (found->fill.a<=0 && (found->texture.id<=0 || found->texture.tint.a<=0))) return true;
    }
    return false;
}
bool SameThemePanel(const SafePanel& a,const SafePanel& b) {
    return a.raw==b.raw && a.handle==b.handle;
}
const GameHudTheme& GameHudThemeStep(const Snapshot& snapshot,const std::vector<HudSlot>& slots,
                                    int width,int height,DWORD now,std::string& error) {
    ++g_runtimeCounters.themeReads;error.clear();auto& cache=g.gameTheme;
    const auto root=ResolveSafePanel(g.nativePanels.rootRef);
    if (!g.ready || !snapshot.inGame || !g.client || !g.vguiModule || !root || width<=0 || height<=0) {
        cache={};error="Current native HUD unavailable";return cache.theme;
    }
    std::vector<HudThemeSource> sources;
    for (const auto& slot:slots) {
        const auto found=std::find_if(g.nativePanels.panelRefs.begin(),g.nativePanels.panelRefs.end(),
                                     [&](const SafePanel& saved){return saved.raw==slot.panel;});
        const auto player=std::find_if(snapshot.players.begin(),snapshot.players.end(),
                                      [&](const Player& value){return value.index==slot.playerIndex;});
        if (!slot.visible || found==g.nativePanels.panelRefs.end() || player==snapshot.players.end() || !ResolveSafePanel(*found)) continue;
        HudThemeSource source;source.panel=*found;source.playerIndex=slot.playerIndex;source.state=ThemeStateIndex(*player);
        source.items={player->throwable,player->medical,player->pills};
        if (!Call(g.panel,27,source.scheme,slot.panel)) continue;
        sources.push_back(source);
        if (sources.size()==4) break;
    }
    bool reset=cache.panelInterface!=g.panel || cache.vguiInterface!=g.vgui || cache.surfaceInterface!=g.surface ||
               cache.clientModule!=g.client || cache.vguiModule!=g.vguiModule ||
               cache.root.raw!=root || cache.root.handle!=g.nativePanels.rootRef.handle || cache.width!=width || cache.height!=height;
    bool changed=cache.sources.size()!=sources.size();
    if (!reset && !sources.empty() && !cache.generationSources.empty() &&
        std::none_of(sources.begin(),sources.end(),[&](const HudThemeSource& source) {
            return std::any_of(cache.generationSources.begin(),cache.generationSources.end(),
                               [&](const SafePanel& old){return SameThemePanel(old,source.panel);});
        })) reset=true;
    if (!reset) for (const auto& next:sources) {
        // Old references are identity keys only. Never call IPanel on a retired
        // source merely to decide whether copied theme values remain useful.
        const auto old=std::find_if(cache.sources.begin(),cache.sources.end(),[&](const HudThemeSource& prior) {
            return SameThemePanel(prior.panel,next.panel);
        });
        if ((!cache.schemes.empty() && std::find(cache.schemes.begin(),cache.schemes.end(),next.scheme)==cache.schemes.end()) ||
            (old!=cache.sources.end() && old->scheme!=next.scheme)) {reset=true;break;}
        if (old==cache.sources.end() || old->playerIndex!=next.playerIndex || old->state!=next.state || old->items!=next.items) changed=true;
    }
    if (reset) {
        cache={};cache.panelInterface=g.panel;cache.vguiInterface=g.vgui;cache.surfaceInterface=g.surface;
        g.itemIcons={};
        cache.clientModule=g.client;cache.vguiModule=g.vguiModule;
        cache.root=g.nativePanels.rootRef;cache.width=width;cache.height=height;
    }
    if (sources.empty()) {
        if (!cache.noSources) {cache.noSources=true;cache.noSourcesSince=now;}
        cache.sources.clear();
        if (now-cache.noSourcesSince>=2000) cache.theme={};
        cache.theme.status=cache.theme.available?"native sources unavailable; retaining theme for up to 2000ms":"No visible native card to sample";
        error=cache.theme.status;return cache.theme;
    }
    cache.noSources=false;
    if (!reset && static_cast<LONG>(now-cache.nextCapture)<0 && (!changed || cache.retryPending)) return cache.theme;
    ++g_runtimeCounters.themeCaptures;cache.nextCapture=now+500;cache.sources=sources;
    cache.generationSources.clear();
    for (const auto& source:sources) cache.generationSources.push_back(source.panel);
    if (cache.schemes.empty()) for (const auto& source:sources)
        if (std::find(cache.schemes.begin(),cache.schemes.end(),source.scheme)==cache.schemes.end()) cache.schemes.push_back(source.scheme);
    cache.observations.erase(std::remove_if(cache.observations.begin(),cache.observations.end(),[&](const HudThemeObservation& observed) {
        return std::none_of(sources.begin(),sources.end(),[&](const HudThemeSource& source){return SameThemePanel(source.panel,observed.panel);});
    }),cache.observations.end());
    ThemeCapturePass pass;ThemePalette(pass);
    GameHudTheme sampled;
    std::array<bool,4> attempted{},captured{};
    std::array<std::array<unsigned,3>,4> itemPriorities{};
    std::array<std::array<bool,3>,4> anyHeld{};
    bool resourceChanged=false;
    for (std::size_t n=0;n<sources.size();++n) {
        const auto& source=sources[n];
        const auto player=std::find_if(snapshot.players.begin(),snapshot.players.end(),[&](const Player& p){return p.index==source.playerIndex;});
        const auto slot=std::find_if(slots.begin(),slots.end(),[&](const HudSlot& s){return s.panel==source.panel.raw && s.playerIndex==source.playerIndex;});
        if (player==snapshot.players.end() || slot==slots.end()) continue;
        attempted[source.state]=true;
        GameHudCardStyle style,signature;GameHudTheme icons;
        if (!CaptureCardStyle(source.panel,*slot,*player,pass,icons,style,&signature)) continue;
        const auto observed=std::find_if(cache.observations.begin(),cache.observations.end(),[&](const HudThemeObservation& value) {
            return SameThemePanel(value.panel,source.panel) && value.state==source.state;
        });
        if (observed==cache.observations.end()) cache.observations.push_back({source.panel,source.state,std::move(signature),{},0});
        else if (ThemeSkinChanged(observed->accepted,signature)) {
            if (observed->confirmations && !ThemeSkinChanged(observed->pending,signature)) {
                if (now-observed->pendingSince>=100) ++observed->confirmations;
            } else {observed->pending=signature;observed->confirmations=1;observed->pendingSince=now;}
            if (observed->confirmations>=2) {
                resourceChanged=true;observed->accepted=std::move(signature);observed->pending={};observed->confirmations=0;
            }
        } else {observed->pending={};observed->confirmations=0;}
        for (const auto& icon:icons.icons) StoreThemeIcon(sampled,icon);
        if (!captured[source.state]) {sampled.states[source.state]=style;captured[source.state]=true;}
        MergeThemeItems(sampled.states[source.state],style,*player,itemPriorities[source.state],anyHeld[source.state]);
        if (source.state==0 && style.number.present && style.number.color.a>0) {
            const float temporary=std::isfinite(player->temporaryHealth)?std::clamp(player->temporaryHealth,0.0f,static_cast<float>((std::max)(0,player->health))):0;
            const float ratio=(player->health-temporary)/(std::max)(1,player->maxHealth);
            const std::size_t band=player->blackWhite?3u:ratio<0.25f?0u:ratio<pass.medium?1u:2u;
            sampled.numberPalette[band]=style.number.color;sampled.numberPaletteObserved[band]=true;
        }
    }
    // Same-scheme hot reloads require two matching observations from the same
    // source and state. A clipped card, a revived teammate or a new sampling
    // representative cannot invalidate another survivor's down/dead template.
    if (resourceChanged) {cache.theme={};cache.failed={};cache.failureSince={};g.itemIcons={};}
    for (std::size_t n=0;n<captured.size();++n) if (captured[n] && ThemeCoreCompatible(cache.theme.states[n],sampled.states[n])) {
        for (int kind=0;kind<3;++kind) {
            const auto& previous=cache.theme.states[n].items[kind];
            auto& current=sampled.states[n].items[kind];
            // Item possession controls the extra player's data, never whether
            // a compatible template is allowed to retain an inventory slot.
            if (ThemeItemGeometry(previous)) {
                if (!ThemeItemGeometry(current)) current=previous;
                else if (!anyHeld[n][kind] && current.color.a<=0 && previous.color.a>0) current.color=previous.color;
            }
        }
    }
    std::array<bool,4> retained{},appearance{};
    bool retry=false;
    for (std::size_t n=0;n<captured.size();++n) if (attempted[n]) {
        appearance[n]=captured[n] && ThemeAppearanceDegraded(cache.theme.states[n],sampled.states[n]);
        if (!captured[n] || appearance[n]) {
            if (!cache.failed[n]) {cache.failed[n]=true;cache.failureSince[n]=now;}
            if (now-cache.failureSince[n]<2000) {
                retained[n]=cache.theme.states[n].ready;retry=true;
                continue;
            }
            if (!captured[n]) {cache.theme.states[n]={};continue;}
        }
        cache.failed[n]=false;
        cache.theme.states[n]=std::move(sampled.states[n]);
    }
    cache.retryPending=retry;
    if (retry) cache.nextCapture=now+100;
    for (const auto& icon:sampled.icons) {
        const auto previous=std::find_if(cache.theme.icons.begin(),cache.theme.icons.end(),[&](const ThemeIcon& value) {
            return value.kind==icon.kind && value.item==icon.item;
        });
        if (retry && icon.color.a<=0 && previous!=cache.theme.icons.end() && previous->color.a>0) continue;
        StoreThemeIcon(cache.theme,icon);
    }
    for (std::size_t n=0;n<sampled.numberPaletteObserved.size();++n) if (sampled.numberPaletteObserved[n]) {
        cache.theme.numberPalette[n]=sampled.numberPalette[n];cache.theme.numberPaletteObserved[n]=true;
    }
    for (std::size_t n=0;n<3;++n) if (!cache.theme.numberPaletteObserved[n])
        cache.theme.numberPalette[n]=pass.palette[n+1];
    for (auto& style:cache.theme.states) if (style.bar.present) {
        style.bar.palette=pass.palette;style.bar.mediumThreshold=pass.medium;
    }
    cache.theme.available=std::any_of(cache.theme.states.begin(),cache.theme.states.end(),[](const GameHudCardStyle& style){return style.ready;});
    std::string readyBits,retainedBits,failedBits,appearanceBits;
    for (std::size_t n=0;n<captured.size();++n) {
        readyBits+=cache.theme.states[n].ready?'1':'0';retainedBits+=retained[n]?'1':'0';
        failedBits+=cache.failed[n]?'1':'0';appearanceBits+=appearance[n]?'1':'0';
    }
    cache.theme.status=std::string(cache.theme.available?"sampled current native HUD":"waiting for supported native controls")+
        " ready="+readyBits+" retained="+retainedBits+" failed="+failedBits+" appearance="+appearanceBits+
        " retry="+(retry?"100ms":"500ms");
    if (!cache.theme.available) error=cache.theme.status;
    return cache.theme;
}
} // namespace

void AbortMemoryQueries() { g_memoryQueries.Abort(); }

bool Runtime::Initialize(std::string& error) {
    if (g.ready) return true;
    g.engine=Create(GetModuleHandleW(L"engine.dll"),"VEngineClient013");
    g.entities=Create(GetModuleHandleW(L"client.dll"),"VClientEntityList003");
    g.panel=Create(GetModuleHandleW(L"vgui2.dll"),"VGUI_Panel009");
    g.vgui=Create(GetModuleHandleW(L"vgui2.dll"),"VGUI_ivgui008");
    g.surface=Create(GetModuleHandleW(L"vguimatsurface.dll"),"VGUI_Surface031");
    g.neko=GetModuleHandleW(L"left4neko.dll");
    g.client=GetModuleHandleW(L"client.dll");g.vguiModule=GetModuleHandleW(L"vgui2.dll");
    g.ready=g.engine&&g.entities&&g.panel&&g.vgui&&g.surface&&g.neko;
    if (!g.ready) {
        error="Waiting for engine013 / entitylist003 / panel009 / ivgui008 / surface031 / Neko modules";
        g.status=error;
        return false;
    }
    // Do not call speculative virtual functions to test a version. The exact
    // interface names and Neko hash are checked by the plugin before this point.
    if (!Method(g.panel,41) || !Method(g.panel,55) || !Method(g.vgui,15) || !Method(g.vgui,16) ||
        !Method(g.entities,0) || !Method(g.engine,26)) {
        g.ready=false;
        error="Interface function pointers are not executable; adapter disabled";
        g.status=error;
        return false;
    }
    error.clear();
    g.status="Interfaces acquired; waiting for a map and native HUD";
    return true;
}
bool Runtime::Ready() const { return g.ready; }
void* Runtime::PanelInterface() const { return g.panel; }
void* Runtime::SurfaceInterface() const { return g.surface; }
std::uint32_t Runtime::RootPanel() const {
    MemoryQueryScope memoryQueries;
    std::uint32_t root=0;
    return g.ready && Call(g.surface,6,root) ? root : 0;
}
bool Runtime::IsTeamRoot(std::uint32_t panel) const {
    MemoryQueryScope memoryQueries;
    if (!g.ready || !panel) return false;
    return PanelString(panel,pi::className)=="CHudTeamDisplay" || PanelString(panel,pi::name)=="CHudTeamDisplay";
}
bool Runtime::TeamHudVisible(std::uint32_t panel) const {
    MemoryQueryScope memoryQueries;
    if (!g.ready || !IsTeamRoot(panel) || !Visible(panel)) {ResetPanelCaches();return false;}
    return true;
}
bool Runtime::ReadNativeHudPaintState(std::uint32_t panel,NativeHudPaintState& state) const {
    MemoryQueryScope memoryQueries;
    state={};
    SafePanel root;
    if (!g.ready || !g.neko || !CaptureSafePanel(panel,root) || !ResolveSafePanel(root) ||
        PanelString(panel,pi::className)!="CHudTeamDisplay") return false;
    void* object=ClientPanel(panel);void** table=nullptr;void* paint=nullptr;
    if (!object || !Read(object,table) ||
        !Read(ModelAddress(table,3*sizeof(void*),sizeof(paint)),paint) ||
        paint!=ModelAddress(g.neko,0x67a70,1)) return false;
    // This is the exact Neko 2.51.0 class PaintTraverse wrapper, not another
    // addon with the same panel name. Only inspect it; never invoke or patch it.
    void* allowHud=nullptr;
    int enabled=0;
    if (!ModelRead(g.neko,0x207c0c,allowHud) || !ModelRead(allowHud,0x30,enabled) ||
        (enabled!=0 && enabled!=1) || ResolveSafePanel(root)!=panel) return false;
    state.enabled=enabled!=0;state.rootHandle=root.handle;return true;
}
std::vector<HudSlot> Runtime::ReadNativeSlots(std::uint32_t teamRoot,std::string& error) {
    MemoryQueryScope memoryQueries(true);
    std::vector<HudSlot> result;
    error.clear();
    if (!g.ready || !IsTeamRoot(teamRoot)) { error="Native CHudTeamDisplay not found"; return result; }
    if (!TeamHudVisible(teamRoot)) return result;
    int width=0,height=0;
    if (!CallVoid(g.engine,5,&width,&height) || width<=0 || height<=0) {
        ResetPanelCaches();error="Native HUD screen size unavailable";return result;
    }
    const DWORD now=GetTickCount();
    const auto& panels=NativePanelCandidates(teamRoot,width,height,now);
    for (std::size_t n=0;n<panels.size();++n) {
        const auto panel=ResolveSafePanel(g.nativePanels.panelRefs[n]);
        if (!panel || panel!=panels[n]) {
            error="Native HUD panel lifetime changed; drawing suppressed";return {};
        }
        HudSlot slot{};
        slot.panel=panel;
        slot.visible=Visible(panel);
        if (!slot.visible) continue;
        void* object=ClientPanel(panel);
        if (!object || !Read(Add(object,0x264),slot.playerIndex)) {
            error="Cannot read native TeamDisplayPlayer target; drawing suppressed";
            return {};
        }
        if (slot.playerIndex<=0) continue;
        if (slot.playerIndex>kMaxPlayers || !Bounds(panel,slot.bounds)) {
            error="Native HUD target/bounds outside validated profile; drawing suppressed";
            return {};
        }
        if (!ClippedBounds(panel,slot.bounds,slot.visibleBounds)) continue;
        slot.hasVisibleBounds=true;
        Rect visual{};
        if (CachedVisualCardBounds(panel,slot.visibleBounds,visual,now)) {
            slot.visibleBounds=visual;
            slot.usesVisualBounds=true;
        }
        void* entity=nullptr;
        if (!Call(g.entities,3,entity,slot.playerIndex) || !entity) continue;
        result.push_back(slot);
    }
    std::sort(result.begin(),result.end(),[](const HudSlot& a,const HudSlot& b){return a.bounds.x<b.bounds.x;});
    if (result.size()>4) { error="More than four native teammate panels detected; unsupported replacement HUD"; return {}; }
    return result;
}
Snapshot Runtime::ReadSnapshot(std::string& error) {
    MemoryQueryScope memoryQueries(true);
    Snapshot snapshot;
    error.clear();
    bool inGame=false, console=false;
    if (!g.ready || !Call(g.engine,26,inGame) || !inGame) {
        ResetPanelCaches();
        g.resourceIndex=-1; g.nextResourceScan=0; g.selfPanel=0; g.nextSelfScan=0; g.selfPanelIsNamed=false;
        g.status="Not in a live map";
        return snapshot;
    }
    if (Call(g.engine,11,console) && console) return snapshot;
    int maxPlayers=0;
    if (!Call(g.engine,12,snapshot.localIndex) || snapshot.localIndex<1 || snapshot.localIndex>kMaxPlayers ||
        !Call(g.engine,21,maxPlayers) || maxPlayers<1 || maxPlayers>kMaxPlayers) {
        error="Engine player count/local slot outside 1..32; adapter disabled for this frame";
        g.status=error; return {};
    }
    void* resource=nullptr;
    if (!Resource(resource)) {
        error=g.resourceError;
        g.status=error; return {};
    }
    snapshot.survivorSet=ReadSurvivorSet();
    snapshot.viewIndex=snapshot.localIndex;
    void* localBase=nullptr;
    ClientClass localClass{};
    if (EntityInfo(snapshot.localIndex,localBase,localClass)) {
        auto localIt=g.playerFields.find(localClass.table);
        if (localIt==g.playerFields.end()) localIt=g.playerFields.emplace(localClass.table,PlayerFields(localClass.table)).first;
        std::uint32_t observer=0;
        int observerMode=0;
        if (Scalar(localBase,localIt->second.observerMode,observerMode) &&
            (observerMode==4 || observerMode==5) && Scalar(localBase,localIt->second.observer,observer)) {
            const int index=observer&0xfff;
            if (index>=1 && index<=maxPlayers) snapshot.viewIndex=index;
        }
    }
    int nativeHealthCount=0,resourceHealthCount=0;
    BeginModelPortraitPass();
    for (int i=1;i<=maxPlayers;++i) {
        unsigned char connected=0,alive=0,incap=0;
        int team=0,health=0,maxHealth=100;
        if (!Array(resource,g.resourceFields.connected,i,connected) || !connected) continue;
        if (!Array(resource,g.resourceFields.team,i,team) || !Array(resource,g.resourceFields.alive,i,alive) ||
            !Array(resource,g.resourceFields.health,i,health) || !Array(resource,g.resourceFields.maxHealth,i,maxHealth) ||
            !Array(resource,g.resourceFields.incap,i,incap)) {
            error="A required resource value became unreadable; no partial roster drawn";
            g.status=error; return {};
        }
        void* entityBase=nullptr;
        ClientClass playerClass{};
        const PlayerFields* fields=nullptr;
        if (EntityInfo(i,entityBase,playerClass)) {
            auto it=g.playerFields.find(playerClass.table);
            if (it==g.playerFields.end()) it=g.playerFields.emplace(playerClass.table,PlayerFields(playerClass.table)).first;
            fields=&it->second;
            int entityTeam=0;
            // A server may modify resource.m_iTeam solely to choose the three
            // stock cards. The individual entity's actual team is preferable.
            if (Scalar(entityBase,fields->team,entityTeam) && entityTeam>=0 && entityTeam<=4) team=entityTeam;
        }
        if (team!=2) continue;
        if (health<0 || health>100000 || maxHealth<1 || maxHealth>100000 || connected>1 || alive>1 || incap>1) {
            error="Resource state failed range checks; no roster drawn";
            g.status=error; return {};
        }
        Player player;
        player.index=i; player.name=PlayerName(i); player.alive=alive!=0;
        player.incapacitated=incap!=0; player.health=health; player.maxHealth=maxHealth;
        Array(resource,g.resourceFields.grenade,i,player.throwable);
        Array(resource,g.resourceFields.medical,i,player.medical);
        Array(resource,g.resourceFields.pills,i,player.pills);
        if (fields) {
            int character=-1;
            unsigned char hanging=0,thirdStrike=0;
            if (Scalar(entityBase,fields->character,character)) {
                player.rawCharacter=character;
                player.character=NormalizeSurvivorCharacter(character,snapshot.survivorSet);
            }
            if (Scalar(entityBase,fields->hanging,hanging) && hanging<=1) player.hanging=hanging!=0;
            if (Scalar(entityBase,fields->thirdStrike,thirdStrike) && thirdStrike<=1) player.blackWhite=thirdStrike!=0;
        }
        // Total HP already includes the time-decayed temporary part. Keep the
        // resource value as the fallback when no supported entity is available.
        int permanent=0,total=0;
        if (player.alive && fields && ReadSurvivorHealth(entityBase,playerClass,*fields,permanent,total)) {
            ++nativeHealthCount;
            player.health=player.incapacitated || player.hanging ? permanent : total;
            if (!player.incapacitated && !player.hanging) player.temporaryHealth=static_cast<float>(total-permanent);
        } else ++resourceHealthCount;
        if (player.temporaryHealth>0) ++g_runtimeCounters.temporaryHealthSamples;
        if (fields) player.modelPortrait=ReadModelPortrait(entityBase,playerClass,*fields);
        snapshot.players.push_back(std::move(player));
    }
    FinishModelPortraitPass();
    g_runtimeCounters.nativeHealthReads+=nativeHealthCount;
    g_runtimeCounters.resourceHealthReads+=resourceHealthCount;
    int selfWidth=0,selfHeight=0;ScreenSize(selfWidth,selfHeight);
    snapshot.hasSelfHudBounds=SelfDiscoveryStep(RootPanel(),g.nativePanels.root,selfWidth,selfHeight,
                                               snapshot.viewIndex,GetTickCount(),snapshot.selfHudBounds);
    snapshot.inGame=true;
    std::ostringstream status;
    status<<"resource entity="<<g.resourceIndex<<" survivors="<<snapshot.players.size()
          <<" local="<<snapshot.localIndex<<" view="<<snapshot.viewIndex
          <<" survivorSet="<<snapshot.survivorSet
          <<" selfHud="<<(snapshot.hasSelfHudBounds?"yes":"unavailable")
          <<" selfHudSource="<<(g.selfPanelIsNamed?"named_local_player":snapshot.hasSelfHudBounds?"verified_target":"none")
          <<" selfScan="<<g.selfDiscovery.lastResult<<" selfScanNodes="<<g.selfDiscovery.visited
          <<" health=native_total (temporary included) healthNative="<<nativeHealthCount
          <<" healthResource="<<resourceHealthCount;
    g.status=status.str();
    return snapshot;
}
void Runtime::ScreenSize(int& width,int& height) const {
    MemoryQueryScope memoryQueries;
    width=height=0;
    if (g.ready) CallVoid(g.engine,5,&width,&height);
}
const GameHudTheme& Runtime::ReadGameHudTheme(const Snapshot& snapshot,const std::vector<HudSlot>& slots,
                                             int width,int height,std::string& error) {
    MemoryQueryScope memoryQueries(true);
    return GameHudThemeStep(snapshot,slots,width,height,GetTickCount(),error);
}
const ThemeIcon* Runtime::ReadGameHudItemIcon(int item) {
    return GameHudItemIconStep(item,GetTickCount());
}
std::string Runtime::Diagnostics() const { return g.status; }
} // namespace neh
