#include "types.hpp"
#include "game_hud_theme.hpp"
#include "draw_trace.hpp"
#include "runtime_counters.hpp"
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <unordered_map>

namespace neh {
namespace {
// Exact Surface031 profile from the inspected vguimatsurface.dll, SHA256:
// 3f2e2d74b3934a482caa8c3d5aa8d1157e04d719185b019ffaa1f9f7b4bb1d58.
// Color(int,int,int,int)=11/19; 10/18 take a packed Color instead.
// GetTextSize=72; 71 is GetCharacterWidth with a different argument count.
template<typename R, typename... Args>
R Call(void* surface, int index, Args... args) {
    TraceSurface(surface,index,"renderer");
    using Fn=R(__thiscall*)(void*,Args...);
    return reinterpret_cast<Fn>((*reinterpret_cast<void***>(surface))[index])(surface,args...);
}
struct ModelMemoryRegion {
    std::uintptr_t begin=0,end=0;
    DWORD state=0,protection=0;
};
struct ModelMemoryQueries {
    // Only page metadata is shared within this draw. Object contents, vtables
    // and methods are still read again under SEH on every use.
    std::array<ModelMemoryRegion,32> regions{};
    std::size_t count=0,replace=0;
    bool active=false;
    void Begin() {count=replace=0;active=true;}
    void End() {count=replace=0;active=false;}
    void Invalidate() {count=replace=0;}
    bool Query(std::uintptr_t address,ModelMemoryRegion& result) {
        if(active) for(std::size_t n=0;n<count;++n) {
            const auto& cached=regions[n];
            if(address>=cached.begin && address<cached.end) {result=cached;return true;}
        }
        MEMORY_BASIC_INFORMATION page{};++g_runtimeCounters.virtualQueries;
        if(!VirtualQuery(reinterpret_cast<void*>(address),&page,sizeof(page)))return false;
        const auto begin=reinterpret_cast<std::uintptr_t>(page.BaseAddress);
        const std::uintptr_t end=begin+page.RegionSize;
        if(end<=begin || address<begin || address>=end)return false;
        result={begin,end,page.State,page.Protect};
        // Failed/guarded regions can become usable during an asset callback;
        // never extend a negative permission observation even within a draw.
        if(active && page.State==MEM_COMMIT && !(page.Protect&PAGE_GUARD) &&
           (page.Protect&0xff)!=PAGE_NOACCESS) {
            if(count<regions.size())regions[count++]=result;
            else {regions[replace]=result;replace=(replace+1)%regions.size();}
        }
        return true;
    }
};
thread_local ModelMemoryQueries g_modelMemoryQueries;
struct ModelMemoryQueryScope {
    ModelMemoryQueryScope() {g_modelMemoryQueries.Begin();}
    ~ModelMemoryQueryScope() {g_modelMemoryQueries.End();}
};
bool ModelReadable(const void* address,std::size_t size) {
    auto current=reinterpret_cast<std::uintptr_t>(address);
    if(!current||!size||current+size<current)return false;
    const auto end=current+size;
    while(current<end) {
        ModelMemoryRegion page;
        if(!g_modelMemoryQueries.Query(current,page)||page.state!=MEM_COMMIT||
           (page.protection&PAGE_GUARD)||(page.protection&0xff)==PAGE_NOACCESS)return false;
        const auto next=page.end;
        if(next<=current)return false;
        current=next;
    }
    return true;
}
template<class T> bool ModelRead(const void* address,T& value) {
    if(!ModelReadable(address,sizeof(value)))return false;
    __try {std::memcpy(&value,address,sizeof(value));return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {g_modelMemoryQueries.Invalidate();return false;}
}
void* ModelMethod(void* object,int slot) {
    void** table=nullptr;void* method=nullptr;
    if(slot<0||slot>256||!ModelRead(object,table)||!table||!ModelRead(table+slot,method)||!method)return nullptr;
    ModelMemoryRegion page;
    if(!g_modelMemoryQueries.Query(reinterpret_cast<std::uintptr_t>(method),page)||
       page.state!=MEM_COMMIT||(page.protection&PAGE_GUARD))return nullptr;
    const DWORD access=page.protection&0xff;
    return access==PAGE_EXECUTE||access==PAGE_EXECUTE_READ||access==PAGE_EXECUTE_READWRITE||access==PAGE_EXECUTE_WRITECOPY?method:nullptr;
}
template<class R,class... Args> bool ModelCall(void* object,int slot,R& output,Args... args) {
    void* method=ModelMethod(object,slot);if(!method)return false;
    g_drawTrace.stage="renderer.model_asset";g_drawTrace.surfaceSlot=slot;
    g_drawTrace.function=reinterpret_cast<std::uintptr_t>(method);
    __try {output=reinterpret_cast<R(__thiscall*)(void*,Args...)>(method)(object,args...);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {g_modelMemoryQueries.Invalidate();return false;}
}
template<class... Args> bool ModelCallVoid(void* object,int slot,Args... args) {
    void* method=ModelMethod(object,slot);if(!method)return false;
    g_drawTrace.stage="renderer.model_asset";g_drawTrace.surfaceSlot=slot;
    g_drawTrace.function=reinterpret_cast<std::uintptr_t>(method);
    __try {reinterpret_cast<void(__thiscall*)(void*,Args...)>(method)(object,args...);return true;}
    __except(EXCEPTION_EXECUTE_HANDLER) {g_modelMemoryQueries.Invalidate();return false;}
}
bool BorrowedTextureReady(void* surface,const ThemeTexture& texture);
void InvalidateModelTextureValidation(int id);
void SetColor(void* s, Color c) { Call<void>(s,11,c.r,c.g,c.b,c.a); }
void Fill(void* s, Rect r, Color c) {
    if (r.w<=0 || r.h<=0) return;
    SetColor(s,c); Call<void>(s,12,r.x,r.y,r.x+r.w,r.y+r.h);
}
void Border(void* s, Rect r, Color c) {
    SetColor(s,c); Call<void>(s,14,r.x,r.y,r.x+r.w,r.y+r.h);
}
std::wstring Wide(const std::string& value) {
    if (value.empty()) return L"";
    int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0);
    UINT page=CP_UTF8;
    if (count<=0) {
        page=CP_ACP;
        count=MultiByteToWideChar(page,0,value.data(),static_cast<int>(value.size()),nullptr,0);
    }
    if (count<=0 || count>2048) return L"?";
    std::wstring result(static_cast<std::size_t>(count),L'\0');
    MultiByteToWideChar(page,0,value.data(),static_cast<int>(value.size()),result.data(),count);
    return result;
}
void Text(void* s, unsigned long font, int x, int y, Color color,
          std::wstring text, int maxWidth=0) {
    if (text.empty()) return;
    if (maxWidth>0) {
        int w=0,h=0;
        for (;;) {
            Call<void,unsigned long,const wchar_t*,int&,int&>(s,72,font,text.c_str(),w,h);
            if (w<=maxWidth || text.size()<=1) break;
            text.pop_back();
            // Never leave half of a UTF-16 surrogate pair.
            if (!text.empty() && text.back()>=0xD800 && text.back()<=0xDBFF) text.pop_back();
        }
    }
    Call<void>(s,17,font);
    Call<void>(s,19,color.r,color.g,color.b,color.a);
    Call<void>(s,20,x,y);
    Call<void,const wchar_t*,int,int>(s,22,text.c_str(),static_cast<int>(text.size()),0);
}
// These are in-game material names, resolved by Surface through the game's
// normal search paths. L4D1 materials have no "lobby_" prefix. The index is
// the player's survivor character, never their displayed name or entity slot.
constexpr std::array<const char*,8> kPortraitPaths={
    "vgui/s_panel_lobby_gambler","vgui/s_panel_lobby_producer",
    "vgui/s_panel_lobby_coach","vgui/s_panel_lobby_mechanic",
    "vgui/s_panel_namvet","vgui/s_panel_teenangst",
    "vgui/s_panel_biker","vgui/s_panel_manager"
};
constexpr std::array<const char*,8> kIncapPortraitPaths={
    "vgui/s_panel_gambler_incap","vgui/s_panel_producer_incap",
    "vgui/s_panel_coach_incap","vgui/s_panel_mechanic_incap",
    "vgui/s_panel_namvet_incap","vgui/s_panel_teenangst_incap",
    "vgui/s_panel_biker_incap","vgui/s_panel_manager_incap"
};
std::array<int,8> g_portraits{};
std::array<int,8> g_incapPortraits{};
void* g_textureSurface=nullptr;
int Portrait(void* surface,int character,bool incapacitated=false) {
    if (character<0 || character>=8) return 0;
    if (g_textureSurface!=surface) {
        g_portraits.fill(0);g_incapPortraits.fill(0);g_textureSurface=surface;
    }
    auto& cache=incapacitated?g_incapPortraits:g_portraits;
    int& id=cache[static_cast<std::size_t>(character)];
    if (id>0 && Call<bool>(surface,32,id)) return id;
    const char* path=(incapacitated?kIncapPortraitPaths:kPortraitPaths)[static_cast<std::size_t>(character)];
    id=Call<int,const char*>(surface,25,path);
    if (id<=0) {
        id=Call<int>(surface,34,false);
        if (id>0) Call<void,int,const char*,int,bool>(surface,27,id,path,1,false);
    }
    int w=0,h=0;
    if (id>0) Call<void,int,int&,int&>(surface,30,id,w,h);
    if (id<=0 || w<=0 || h<=0) { id=0; return 0; }
    return id;
}
struct ModelPortraitKey {
    std::uint64_t revision=0;std::string material;
    bool operator==(const ModelPortraitKey& other) const {return revision==other.revision&&material==other.material;}
};
struct ModelPortraitHash {
    std::size_t operator()(const ModelPortraitKey& key) const {
        return std::hash<std::string>{}(key.material)^(std::hash<std::uint64_t>{}(key.revision)<<1);
    }
};
struct ModelPortraitEntry {int id=0;std::uint64_t used=0;};
std::unordered_map<ModelPortraitKey,ModelPortraitEntry,ModelPortraitHash> g_modelPortraits;
std::uint64_t g_modelPortraitUse=0;
void* g_modelSurface=nullptr;
void* g_modelMaterials=nullptr;
bool ModelPortraitPath(const std::string& path) {
    constexpr char prefix[]="vgui/l4n_s/";
    if(path.size()<=sizeof(prefix)-1||path.size()>=512||path.compare(0,sizeof(prefix)-1,prefix)!=0)return false;
    const auto begin=sizeof(prefix)-1,length=path.size()-begin;
    if(length>134||(length==1&&path[begin]=='.')||(length==2&&path[begin]=='.'&&path[begin+1]=='.'))return false;
    for(std::size_t n=begin;n<path.size();++n) {
        const auto c=static_cast<unsigned char>(path[n]);
        if(c<0x20||c==0x7f||c=='/'||c=='\\'||c==':')return false;
    }
    return true;
}
bool ModelTextureValid(void* surface,int id,const std::string& material) {
    if(id<=0)return false;
    ThemeTexture texture;texture.id=id;texture.material=material;
    return BorrowedTextureReady(surface,texture);
}
int ModelPortraitId(void* surface,const ModelPortrait& model,bool incapacitated) {
    const auto& path=incapacitated?model.incapMaterial:model.normalMaterial;
    const int supplied=incapacitated?model.incapId:model.normalId;
    if(!model.revision||!ModelPortraitPath(path))return 0;
    // A current Neko registry ID can appear after an earlier missing lookup.
    // Validate that live value before consulting the per-revision load cache.
    if(supplied>0&&ModelTextureValid(surface,supplied,path))return supplied;
    ModelPortraitKey key{model.revision,path};
    const auto prior=g_modelPortraits.find(key);
    if(prior!=g_modelPortraits.end()) {
        prior->second.used=++g_modelPortraitUse;
        if(prior->second.id>0&&!ModelTextureValid(surface,prior->second.id,path))prior->second.id=0;
        return prior->second.id;
    }
    if(g_modelPortraits.size()>=256) {
        const auto oldest=std::min_element(g_modelPortraits.begin(),g_modelPortraits.end(),
            [](const auto& a,const auto& b){return a.second.used<b.second.used;});
        g_modelPortraits.erase(oldest);
    }
    auto& saved=g_modelPortraits.emplace(std::move(key),ModelPortraitEntry{0,++g_modelPortraitUse}).first->second;
    int id=0;
    if(!ModelCall(surface,25,id,path.c_str()))return 0;
    // FindMaterial is a verified engine API, never a Neko private loader.
    // Its returned pointer is used immediately and is never retained.
    if(g_modelMaterials) {
        ++g_runtimeCounters.modelPortraitLoadAttempts;
        void* material=nullptr;bool error=true;
        if(!ModelCall(g_modelMaterials,71,material,path.c_str(),"VGUI textures",false,static_cast<const char*>(nullptr))||
           !material||!ModelCall(material,42,error)||error)return 0;
    } else if(id<=0)return 0;
    if(id<=0) {
        if(!ModelCall(surface,34,id,false)||id<=0||!ModelCallVoid(surface,27,id,path.c_str(),1,false))return 0;
    }
    // Uploading may have recycled a Surface ID already queried this draw.
    // Get its current name and size again before adopting the new asset.
    InvalidateModelTextureValidation(id);
    if(!ModelTextureValid(surface,id,path))return 0;
    saved.id=id;return id;
}
int PlayerPortrait(void* surface,const Player& player,bool incapacitated,bool& model) {
    const int id=ModelPortraitId(surface,player.modelPortrait,incapacitated);
    model=id>0;
    return id>0?id:Portrait(surface,player.character,incapacitated);
}
// Small vector symbols avoid shipping game artwork or assuming an icon atlas.
// The actual item ID controls whether a slot is occupied; medical is a cross,
// pills a capsule, and throwables use a grenade silhouette.
void ItemSymbol(void* surface, Rect box, int kind, int item, Color ink) {
    if (item<=0) return;
    Fill(surface,box,{22,24,22,230});
    Border(surface,box,{110,116,105,220});
    const int unit=std::max(1,box.w/5), cx=box.x+box.w/2, cy=box.y+box.h/2;
    if (kind==1) {
        Fill(surface,{cx-unit/2,box.y+2,unit,box.h-4},ink);
        Fill(surface,{box.x+2,cy-unit/2,box.w-4,unit},ink);
    } else if (kind==2) {
        const int pad=std::max(2,box.w/4);
        Fill(surface,{box.x+pad,box.y+2,box.w-pad*2,box.h-4},ink);
        Fill(surface,{box.x+pad,cy,box.w-pad*2,1},{50,50,50,255});
    } else {
        Fill(surface,{cx-unit,cy-unit,unit*2,unit*2+1},ink);
        Fill(surface,{cx,cy-unit*2,unit,unit},ink);
        Fill(surface,{cx+unit,cy-unit*2,unit,1},ink);
    }
}
bool DrawResolvedItem(void* surface,ItemIconLookup lookup,int item,const Rect& box,const Rect& clip,Color ink);
void CurrentCard(void* surface,unsigned long font,unsigned long small,const Player& p,const Rect& b,
                 ItemIconLookup lookupItem) {
    const int pad=std::max(3,b.h/16);
    const int footer=std::max(12,b.h/4);
    const int portrait=std::max(12,b.h-footer-pad*2);
    const int barX=b.x+portrait+pad*3;
    const int right=b.x+b.w-pad;
    const Color health=p.blackWhite?Color{225,225,225,255}:
        p.health<=24?Color{235,56,48,255}:p.health<=39?Color{235,196,39,255}:Color{135,231,44,255};
    Fill(surface,b,{7,10,8,218}); Border(surface,b,{86,91,78,235});
    const Rect photo={b.x+pad,b.y+pad,portrait,portrait};
    Fill(surface,photo,{36,39,34,255});
    bool modelPortrait=false;const int texture=PlayerPortrait(surface,p,false,modelPortrait);
    if (texture>0) {
        SetColor(surface,p.alive?Color{255,255,255,255}:Color{100,100,100,255});
        Call<void>(surface,29,texture);
        Call<void>(surface,31,photo.x,photo.y,photo.x+photo.w,photo.y+photo.h);
        if(modelPortrait)++g_runtimeCounters.modelPortraitDraws;
    } else {
        auto name=Wide(p.name);
        Text(surface,font,photo.x+3,photo.y+2,{210,216,204,255},name.empty()?L"?":name.substr(0,1),photo.w-4);
    }
    const int barHeight=std::max(5,b.h/7);
    const int barY=b.y+portrait+pad-barHeight;
    if (right>barX) {
        const int barWidth=right-barX;
        const float total=static_cast<float>(std::max(0,p.health));
        const float temporary=p.alive && !p.incapacitated && !p.hanging && std::isfinite(p.temporaryHealth) ?
            std::clamp(p.temporaryHealth,0.0f,total) : 0.0f;
        const float maximum=static_cast<float>(std::max(1,p.maxHealth));
        Fill(surface,{barX,barY,barWidth,barHeight},{45,49,41,255});
        const int healthWidth=static_cast<int>(barWidth*std::clamp((total-temporary)/maximum,0.0f,1.0f));
        if (p.alive) Fill(surface,{barX,barY,healthWidth,barHeight},health);
        if (temporary>0) {
            const int totalWidth=static_cast<int>(barWidth*std::clamp(total/maximum,0.0f,1.0f));
            Fill(surface,{barX+healthWidth,barY,totalWidth-healthWidth,barHeight},{166,179,109,210});
        }
    }
    std::wstring label;
    if (!p.alive) label=L"DEAD";
    else if (p.hanging) label=L"HANG "+std::to_wstring(std::max(0,p.health));
    else if (p.incapacitated) label=L"DOWN "+std::to_wstring(std::max(0,p.health));
    else label=L"+"+std::to_wstring(std::max(0,p.health));
    bool showItems=b.w>=90 && portrait>=25 && right-barX>=68;
    const int iconSize=std::clamp(b.h/5,8,12);
    const int iconStart=std::max(barX,right-3*(iconSize+2));
    int labelWidth=showItems?std::max(10,iconStart-barX-2):std::max(10,right-barX);
    unsigned long labelFont=(!p.alive || p.hanging || p.incapacitated)?small:font;
    int measured=0, measuredHeight=0;
    Call<void,unsigned long,const wchar_t*,int&,int&>(surface,72,labelFont,label.c_str(),measured,measuredHeight);
    if (measured>labelWidth) {
        labelFont=small;
        Call<void,unsigned long,const wchar_t*,int&,int&>(surface,72,labelFont,label.c_str(),measured,measuredHeight);
    }
    if (measured>labelWidth && showItems) { showItems=false; labelWidth=right-barX; }
    if (measured>labelWidth && p.alive && (p.hanging || p.incapacitated)) {
        // At very small card sizes retain an intact number rather than
        // clipping a status label into a different health value.
        label=std::to_wstring(std::max(0,p.health));
        Call<void,unsigned long,const wchar_t*,int&,int&>(surface,72,labelFont,label.c_str(),measured,measuredHeight);
    }
    // A truncated number changes its meaning (e.g. +100 to +10). Prefer a
    // smaller label, then omit inventory, and finally omit the number itself.
    if (measured<=labelWidth)
        Text(surface,labelFont,barX,b.y+pad,p.alive?health:Color{183,72,67,255},label);
    // Inventory is deliberately below the number rather than competing with it.
    if (showItems) {
        const int size=iconSize;
        const int iy=std::max(b.y+pad,barY-size-2);
        const int first=std::max(barX,right-3*(size+2));
        const int items[]={p.throwable,p.medical,p.pills};
        const Color ink{232,236,227,255};
        for(int kind=0;kind<3;++kind) {
            const Rect rect{first+kind*(size+2),iy,size,size};
            if(!DrawResolvedItem(surface,lookupItem,items[kind],rect,b,ink))
                ItemSymbol(surface,rect,kind,items[kind],ink);
        }
    }
    Text(surface,small,b.x+pad,b.y+b.h-footer+1,{235,237,231,255},Wide(p.name),b.w-pad*2);
}

Color BoundedColor(Color c) {
    return {std::clamp(c.r,0,255),std::clamp(c.g,0,255),std::clamp(c.b,0,255),std::clamp(c.a,0,255)};
}
Rect Intersection(const Rect& a,const Rect& b) {
    const auto x=(std::max<std::int64_t>)(a.x,b.x),y=(std::max<std::int64_t>)(a.y,b.y);
    const auto right=(std::min)(static_cast<std::int64_t>(a.x)+a.w,static_cast<std::int64_t>(b.x)+b.w);
    const auto bottom=(std::min)(static_cast<std::int64_t>(a.y)+a.h,static_cast<std::int64_t>(b.y)+b.h);
    if(a.w<=0 || a.h<=0 || b.w<=0 || b.h<=0 || right<=x || bottom<=y)return {};
    return {static_cast<int>(x),static_cast<int>(y),static_cast<int>(right-x),static_cast<int>(bottom-y)};
}
Rect Project(const Rect& source,const GameHudCardStyle& style,const Rect& card) {
    if(source.w<=0 || source.h<=0 || style.width<=0 || style.height<=0 || card.w<=0 || card.h<=0)return {};
    auto edge=[](int start,int extent,std::int64_t coordinate,int original) {
        const double value=start+static_cast<double>(extent)*static_cast<double>(coordinate)/original;
        const double limit=static_cast<double>((std::numeric_limits<int>::max)()/4);
        return static_cast<int>(std::lround(std::clamp(value,-limit,limit)));
    };
    const int x=edge(card.x,card.w,source.x,style.width),y=edge(card.y,card.h,source.y,style.height);
    const int right=edge(card.x,card.w,static_cast<std::int64_t>(source.x)+source.w,style.width);
    const int bottom=edge(card.y,card.h,static_cast<std::int64_t>(source.y)+source.h,style.height);
    return {x,y,right-x,bottom-y};
}
void ThemeFill(void* surface,const Rect& rect,const Rect& clip,Color color) {
    color=BoundedColor(color);
    if(color.a>0)Fill(surface,Intersection(rect,clip),color);
}
bool TextureReady(const ThemeTexture& texture) {
    return texture.id>0 && std::isfinite(texture.u0) && std::isfinite(texture.v0) &&
        std::isfinite(texture.u1) && std::isfinite(texture.v1) &&
        texture.u0>=0 && texture.v0>=0 && texture.u1<=1 && texture.v1<=1 &&
        texture.u1>texture.u0 && texture.v1>texture.v0;
}
struct ThemeTextureValidation {
    std::string material;
    int width=0,height=0;
    bool nameValid=false,sizeQueried=false,valid=false;
};
thread_local std::unordered_map<int,ThemeTextureValidation> g_themeTextureValidation;
void InvalidateModelTextureValidation(int id) {g_themeTextureValidation.erase(id);}
bool BorrowedTextureReady(void* surface,const ThemeTexture& texture) {
    // Portraits are owned by the existing character-based loading path. Only
    // loaded assets copied from the game's HUD have a material identity here.
    if(texture.material.empty())return true;
    if(texture.material.size()>=512 || texture.material.find('\0')!=std::string::npos) {
        ++g_runtimeCounters.themeTextureMisses;return false;
    }
    auto inserted=g_themeTextureValidation.try_emplace(texture.id);
    auto& value=inserted.first->second;
    if(inserted.second) {
        ++g_runtimeCounters.themeTextureQueries;
        char material[512]{};
        bool found=false;
        if(ModelCall(surface,26,found,texture.id,material,static_cast<int>(sizeof(material)))&&found &&
           material[0] && material[sizeof(material)-1]==0) {
            value.material=material;value.nameValid=true;
        }
    }
    // Keep the actual identity independent from each caller's expectation: a
    // recycled ID may be requested with both an old and a new material name.
    if(!value.nameValid || value.material!=texture.material) {
        ++g_runtimeCounters.themeTextureMisses;return false;
    }
    if(!value.sizeQueried) {
        value.sizeQueried=true;
        value.valid=ModelCallVoid(surface,30,texture.id,&value.width,&value.height)&&
            value.width>0 && value.height>0 && value.width<=32768 && value.height<=32768;
    }
    if(!value.valid)++g_runtimeCounters.themeTextureMisses;
    return value.valid;
}
bool ThemeTextureRect(void* surface,const ThemeTexture& texture,const Rect& rect,const Rect& clip) {
    if(!TextureReady(texture)) {
        if(!texture.material.empty())++g_runtimeCounters.themeTextureMisses;
        return false;
    }
    const Rect visible=Intersection(rect,clip);
    const auto tint=BoundedColor(texture.tint);
    if(visible.w<=0 || visible.h<=0 || tint.a==0)return true;
    if(!BorrowedTextureReady(surface,texture))return false;
    const float du=texture.u1-texture.u0,dv=texture.v1-texture.v0;
    const float u0=texture.u0+du*static_cast<float>(visible.x-rect.x)/rect.w;
    const float v0=texture.v0+dv*static_cast<float>(visible.y-rect.y)/rect.h;
    const float u1=texture.u0+du*static_cast<float>(visible.x+visible.w-rect.x)/rect.w;
    const float v1=texture.v0+dv*static_cast<float>(visible.y+visible.h-rect.y)/rect.h;
    if(!std::isfinite(u0)||!std::isfinite(v0)||!std::isfinite(u1)||!std::isfinite(v1))return false;
    SetColor(surface,tint);Call<void>(surface,29,texture.id);
    // Exact Surface031 slot98: four int coordinates + four float UVs, ret32.
    Call<void,int,int,int,int,float,float,float,float>(surface,98,
        visible.x,visible.y,visible.x+visible.w,visible.y+visible.h,u0,v0,u1,v1);
    return true;
}
bool ThemeText(void* surface,const ThemeWidget& widget,const GameHudCardStyle& style,const Rect& card,
               unsigned long fallback,unsigned long small,std::wstring text,Color color,bool truncate) {
    if(!widget.present || text.empty())return false;
    const Rect rect=Project(widget.rect,style,card),visible=Intersection(rect,card);
    if(visible.w<=0 || visible.h<=0)return false;
    // A borrowed font keeps its original pixel size. It may extend beyond the
    // scaled label rectangle, but never beyond this extra card's boundary.
    auto inset=[](int original,int extent,int sourceExtent) {
        const double limit=static_cast<double>((std::numeric_limits<int>::max)()/4);
        return static_cast<int>(std::lround(std::clamp(
            static_cast<double>(original)*extent/sourceExtent,-limit,limit)));
    };
    const int insetX=inset(widget.insetX,card.w,style.width),insetY=inset(widget.insetY,card.h,style.height);
    const int alignment=std::clamp(widget.alignment,0,8),column=alignment%3,row=alignment/3;
    unsigned long font=widget.font?widget.font:fallback;
    int width=0,height=0,x=0,y=0;
    auto measure=[&]() {
        if(!font)return false;
        width=height=0;
        Call<void,unsigned long,const wchar_t*,int&,int&>(surface,72,font,text.c_str(),width,height);
        if(width<0 || height<=0 || width>rect.w)return false;
        const std::int64_t offsetX=column==0?insetX:column==1?(rect.w-width)/2:
            static_cast<std::int64_t>(rect.w)-width-insetX;
        const std::int64_t offsetY=(row==0?0:row==1?(rect.h-height)/2:
            static_cast<std::int64_t>(rect.h)-height)+insetY;
        const auto left=static_cast<std::int64_t>(rect.x)+offsetX,top=static_cast<std::int64_t>(rect.y)+offsetY;
        if(left<card.x || top<card.y || left+width>static_cast<std::int64_t>(card.x)+card.w ||
           top+height>static_cast<std::int64_t>(card.y)+card.h)return false;
        x=static_cast<int>(left);y=static_cast<int>(top);return true;
    };
    bool fits=measure();
    if(!fits && small && font!=small) {font=small;fits=measure();}
    while(!fits && truncate && text.size()>1) {
        text.pop_back();
        if(!text.empty() && text.back()>=0xD800 && text.back()<=0xDBFF)text.pop_back();
        if(text.empty())break;
        fits=measure();
    }
    if(!fits || text.empty())return false;
    color=BoundedColor(color);
    if(color.a>0)Text(surface,font,x,y,color,std::move(text));
    return true;
}
void DeadLabelFallback(void* surface,const ThemeWidget& widget,const GameHudCardStyle& style,const Rect& card,
                       unsigned long small) {
    const auto name=Intersection(Project(style.name.rect,style,card),card);
    auto color=BoundedColor(widget.color);
    if(color.a==0)color={232,236,227,255};
    for(const auto font:{widget.font?widget.font:small,small}) {
        if(!font)continue;
        int width=0,height=0;
        Call<void,unsigned long,const wchar_t*,int&,int&>(surface,72,font,L"DEAD",width,height);
        if(width<=0 || height<=0 || width>card.w || height>card.h)continue;
        const int x=std::clamp(name.w>0?name.x:card.x,card.x,card.x+card.w-width);
        const int above=name.y-height-2,below=name.y+name.h+2;
        const int y=name.h>0 && above>=card.y?above:
            name.h>0 && below<=card.y+card.h-height?below:card.y+card.h-height;
        Text(surface,font,x,y,color,L"DEAD");
        return;
    }
}
float TemporaryPart(const Player& player) {
    return player.alive && !player.incapacitated && !player.hanging && std::isfinite(player.temporaryHealth)?
        std::clamp(player.temporaryHealth,0.0f,static_cast<float>(std::max(0,player.health))):0.0f;
}
int HealthBand(const Player& player,const ThemeHealthBar& bar) {
    const float permanent=static_cast<float>(std::max(0,player.health))-TemporaryPart(player);
    if(player.incapacitated || player.hanging || (player.blackWhite && permanent<=1))return 0;
    const float ratio=permanent/static_cast<float>(std::max(1,player.maxHealth));
    const float low=std::isfinite(bar.lowThreshold)?std::clamp(bar.lowThreshold,0.0f,1.0f):0.25f;
    const float medium=std::isfinite(bar.mediumThreshold)?std::clamp(bar.mediumThreshold,low,1.0f):0.4f;
    return ratio<low?1:ratio<medium?2:3;
}
Color NumberColor(const Player& player,const GameHudTheme& theme,const ThemeHealthBar& bar) {
    const int band=player.blackWhite?3:std::max(0,HealthBand(player,bar)-1);
    return BoundedColor(theme.numberPalette[static_cast<std::size_t>(band)]);
}
void HealthSegment(void* surface,const ThemeTexture& original,const Rect& rect,const Rect& clip,
                   float from,float to,Color fallback) {
    from=std::clamp(from,0.0f,1.0f);to=std::clamp(to,from,1.0f);
    const int left=rect.x+static_cast<int>(rect.w*from),right=rect.x+static_cast<int>(rect.w*to);
    if(rect.w<=0 || rect.h<=0 || right<=left)return;
    const Rect segment{left,rect.y,right-left,rect.h};
    auto texture=original;
    const float du=original.u1-original.u0;
    texture.u0=original.u0+du*from;texture.u1=original.u0+du*to;
    if(!ThemeTextureRect(surface,texture,segment,clip))ThemeFill(surface,segment,clip,fallback);
}
ThemeTexture Tinted(ThemeTexture texture,Color color) {
    color=BoundedColor(color);const auto source=BoundedColor(texture.tint);
    texture.tint={source.r*color.r/255,source.g*color.g/255,source.b*color.b/255,source.a*color.a/255};
    return texture;
}
void LiveHealth(void* surface,const Player& player,const GameHudCardStyle& style,const Rect& card) {
    if(!player.alive || !style.health.present)return;
    const Rect rect=Project(style.health.rect,style,card);
    if(rect.w<=0 || rect.h<=0)return;
    const auto& bar=style.bar;
    const float temporary=TemporaryPart(player),total=static_cast<float>(std::max(0,player.health));
    const float permanent=total-temporary;
    const bool down=player.incapacitated || player.hanging;
    const float maximum=down?300.0f:static_cast<float>(std::max(1,player.maxHealth));
    const float solid=std::clamp(permanent/maximum,0.0f,1.0f);
    const float combined=std::clamp(total/maximum,0.0f,1.0f);
    const int band=HealthBand(player,bar);
    const Color color=bar.monochrome?bar.monochromeColor:bar.palette[static_cast<std::size_t>(band)];
    if(bar.present && bar.newMaterial) {
        const auto index=static_cast<std::size_t>(bar.monochrome?4:band);
        if(down)HealthSegment(surface,bar.ticks[index],rect,card,0,combined,color);
        else {
            // The loaded control paints ticks first and solid health last.
            if(temporary>0)HealthSegment(surface,bar.ticks[index],rect,card,solid,combined,color);
            if(permanent>0)HealthSegment(surface,bar.fill[index],rect,card,0,solid,color);
        }
        return;
    }
    if(!ThemeTextureRect(surface,bar.grey,rect,card))ThemeFill(surface,rect,card,{45,49,41,255});
    if(permanent>0)ThemeTextureRect(surface,Tinted(bar.outline,color),rect,card);
    auto source=style.health.rect;
    // A resized legacy control can retain the original inset. Keep some
    // interior before scaling, then retain one pixel if edge rounding erases it.
    const int inset=std::clamp(bar.inset,0,(std::min(source.w,source.h)-1)/2);
    source.x+=inset;source.y+=inset;source.w-=inset*2;source.h-=inset*2;
    Rect inside=Project(source,style,card);
    if(inside.w<=0) {inside.x=std::clamp(inside.x,rect.x,rect.x+rect.w-1);inside.w=1;}
    if(inside.h<=0) {inside.y=std::clamp(inside.y,rect.y,rect.y+rect.h-1);inside.h=1;}
    const float from=down?0:solid,to=combined;
    const int first=inside.x+static_cast<int>(inside.w*from),last=inside.x+static_cast<int>(inside.w*to);
    if(down || temporary>0)for(int x=first;x<last;x+=4)
        ThemeFill(surface,{x,inside.y,std::min(2,last-x),inside.h},card,color);
    if(!down && permanent>0)HealthSegment(surface,Tinted(bar.white,color),inside,card,0,solid,color);
}
int HeldItem(const Player& player,int kind) {
    return kind==0?player.throwable:kind==1?player.medical:player.pills;
}
bool HasItemRoom(const Rect& rect) {return rect.w>=8 && rect.h>=8;}
bool DrawResolvedItem(void* surface,ItemIconLookup lookup,int item,const Rect& box,const Rect& clip,Color ink) {
    if(!lookup || item<=0)return false;
    const Rect visible=Intersection(box,clip);
    if(!HasItemRoom(visible))return false;
    const auto* icon=lookup(item);
    if(!icon || icon->item!=item || icon->width<=0 || icon->height<=0)return false;
    if(icon->usingFont) {
        // CHudTexture stores the game's measured glyph size. The glyph only
        // has the intended meaning in its own font and cannot be scaled.
        if(!icon->font || !icon->glyph || icon->width>visible.w || icon->height>visible.h)return false;
        Text(surface,icon->font,visible.x+(visible.w-icon->width)/2,visible.y+(visible.h-icon->height)/2,
             ink,std::wstring(1,icon->glyph));
    } else {
        const double scale=std::min(static_cast<double>(visible.w)/icon->width,
                                    static_cast<double>(visible.h)/icon->height);
        const int width=std::clamp(static_cast<int>(icon->width*scale),1,visible.w);
        const int height=std::clamp(static_cast<int>(icon->height*scale),1,visible.h);
        const Rect rect{visible.x+(visible.w-width)/2,visible.y+(visible.h-height)/2,width,height};
        auto texture=icon->texture;texture.tint=ink;
        if(!ThemeTextureRect(surface,texture,rect,clip))return false;
    }
    ++g_runtimeCounters.itemIconRecoveries;
    return true;
}
struct LiveItemWidget {ThemeWidget widget;bool inferred=false;};
LiveItemWidget PrepareItemWidget(int kind,const GameHudCardStyle& style,const Rect& card) {
    LiveItemWidget result{style.items[static_cast<std::size_t>(kind)],false};
    if(result.widget.present && HasItemRoom(Intersection(Project(result.widget.rect,style,card),card)))return result;
    result.inferred=true;
    std::array<int,2> known{};int count=0;
    for(int i=0;i<3;++i) {
        const auto& candidate=style.items[static_cast<std::size_t>(i)];
        if(i!=kind && candidate.present && HasItemRoom(Intersection(Project(candidate.rect,style,card),card)))
            known[static_cast<std::size_t>(count++)]=i;
    }
    // Item controls can be absent when that native survivor owns no item.
    // Recover their row/column spacing from other kinds before using a generic
    // card-local inventory area. Do not reuse another kind's texture or data.
    if(count) {
        const int reference=known[0];
        result.widget=style.items[static_cast<std::size_t>(reference)];
        const auto origin=result.widget.rect;
        const double stepX=count==2?
            (static_cast<double>(style.items[static_cast<std::size_t>(known[1])].rect.x)-origin.x)/(known[1]-reference):
            static_cast<double>(origin.w)+std::max(1,origin.w/6);
        const double stepY=count==2?
            (static_cast<double>(style.items[static_cast<std::size_t>(known[1])].rect.y)-origin.y)/(known[1]-reference):0.0;
        auto coordinate=[](double value) {
            const double limit=static_cast<double>((std::numeric_limits<int>::max)()/4);
            return static_cast<int>(std::lround(std::clamp(value,-limit,limit)));
        };
        result.widget.rect.x=coordinate(origin.x+stepX*(kind-reference));
        result.widget.rect.y=coordinate(origin.y+stepY*(kind-reference));
        bool useful=HasItemRoom(Intersection(Project(result.widget.rect,style,card),card)) &&
            (std::abs(stepX)>=1 || std::abs(stepY)>=1);
        if(!useful && count==1) {
            // A single control near the right edge may belong to a column.
            result.widget.rect=origin;
            result.widget.rect.y=coordinate(origin.y+(origin.h+std::max(1.0,origin.h/6.0))*(kind-reference));
            useful=HasItemRoom(Intersection(Project(result.widget.rect,style,card),card));
        }
        if(!useful)result.widget.rect={};
    } else result.widget.rect={};
    result.widget.present=true;
    return result;
}
Rect SafeItemRect(const Rect& card,int kind) {
    if(card.w<8 || card.h<8)return {};
    const int pad=std::min(std::max(2,card.h/16),std::max(0,(card.w-28)/2));
    const int gap=2;
    const int size=std::min({12,std::max(8,card.h/5),(card.w-pad*2-gap*2)/3,card.h});
    if(size<8) {
        const int y=card.h>=28?card.y+kind*10:card.y;
        return {card.x+card.w-8,y,8,8};
    }
    const int first=card.x+card.w-pad-(size*3+gap*2);
    const int footer=std::max(12,card.h/4),portrait=std::max(12,card.h-footer-pad*2);
    const int barY=card.y+portrait+pad-std::max(5,card.h/7);
    const int y=std::clamp(barY-size-2,card.y,card.y+card.h-size);
    return {first+kind*(size+gap),y,size,size};
}
Rect ThemedItemRect(const GameHudCardStyle& style,const Rect& card,int kind) {
    if(!style.health.present)return {};
    const auto bar=Intersection(Project(style.health.rect,style,card),card);
    const int gap=std::max(2,card.h/48);
    const int size=std::min({card.h/5,(bar.w/2-gap*3)/3,bar.y-card.y-gap});
    if(size<8)return {};
    // Reserve the other half above the bar for the health number.
    const bool numberOnRight=style.number.present && style.number.alignment%3==2;
    const int rowWidth=size*3+gap*2;
    const int first=numberOnRight?bar.x+gap:bar.x+bar.w-gap-rowWidth;
    return {first+kind*(size+gap),bar.y-gap-size,size,size};
}
struct ItemFallback {Rect art,symbol;int kind=0,item=0;Color ink;};
void LiveItem(void* surface,const Player& player,int kind,const LiveItemWidget& prepared,
              const GameHudCardStyle& style,const Rect& card,const GameHudTheme& theme,
              std::vector<ItemFallback>& fallbacks) {
    const int item=HeldItem(player,kind);
    if(item<=0)return;
    const auto& widget=prepared.widget;
    Rect box=Project(widget.rect,style,card);
    const bool safeArea=!HasItemRoom(Intersection(box,card));
    const Rect symbolBox=safeArea?SafeItemRect(card,kind):box;
    if(safeArea) {
        box=ThemedItemRect(style,card,kind);
        if(!HasItemRoom(box))box=symbolBox;
    }
    if(!HasItemRoom(box))return;
    const auto found=std::find_if(theme.icons.begin(),theme.icons.end(),
        [&](const ThemeIcon& icon){return icon.kind==kind && icon.item==item;});
    if(found!=theme.icons.end()) {
        if(!found->usingFont && BoundedColor(found->color).a>0) {
            auto texture=found->texture;texture.tint=found->color;
            Rect rect=box;
            if(!safeArea && !found->scale && found->width>0 && found->height>0) {
                auto source=widget.rect;source.w=found->width;source.h=found->height;
                rect=Project(source,style,card);
            }
            if(HasItemRoom(Intersection(rect,card)) && ThemeTextureRect(surface,texture,rect,card)) {
                if(prepared.inferred || safeArea)++g_runtimeCounters.themeItemFallbacks;
                return;
            }
        }
        // No speculative font-icon entry points: unsupported glyphs use the
        // existing item symbol and keep the extra player's actual item kind.
    }
    const auto visible=Intersection(symbolBox,card);
    const int size=std::min(visible.w,visible.h);
    auto ink=BoundedColor(found==theme.icons.end()?widget.color:found->color);
    // A hidden/empty source slot must not hide an item actually held by this
    // extra survivor. Keep native artwork when usable; otherwise draw only a
    // readable opaque fallback, after native decorations cannot cover it.
    if(ink.r+ink.g+ink.b<96)ink={232,236,227,255};
    ink.a=255;
    fallbacks.push_back({box,{visible.x,visible.y,size,size},kind,item,ink});
}
void LiveCard(void* surface,unsigned long font,unsigned long small,const Player& player,const Rect& card,
              const GameHudCardStyle& style,const GameHudTheme& theme,ItemIconLookup lookupItem) {
    enum class Kind {Image,Portrait,Name,Number,Status,Health,HealthIcon,Item};
    struct Entry {Kind kind;const ThemeWidget* widget;std::size_t index;};
    std::vector<Entry> entries;entries.reserve(style.images.size()+9);
    auto add=[&](Kind kind,const ThemeWidget& widget,std::size_t index=0) {
        if(widget.present)entries.push_back({kind,&widget,index});
    };
    for(std::size_t i=0;i<style.images.size();++i)add(Kind::Image,style.images[i].widget,i);
    add(Kind::Portrait,style.portrait);add(Kind::Name,style.name);add(Kind::Number,style.number);
    add(Kind::Status,style.status);add(Kind::Health,style.health);add(Kind::HealthIcon,style.healthIcon);
    std::array<LiveItemWidget,3> items;
    for(std::size_t i=0;i<items.size();++i)if(HeldItem(player,static_cast<int>(i))>0) {
        items[i]=PrepareItemWidget(static_cast<int>(i),style,card);
        add(Kind::Item,items[i].widget,i);
    }
    std::vector<ItemFallback> itemFallbacks;itemFallbacks.reserve(3);
    std::stable_sort(entries.begin(),entries.end(),[](const Entry& a,const Entry& b) {
        return a.widget->z!=b.widget->z?a.widget->z<b.widget->z:a.widget->order<b.widget->order;
    });
    const bool normalState=ThemeStateIndex(player)==0;
    const auto healthColor=normalState?NumberColor(player,theme,style.bar):style.number.color;
    for(const auto& entry:entries) {
        const auto& widget=*entry.widget;
        if(entry.kind==Kind::Image) {
            const auto& image=style.images[entry.index];const Rect rect=Project(widget.rect,style,card);
            ThemeFill(surface,rect,card,image.fill);
            if(image.characterIncap) {
                bool model=false;const int id=PlayerPortrait(surface,player,true,model);
                if(id>0) {
                    auto texture=image.texture;texture.id=id;
                    texture.material=model?player.modelPortrait.incapMaterial:std::string();
                    const bool painted=ThemeTextureRect(surface,texture,rect,card);
                    if(painted&&model&&Intersection(rect,card).w>0&&Intersection(rect,card).h>0&&BoundedColor(texture.tint).a>0)
                        ++g_runtimeCounters.modelPortraitDraws;
                }
            } else ThemeTextureRect(surface,image.texture,rect,card);
        } else if(entry.kind==Kind::Portrait) {
            bool model=false;const int id=PlayerPortrait(surface,player,false,model);
            if(id>0) {ThemeTexture texture;texture.id=id;texture.tint=widget.color;
                texture.material=model?player.modelPortrait.normalMaterial:std::string();
                const auto rect=Project(widget.rect,style,card);
                if(ThemeTextureRect(surface,texture,rect,card)&&model&&
                   Intersection(rect,card).w>0&&Intersection(rect,card).h>0&&BoundedColor(texture.tint).a>0)
                    ++g_runtimeCounters.modelPortraitDraws;}
            else {auto text=Wide(player.name);
                ThemeText(surface,widget,style,card,font,small,text.empty()?L"?":text.substr(0,1),widget.color,false);}
        } else if(entry.kind==Kind::Name) {
            ThemeText(surface,widget,style,card,small,small,Wide(player.name),widget.color,true);
        } else if(entry.kind==Kind::Number) {
            const auto number=std::to_wstring(std::max(0,player.health));
            const auto text=!player.alive?L"DEAD":!style.status.present && player.hanging?L"HANG "+number:
                !style.status.present && player.incapacitated?L"DOWN "+number:
                style.healthIcon.present && !style.healthGlyph.empty()?number:L"+"+number;
            if(!ThemeText(surface,widget,style,card,font,small,text,player.alive?healthColor:widget.color,false)) {
                if(!player.alive)DeadLabelFallback(surface,widget,style,card,small);
                else if(player.hanging || player.incapacitated)
                    ThemeText(surface,widget,style,card,font,small,number,healthColor,false);
            }
        } else if(entry.kind==Kind::Status) {
            std::wstring text;
            if(!player.alive)text=L"DEAD";
            else if(player.hanging)text=L"HANG "+std::to_wstring(std::max(0,player.health));
            else if(player.incapacitated)text=L"DOWN "+std::to_wstring(std::max(0,player.health));
            if(!ThemeText(surface,widget,style,card,small,small,text,widget.color,false) && !text.empty()) {
                if(!player.alive)DeadLabelFallback(surface,widget,style,card,small);
                else ThemeText(surface,widget,style,card,small,small,std::to_wstring(std::max(0,player.health)),widget.color,false);
            }
        } else if(entry.kind==Kind::Health)LiveHealth(surface,player,style,card);
        else if(entry.kind==Kind::HealthIcon) {
            if(player.alive)ThemeText(surface,widget,style,card,font,small,style.healthGlyph,
                normalState?healthColor:widget.color,false);
        } else LiveItem(surface,player,static_cast<int>(entry.index),items[entry.index],style,card,theme,itemFallbacks);
    }
    for(const auto& item:itemFallbacks) {
        if(DrawResolvedItem(surface,lookupItem,item.item,item.art,card,item.ink))continue;
        ItemSymbol(surface,item.symbol,item.kind,item.item,item.ink);
        ++g_runtimeCounters.themeItemFallbacks;
    }
}
bool SizedStyle(const GameHudCardStyle& style) {
    return style.ready && style.width>0 && style.height>0;
}
bool NormalCanAdapt(const GameHudCardStyle& style) {
    auto usable=[](const ThemeWidget& widget){return widget.present && widget.rect.w>0 && widget.rect.h>0;};
    return SizedStyle(style) && usable(style.portrait) && usable(style.name) && usable(style.number) && usable(style.health);
}
void AddDeadStatus(GameHudCardStyle& style,const GameHudCardStyle& normal) {
    if(style.number.present || style.status.present)return;
    if(normal.number.present && normal.number.rect.w>0 && normal.number.rect.h>0 && normal.width>0 && normal.height>0) {
        style.status=normal.number;
        style.status.rect=Project(normal.number.rect,normal,{0,0,style.width,style.height});
    } else {
        style.status=style.name;
        const auto& name=style.name.rect;
        const int height=std::clamp(name.h,1,style.height);
        const int y=name.y>=height?name.y-height:
            static_cast<std::int64_t>(name.y)+height*2LL<=style.height?name.y+height:std::max(0,style.height-height);
        style.status.rect={std::clamp(name.x,0,std::max(0,style.width-1)),y,style.width,height};
        style.status.rect.w=style.width-style.status.rect.x;
        style.status.alignment=0;style.status.insetX=style.status.insetY=0;
    }
    style.status.present=true;style.status.color=BoundedColor(style.name.color);
    if(style.status.color.a==0)style.status.color={232,236,227,255};
    style.status.z=(std::numeric_limits<int>::max)();
}
bool GameHudCard(void* surface,unsigned long font,unsigned long small,const Player& player,const Rect& card,
                 const GameHudTheme& theme,ItemIconLookup lookupItem) {
    if(!theme.available)return false;
    const auto state=ThemeStateIndex(player);
    const auto& normal=theme.states[0];
    const auto* selected=&theme.states[state];
    if((state==1 || state==2) && SizedStyle(theme.preparedIncap))selected=&theme.preparedIncap;
    if(state==3 && !SizedStyle(*selected) && SizedStyle(theme.preparedDead))selected=&theme.preparedDead;
    GameHudCardStyle owned;
    if(!SizedStyle(*selected)) {
        if(state==0 || !NormalCanAdapt(normal))return false;
        owned=normal;selected=&owned;
        ++g_runtimeCounters.themeAdaptedStateCards;
    }
    // Dead resources may contain only a name and death artwork. Add the actual
    // extra survivor's status to copied geometry without modifying that source.
    if(state==3 && !selected->number.present && !selected->status.present) {
        if(selected!=&owned) {owned=*selected;selected=&owned;}
        AddDeadStatus(owned,normal);
    }
    LiveCard(surface,font,small,player,card,*selected,theme,lookupItem);
    return true;
}
} // namespace

void Renderer::Initialize(void* surface,void* materials) {
    if(surface_!=surface||g_modelSurface!=surface||g_modelMaterials!=materials) {
        g_modelPortraits.clear();g_modelPortraitUse=0;g_modelSurface=surface;
    }
    g_modelMaterials=materials;
    if (surface_!=surface) {
        surface_=surface; font_=0; smallFont_=0; fontHeight_=0;
        g_portraits.fill(0);g_incapPortraits.fill(0);g_textureSurface=surface;
    }
}
bool Renderer::Draw(const Snapshot& snapshot,const Layout& layout,const Settings& settings,const GameHudTheme* theme,
                    ItemIconLookup lookupItem) {
    // Begin unconditionally: a previous outer SEH handler may have bypassed
    // C++ cleanup, and a hidden or failed draw must not retain page metadata.
    ModelMemoryQueryScope modelMemoryQueries;
    if (!surface_ || layout.cards.empty() || layout.suppressed) return false;
    g_themeTextureValidation.clear();
    const int fontHeight=std::clamp(layout.cards.front().bounds.h/3,12,22);
    if (!font_ || !smallFont_ || fontHeight_!=fontHeight) {
        font_=Call<unsigned long>(surface_,63);
        smallFont_=Call<unsigned long>(surface_,63);
        if (!font_ || !smallFont_) return false;
        const int flags=0x010|0x080; // antialias + drop shadow
        const bool normal=Call<bool,unsigned long,const char*,int,int,int,int,int,int,int>(
            surface_,64,font_,"Segoe UI",fontHeight,700,0,0,flags,0,0);
        const bool small=Call<bool,unsigned long,const char*,int,int,int,int,int,int,int>(
            surface_,64,smallFont_,"Segoe UI",std::max(11,fontHeight*3/4),500,0,0,flags,0,0);
        if (!normal || !small) { font_=0; smallFont_=0; return false; }
        fontHeight_=fontHeight;
    }
    bool submitted=false;
    for (const auto& placement:layout.cards) {
        const auto it=std::find_if(snapshot.players.begin(),snapshot.players.end(),
            [&](const Player& p){ return p.index==placement.playerIndex; });
        if (it!=snapshot.players.end()) {
            g_drawTrace.playerIndex=it->index;
            const bool selected=settings.theme==HudTheme::GameHud;
            if(!(selected && theme && GameHudCard(surface_,font_,smallFont_,*it,placement.bounds,*theme,lookupItem))) {
                if(selected)++g_runtimeCounters.themeFallbackCards;
                CurrentCard(surface_,font_,smallFont_,*it,placement.bounds,selected?lookupItem:nullptr);
            }
            submitted=true;
        }
    }
    if(submitted)Call<void>(surface_,24); // flush batched glyphs before root restores context
    return submitted;
}
} // namespace neh
