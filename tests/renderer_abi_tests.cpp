#include "types.hpp"
#include "game_hud_theme.hpp"
#include "draw_trace.hpp"
#include "runtime_counters.hpp"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#undef CreateFont
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <iostream>
#include <limits>
#include <map>
#include <new>
#include <set>
#include <cmath>
#include <string>
#include <vector>

namespace {
int checks=0;
void Check(bool condition,const char* message) {
    if (!condition) { std::cerr<<"FAIL: "<<message<<'\n'; std::exit(1); }
    ++checks;
}
enum class TextureMode { Create,Existing,Missing };
struct FillRecord { neh::Rect rect;std::array<int,4> color; };
struct TextRecord { std::wstring text;std::array<int,4> color;int x=0,y=0;unsigned long font=0; };
struct TextureRecord { int id;neh::Rect rect;std::array<float,4> uv;std::array<int,4> color; };
struct Surface {
    void** vtable=nullptr;
    std::array<void*,100> methods{};
    TextureMode textureMode=TextureMode::Create;
    bool invalidArguments=false,rejectGlyphs=false;
    int obsoleteCalls=0,fillCalls=0,borderCalls=0,textColorCalls=0,textSizeCalls=0;
    int flushCalls=0,fontCalls=0,glyphCalls=0,textureLookups=0,textureCreates=0;
    int textureFiles=0,textureSizes=0,textureValidations=0,textureDraws=0;
    int selectedTexture=0;
    unsigned long currentFont=0,nextFont=1;
    std::array<int,4> currentColor{},currentTextColor{};
    int textX=0,textY=0;
    std::map<unsigned long,int> fonts;
    std::vector<std::array<int,4>> colors;
    std::vector<std::wstring> printed;
    std::vector<FillRecord> fills;
    std::vector<TextRecord> textDraws;
    std::vector<std::string> texturePaths;
    std::vector<int> texturedDrawIds;
    std::set<int> borrowedTextures;
    std::map<int,std::string> borrowedMaterials;
    std::map<int,std::pair<int,int>> borrowedSizes;
    std::map<int,int> materialQueries,sizeQueries;
    std::map<std::string,int> existingByPath,lookupsByPath;
    bool throwModelLookup=false,throwModelUpload=false,throwMaterialQuery=false;
    bool replaceSizeMethodOnQuery=false;
    int replacementSizeCalls=0;
    int throwSizeId=0;
    std::vector<TextureRecord> textureDrawsWithUv;
};
void __fastcall ReplacementTextureSize(void*,void*,int,int*,int*);
struct FakeMaterial;
struct FakeMaterialSystem;
bool __fastcall IsErrorMaterial(FakeMaterial*,void*);
void* __fastcall FindMaterial(FakeMaterialSystem*,void*,const char*,const char*,bool,const char*);
struct FakeMaterial {
    void** vtable;
    std::array<void*,43> methods{};
    bool error=true,throwCheck=false;
    void* revokePageAfterCheck=nullptr;
    int checks=0;
    FakeMaterial() : vtable(methods.data()) {methods[42]=reinterpret_cast<void*>(&IsErrorMaterial);}
};
struct FakeMaterialSystem {
    void** vtable;
    std::array<void*,72> methods{};
    FakeMaterial material;
    std::set<std::string> available;
    std::map<std::string,int> lookups;
    bool invalidArguments=false,throwFind=false,invalidReturn=false;
    void* returnedMaterial=nullptr;
    FakeMaterialSystem() : vtable(methods.data()) {methods[71]=reinterpret_cast<void*>(&FindMaterial);}
};
bool __fastcall IsErrorMaterial(FakeMaterial* self,void*) {
    ++self->checks;if(self->throwCheck)RaiseException(0xE0010005,0,0,nullptr);
    const bool error=self->error;
    if(self->revokePageAfterCheck) {
        DWORD previous=0;VirtualProtect(self->revokePageAfterCheck,4096,PAGE_NOACCESS,&previous);
    }
    return error;
}
void* __fastcall FindMaterial(FakeMaterialSystem* self,void*,const char* name,const char* group,bool complain,const char* prefix) {
    self->invalidArguments|=!name||!group||std::strcmp(group,"VGUI textures")!=0||complain||prefix!=nullptr;
    if(!name)return nullptr;
    ++self->lookups[name];
    if(self->throwFind)RaiseException(0xE0010006,0,0,nullptr);
    if(self->invalidReturn)return reinterpret_cast<void*>(1);
    self->material.error=self->available.count(name)==0;
    return self->returnedMaterial?self->returnedMaterial:&self->material;
}
Surface& S(void* self) { return *static_cast<Surface*>(self); }
bool Channels(int r,int g,int b,int a) {
    return r>=0&&r<=255&&g>=0&&g<=255&&b>=0&&b<=255&&a>=0&&a<=255;
}
void __fastcall SetColor(void* self,void*,int r,int g,int b,int a) {
    auto& s=S(self); s.invalidArguments|=!Channels(r,g,b,a);
    s.currentColor={r,g,b,a};
    s.colors.push_back({r,g,b,a});
}
void __fastcall SetTextColor(void* self,void*,int r,int g,int b,int a) {
    auto& s=S(self); s.invalidArguments|=!Channels(r,g,b,a); ++s.textColorCalls;
    s.currentTextColor={r,g,b,a};
}
void __fastcall Fill(void* self,void*,int x,int y,int right,int bottom) {
    auto& s=S(self); s.invalidArguments|=right<=x||bottom<=y; ++s.fillCalls;
    s.fills.push_back({{x,y,right-x,bottom-y},s.currentColor});
}
void __fastcall Border(void* self,void*,int x,int y,int right,int bottom) {
    auto& s=S(self); s.invalidArguments|=right<=x||bottom<=y; ++s.borderCalls;
}
void __fastcall SetFont(void* self,void*,unsigned long font) {
    auto& s=S(self); s.invalidArguments|=s.fonts.count(font)==0; s.currentFont=font;
}
void __fastcall SetPosition(void* self,void*,int x,int y) { S(self).textX=x;S(self).textY=y; }
void __fastcall Print(void* self,void*,const wchar_t* text,int length,int drawType) {
    auto& s=S(self);
    if (!text||length<0||length>2048||drawType!=0||!s.currentFont) { s.invalidArguments=true; return; }
    s.printed.emplace_back(text,static_cast<std::size_t>(length));
    s.textDraws.push_back({s.printed.back(),s.currentTextColor,s.textX,s.textY,s.currentFont});
}
void __fastcall Flush(void* self,void*) { ++S(self).flushCalls; }
unsigned long __fastcall CreateFont(void* self,void*) {
    auto& s=S(self); ++s.fontCalls; return s.nextFont++;
}
bool __fastcall SetGlyphs(void* self,void*,unsigned long font,const char* family,
                         int tall,int weight,int blur,int scanlines,int flags,int low,int high) {
    auto& s=S(self); ++s.glyphCalls;
    s.invalidArguments|=!family||std::strcmp(family,"Segoe UI")!=0||tall<1||weight<1||
                        blur!=0||scanlines!=0||flags!=0x090||low!=0||high!=0;
    s.fonts[font]=tall;
    return !s.rejectGlyphs;
}
void __fastcall TextSize(void* self,void*,unsigned long font,const wchar_t* text,int* width,int* height) {
    auto& s=S(self); ++s.textSizeCalls;
    if (!text||!width||!height||s.fonts.count(font)==0) { s.invalidArguments=true; return; }
    *height=s.fonts[font];
    *width=static_cast<int>(std::wcslen(text))*(*height)/2;
}
int __fastcall TextureId(void* self,void*,const char* path) {
    auto& s=S(self); ++s.textureLookups;
    if (!path) { s.invalidArguments=true; return 0; }
    s.texturePaths.emplace_back(path);
    ++s.lookupsByPath[path];
    if(std::strncmp(path,"vgui/l4n_s/",11)==0) {
        if(s.throwModelLookup)RaiseException(0xE0010001,0,0,nullptr);
        const auto existing=s.existingByPath.find(path);
        return existing==s.existingByPath.end()?0:existing->second;
    }
    return s.textureMode==TextureMode::Existing?301:0;
}
int __fastcall CreateTexture(void* self,void*,bool procedural) {
    auto& s=S(self); ++s.textureCreates; s.invalidArguments|=procedural;
    return s.textureMode==TextureMode::Missing?0:500+s.textureCreates;
}
void __fastcall TextureFile(void* self,void*,int id,const char* path,int filter,bool reload) {
    auto& s=S(self); ++s.textureFiles;
    s.invalidArguments|=id<501||id>500+s.textureCreates||!path||filter!=1||reload;
    if (path) s.texturePaths.emplace_back(path);
    if(path&&std::strncmp(path,"vgui/l4n_s/",11)==0) {
        if(s.throwModelUpload)RaiseException(0xE0010002,0,0,nullptr);
        s.borrowedTextures.insert(id);s.borrowedMaterials[id]=path;s.borrowedSizes[id]={64,64};s.existingByPath[path]=id;
    }
}
bool __fastcall GetTextureFile(void* self,void*,int id,char* path,int capacity) {
    auto& s=S(self);++s.materialQueries[id];
    if(s.throwMaterialQuery)RaiseException(0xE0010003,0,0,nullptr);
    if(s.replaceSizeMethodOnQuery)s.methods[30]=reinterpret_cast<void*>(&ReplacementTextureSize);
    if(!path||capacity<2) {s.invalidArguments=true;return false;}
    const auto found=s.borrowedMaterials.find(id);
    if(found==s.borrowedMaterials.end()||found->second.size()>=static_cast<std::size_t>(capacity))return false;
    std::memcpy(path,found->second.c_str(),found->second.size()+1);return true;
}
void __fastcall TextureSize(void* self,void*,int id,int* width,int* height) {
    auto& s=S(self); ++s.textureSizes;++s.sizeQueries[id];
    if(s.throwSizeId==id)RaiseException(0xE0010004,0,0,nullptr);
    if (!width||!height||(id!=301&&!s.borrowedTextures.count(id)&&(id<501||id>500+s.textureCreates))) { s.invalidArguments=true; return; }
    const auto custom=s.borrowedSizes.find(id);
    if(custom!=s.borrowedSizes.end()) {*width=custom->second.first;*height=custom->second.second;return;}
    *width=*height=64;
}
void __fastcall ReplacementTextureSize(void* self,void* ignored,int id,int* width,int* height) {
    ++S(self).replacementSizeCalls;TextureSize(self,ignored,id,width,height);
}
bool __fastcall ValidTexture(void* self,void*,int id) {
    auto& s=S(self); ++s.textureValidations;
    return s.borrowedTextures.count(id)||(s.textureMode!=TextureMode::Missing&&(id==301||(id>=501&&id<=500+s.textureCreates)));
}
void __fastcall SelectTexture(void* self,void*,int id) { S(self).selectedTexture=id; }
void __fastcall DrawTexture(void* self,void*,int x,int y,int right,int bottom) {
    auto& s=S(self); ++s.textureDraws;
    s.texturedDrawIds.push_back(s.selectedTexture);
    s.invalidArguments|=(!s.borrowedTextures.count(s.selectedTexture)&&s.selectedTexture!=301&&
                        (s.selectedTexture<501||s.selectedTexture>500+s.textureCreates))||right<=x||bottom<=y;
}
void __fastcall DrawTextureUv(void* self,void*,int x,int y,int right,int bottom,
                              float u0,float v0,float u1,float v1) {
    auto& s=S(self);++s.textureDraws;s.texturedDrawIds.push_back(s.selectedTexture);
    s.textureDrawsWithUv.push_back({s.selectedTexture,{x,y,right-x,bottom-y},{u0,v0,u1,v1},s.currentColor});
    s.invalidArguments|=(!s.borrowedTextures.count(s.selectedTexture)&&s.selectedTexture!=301&&
                        (s.selectedTexture<501||s.selectedTexture>500+s.textureCreates))||right<=x||bottom<=y||
                        !std::isfinite(u0)||!std::isfinite(v0)||!std::isfinite(u1)||!std::isfinite(v1)||u1<=u0||v1<=v0;
}

// These traps intentionally accept the OLD BUGGY call shapes, so a regression
// reports a failed check instead of corrupting the test process's x86 stack.
// They are NOT implementations of the real packed-Color/GetCharacterWidth
// signatures. The independent DLL disassembly establishes those real ABIs:
// slots 10/18 pop 4 bytes, slot 71 pops 8, whereas the old caller passed 16.
void __fastcall ObsoleteColor(void* self,void*,int,int,int,int) { ++S(self).obsoleteCalls; }
void __fastcall ObsoleteTextSize(void* self,void*,unsigned long,const wchar_t*,int* width,int* height) {
    ++S(self).obsoleteCalls; *width=0; *height=12;
}
void Bind(Surface& s) {
    // Literal positions verified in the installed Surface031 binary. Do not
    // share renderer slot constants: that would reproduce the original mock's
    // false confidence when both the mock and production code were wrong.
    s.vtable=s.methods.data();
    s.methods[10]=reinterpret_cast<void*>(&ObsoleteColor);
    s.methods[11]=reinterpret_cast<void*>(&SetColor);
    s.methods[12]=reinterpret_cast<void*>(&Fill);
    s.methods[14]=reinterpret_cast<void*>(&Border);
    s.methods[17]=reinterpret_cast<void*>(&SetFont);
    s.methods[18]=reinterpret_cast<void*>(&ObsoleteColor);
    s.methods[19]=reinterpret_cast<void*>(&SetTextColor);
    s.methods[20]=reinterpret_cast<void*>(&SetPosition);
    s.methods[22]=reinterpret_cast<void*>(&Print);
    s.methods[24]=reinterpret_cast<void*>(&Flush);
    s.methods[25]=reinterpret_cast<void*>(&TextureId);
    s.methods[26]=reinterpret_cast<void*>(&GetTextureFile);
    s.methods[27]=reinterpret_cast<void*>(&TextureFile);
    s.methods[29]=reinterpret_cast<void*>(&SelectTexture);
    s.methods[30]=reinterpret_cast<void*>(&TextureSize);
    s.methods[31]=reinterpret_cast<void*>(&DrawTexture);
    s.methods[32]=reinterpret_cast<void*>(&ValidTexture);
    s.methods[34]=reinterpret_cast<void*>(&CreateTexture);
    s.methods[63]=reinterpret_cast<void*>(&CreateFont);
    s.methods[64]=reinterpret_cast<void*>(&SetGlyphs);
    s.methods[71]=reinterpret_cast<void*>(&ObsoleteTextSize);
    s.methods[72]=reinterpret_cast<void*>(&TextSize);
    s.methods[98]=reinterpret_cast<void*>(&DrawTextureUv);
}
neh::Snapshot Snapshot(const char* name,int character) {
    neh::Snapshot snapshot; snapshot.inGame=true; snapshot.localIndex=snapshot.viewIndex=1;
    neh::Player player; player.index=6; player.name=name; player.character=character;
    player.alive=true; player.health=73; player.maxHealth=100;
    player.throwable=13; player.medical=12; player.pills=15;
    snapshot.players.push_back(player);
    return snapshot;
}
neh::Layout Layout() {
    neh::Layout layout; layout.cards.push_back({6,0,5,{700,900,320,96}}); return layout;
}
bool Printed(const Surface& s,const wchar_t* text) {
    return std::find(s.printed.begin(),s.printed.end(),text)!=s.printed.end();
}
void CheckClean(const Surface& s) {
    Check(s.obsoleteCalls==0,"renderer never calls packed-Color or character-width slots with the wrong argument shapes");
    Check(!s.invalidArguments,"surface calls preserve valid colors, font handles, rectangles and texture arguments");
}
void CheckHealthRendering() {
    const std::array<int,4> green={135,231,44,255},yellow={235,196,39,255},red={235,56,48,255};
    const std::array<int,4> temporary={166,179,109,210},barBackground={45,49,41,255};
    Surface surface;Bind(surface);
    neh::Renderer renderer;renderer.Initialize(&surface);
    auto snapshot=Snapshot("Status Survivor",-1);
    auto& player=snapshot.players.front();
    player.throwable=player.medical=player.pills=0;
    auto layout=Layout();neh::Settings settings;
    auto draw=[&](int total,float temp,bool alive=true,bool incap=false,bool hanging=false,int width=249) {
        player.health=total;player.temporaryHealth=temp;player.alive=alive;
        player.incapacitated=incap;player.hanging=hanging;
        layout.cards.front().bounds={700,900,width,111};
        surface.fills.clear();surface.printed.clear();surface.textDraws.clear();
        renderer.Draw(snapshot,layout,settings);
    };
    auto fill=[&](const std::array<int,4>& color)->const FillRecord* {
        const auto it=std::find_if(surface.fills.begin(),surface.fills.end(),
            [&](const FillRecord& record){return record.color==color;});
        return it==surface.fills.end()?nullptr:&*it;
    };
    auto label=[&](const wchar_t* text,const std::array<int,4>& color) {
        return std::any_of(surface.textDraws.begin(),surface.textDraws.end(),
            [&](const TextRecord& record){return record.text==text&&record.color==color;});
    };
    // A 249x111 card has a 153x15 bar at (790,963). Check emitted pixels,
    // not a second implementation of the renderer's percentage calculation.
    auto segments=[&](int solidWidth,int tempWidth,const std::array<int,4>& healthColor) {
        const auto background=fill(barBackground),solid=fill(healthColor),temp=fill(temporary);
        if(!background||background->rect.x!=790||background->rect.y!=963||
           background->rect.w!=153||background->rect.h!=15)return false;
        if(solidWidth?( !solid||solid->rect.x!=790||solid->rect.y!=963||
                         solid->rect.w!=solidWidth||solid->rect.h!=15):solid!=nullptr)return false;
        if(tempWidth?( !temp||temp->rect.x!=790+solidWidth||temp->rect.y!=963||
                        temp->rect.w!=tempWidth||temp->rect.h!=15):temp!=nullptr)return false;
        return solidWidth+tempWidth<=153;
    };
    draw(80,79);
    Check(label(L"+80",green),"one permanent plus seventy-nine temporary health prints the total eighty in green");
    Check(segments(1,121,green),"temporary health is part of the eighty-percent bar rather than added a second time");
    const struct {int total;std::array<int,4> color;} thresholds[]={{24,red},{25,yellow},{39,yellow},{40,green}};
    for(const auto& threshold:thresholds) {
        draw(threshold.total,static_cast<float>(threshold.total-1));
        const auto text=L"+"+std::to_wstring(threshold.total);
        Check(label(text.c_str(),threshold.color),"health warning colors use total health at the 24/25 and 39/40 boundaries");
    }
    const struct {float value;int solid,temp;} buffers[]={
        {0,122,0},{std::numeric_limits<float>::quiet_NaN(),122,0},
        {std::numeric_limits<float>::infinity(),122,0},{-5,122,0},{200,0,122}
    };
    for(const auto& buffer:buffers) {
        draw(80,buffer.value);
        Check(label(L"+80",green)&&segments(buffer.solid,buffer.temp,green),
              "zero, nonfinite, negative and excessive temporary health cannot change the total or overfill the bar");
    }
    draw(100,99);
    Check(segments(1,152,green),"full health segments meet at one boundary and stop at the bar's right edge");
    draw(250,200);
    Check(label(L"+250",green)&&segments(76,77,green),"over-max total health keeps its number while both bar segments remain bounded");
    draw(0,79);
    Check(label(L"+0",red)&&segments(0,0,red),"zero total health produces no positive solid or temporary segment");
    draw(245,79,true,true);
    Check(label(L"DOWN 245",green)&&segments(153,0,green),"downed health remains numeric and never draws a temporary segment");
    draw(96,79,true,true,true);
    Check(label(L"HANG 96",green)&&segments(146,0,green),"hanging takes precedence over downed and retains its current health without temporary fill");
    draw(245,79,true,true,false,136);
    Check(label(L"245",green)&&!Printed(surface,L"DOWN")&&!Printed(surface,L"DOWN 24"),
          "narrow downed cards retain the complete health number when the status prefix cannot fit");
    draw(96,79,true,false,true,136);
    Check(label(L"96",green)&&!Printed(surface,L"HANG")&&!Printed(surface,L"HANG 9"),
          "narrow hanging cards retain the complete health number when the status prefix cannot fit");
    const auto& card=layout.cards.front().bounds;
    Check(std::all_of(surface.fills.begin(),surface.fills.end(),[&](const FillRecord& record) {
        const auto& r=record.rect;
        return r.x>=card.x&&r.y>=card.y&&r.w>0&&r.h>0&&r.x+r.w<=card.x+card.w&&r.y+r.h<=card.y+card.h;
    }),"all emitted fill rectangles remain inside a narrow card");
    draw(0,79,false,true,true);
    Check(label(L"DEAD",{183,72,67,255})&&!Printed(surface,L"DOWN 0")&&!Printed(surface,L"HANG 0")&&
          segments(0,0,red),"dead status overrides live states and draws no health or temporary fill");
    draw(100,0,true,false,false,110);
    Check(!Printed(surface,L"+100")&&!Printed(surface,L"+10")&&!Printed(surface,L"+1"),
          "a total that cannot fit is omitted instead of printed as a misleading truncated number");
    CheckClean(surface);
}
void CheckGameHudThemeRendering() {
    using namespace neh;
    Surface surface;Bind(surface);
    surface.fonts[900]=12;surface.fonts[901]=14;surface.fonts[902]=11;
    for(int id:{7001,7007,7010,7011,7012,7020,7021,7022,7023,7024,7025})surface.borrowedTextures.insert(id);
    Renderer renderer;renderer.Initialize(&surface);
    auto snapshot=Snapshot("Actual Extra Survivor",4);
    auto& player=snapshot.players.front();player.health=80;player.temporaryHealth=79;
    auto layout=Layout();layout.cards.front().bounds={700,900,400,200};
    Settings settings;GameHudTheme theme;theme.available=true;
    auto widget=[](Rect rect,Color color,unsigned long font,int z,int order) {
        ThemeWidget value;value.present=true;value.rect=rect;value.color=color;value.font=font;value.z=z;value.order=order;
        return value;
    };
    auto& normal=theme.states[0];normal.ready=true;normal.width=200;normal.height=100;
    normal.portrait=widget({5,5,30,40},{201,202,203,220},0,2,0);
    normal.name=widget({45,5,145,12},{20,200,80,240},900,3,0);
    normal.number=widget({45,20,80,15},{30,40,50,230},901,3,1);
    normal.status=widget({45,20,145,15},{66,77,88,255},902,3,2);
    for(std::size_t i=0;i<3;++i)normal.items[i]=widget({45+static_cast<int>(i)*30,78,24,18},{255,255,255,255},0,4,static_cast<int>(i));
    ThemeImage background;background.widget=widget({-20,-10,240,120},{255,255,255,255},0,0,2);
    background.texture.id=7001;background.texture.u0=.1f;background.texture.v0=.2f;
    background.texture.u1=.9f;background.texture.v1=.8f;background.texture.tint={12,34,56,200};
    ThemeImage first;first.widget=widget({0,0,10,10},{255,255,255,255},0,0,1);first.texture.id=7007;
    normal.images={background,first};
    ThemeIcon wrong;wrong.kind=1;wrong.item=13;wrong.texture.id=7011;
    ThemeIcon grenade;grenade.kind=0;grenade.item=13;grenade.texture.id=7010;
    grenade.texture.u0=.25f;grenade.texture.v0=.125f;grenade.texture.u1=.5f;grenade.texture.v1=.625f;
    ThemeIcon medical;medical.kind=1;medical.item=12;medical.texture.id=7012;
    theme.icons={wrong,grenade,medical};
    auto resetRecords=[&]() {
        surface.fills.clear();surface.printed.clear();surface.textDraws.clear();surface.texturedDrawIds.clear();
        surface.textureDrawsWithUv.clear();surface.texturePaths.clear();surface.invalidArguments=false;
    };
    auto draw=[&]() {resetRecords();return renderer.Draw(snapshot,layout,settings,&theme);};
    auto texture=[&](int id)->const TextureRecord* {
        const auto found=std::find_if(surface.textureDrawsWithUv.begin(),surface.textureDrawsWithUv.end(),
            [&](const TextureRecord& record){return record.id==id;});
        return found==surface.textureDrawsWithUv.end()?nullptr:&*found;
    };
    auto text=[&](const wchar_t* value)->const TextRecord* {
        const auto found=std::find_if(surface.textDraws.begin(),surface.textDraws.end(),
            [&](const TextRecord& record){return record.text==value;});
        return found==surface.textDraws.end()?nullptr:&*found;
    };
    auto close=[](float a,float b){return std::fabs(a-b)<0.00001f;};
    player.character=0;draw();
    Check(!texture(7001)&&surface.textureDrawsWithUv.empty()&&Printed(surface,L"+80")&&
          !surface.fills.empty()&&surface.fills.front().color==std::array<int,4>{7,10,8,218},
          "current theme keeps the existing card even when a complete native theme is supplied");
    settings.theme=HudTheme::GameHud;player.character=4;
    Check(draw(),"a submitted supported game-HUD card reports success to the native replacement caller");
    const auto bg=texture(7001);auto name=text(L"Actual Extra Survivor");
    Check(bg&&bg->rect.x==700&&bg->rect.y==900&&bg->rect.w==400&&bg->rect.h==200&&
          close(bg->uv[0],1.0f/6)&&close(bg->uv[1],.25f)&&close(bg->uv[2],5.0f/6)&&close(bg->uv[3],.75f)&&
          bg->color==std::array<int,4>{12,34,56,200},
          "borrowed background scales and clips to the extra card with matching Surface98 UV interpolation and tint");
    Check(name&&name->x==790&&name->y==910&&name->font==900&&name->color==std::array<int,4>{20,200,80,240}&&
          Printed(surface,L"+80")&&!Printed(surface,L"+159"),
          "native widget positions and fonts display the extra survivor's own name and already-summed total health");
    Check(surface.texturedDrawIds.size()>=2&&surface.texturedDrawIds[0]==7007&&surface.texturedDrawIds[1]==7001,
          "native images retain their stable z and order instead of vector insertion order");
    Check(surface.texturePaths==std::vector<std::string>{"vgui/s_panel_namvet","vgui/s_panel_namvet"},
          "native theme portrait is selected from the extra player's Bill identity rather than copied source art");
    const auto item=texture(7010);
    Check(item&&item->rect.x==790&&item->rect.y==1056&&item->rect.w==48&&item->rect.h==36&&
          close(item->uv[0],.25f)&&close(item->uv[1],.125f)&&close(item->uv[2],.5f)&&close(item->uv[3],.625f)&&
          texture(7012)&&!texture(7011),
          "inventory borrows UVs only for the matching kind and current item ID");
    player.throwable=14;draw();
    Check(!texture(7010)&&!texture(7011)&&texture(7012),
          "a changed item never reuses a previous grenade or a different kind's icon");
    player.throwable=13;player.character=5;player.name="Actual Zoey";draw();
    Check(surface.texturePaths==std::vector<std::string>{"vgui/s_panel_teenangst","vgui/s_panel_teenangst"}&&
          Printed(surface,L"Actual Zoey"),"changing character in a themed slot selects Zoey's own game portrait");

    normal.name.font=0;draw();name=text(L"Actual Zoey");
    Check(name&&name->font!=0&&name->font!=900,"a missing native font falls back to a valid existing card font");
    normal.name.font=900;
    surface.fonts[900]=500;draw();name=text(L"Actual Zoey");
    Check(name&&name->font!=900,"an oversized native font uses a valid smaller font before text can leave its extra card");
    surface.fonts[900]=12;
    normal.name.alignment=3;normal.name.insetX=3;normal.name.insetY=2;draw();name=text(L"Actual Zoey");
    Check(name&&name->x==796&&name->y==920,
          "native west alignment vertically centers text and scales both label insets to the extra card");
    normal.name.alignment=5;draw();name=text(L"Actual Zoey");
    Check(name&&name->x==1008&&name->y==920,
          "native east alignment measures the whole text before applying the scaled right inset");
    normal.name.alignment=4;draw();name=text(L"Actual Zoey");
    Check(name&&name->x==902&&name->y==920,
          "native centered alignment ignores the horizontal inset while retaining its vertical inset");
    normal.name.alignment=normal.name.insetX=normal.name.insetY=0;
    normal.images[0].texture.u0=std::numeric_limits<float>::quiet_NaN();draw();
    Check(!texture(7001)&&texture(7007)&&Printed(surface,L"+80")&&!surface.invalidArguments,
          "a nonfinite native UV excludes its image while valid theme widgets continue drawing");
    normal.images[0]=background;normal.images[0].texture.id=0;draw();
    Check(!texture(7001)&&texture(7007)&&!surface.invalidArguments,
          "an absent borrowed texture ID is skipped without an invalid surface call");
    normal.images[0]=background;normal.images[0].widget.rect.w=0;draw();
    Check(!texture(7001)&&texture(7007)&&!surface.invalidArguments,
          "empty widget bounds cannot produce a textured rectangle");
    normal.images[0]=background;
    normal.health=widget({45,55,145,15},{255,255,255,255},0,3,3);
    normal.bar.present=true;normal.bar.newMaterial=true;
    for(std::size_t band=0;band<5;++band) {
        normal.bar.fill[band].id=7020;normal.bar.ticks[band].id=7024;
    }
    normal.bar.fill[1].u0=.2f;normal.bar.fill[1].v0=.3f;
    normal.bar.fill[1].u1=.8f;normal.bar.fill[1].v1=.9f;
    theme.numberPalette[0]={211,22,33,244};draw();
    const auto solid=texture(7020),temporary=texture(7024);const auto healthText=text(L"+80");
    Check(solid&&temporary&&solid->rect.x==790&&solid->rect.y==1010&&solid->rect.w==2&&solid->rect.h==30&&
          temporary->rect.x==792&&temporary->rect.y==1010&&temporary->rect.w==230&&temporary->rect.h==30&&
          close(solid->uv[0],.2f)&&close(solid->uv[2],.206f)&&
          close(temporary->uv[0],.01f)&&close(temporary->uv[2],.8f)&&
          healthText&&healthText->color==std::array<int,4>{211,22,33,244},
          "native health materials split one permanent plus seventy-nine temporary HP inside the eighty-percent total and use its observed number color");
    const auto ticksOrder=std::find(surface.texturedDrawIds.begin(),surface.texturedDrawIds.end(),7024);
    const auto solidOrder=std::find(surface.texturedDrawIds.begin(),surface.texturedDrawIds.end(),7020);
    Check(ticksOrder<solidOrder,"native temporary ticks draw before the permanent health material");
    player.temporaryHealth=std::numeric_limits<float>::infinity();draw();
    Check(texture(7020)&&texture(7020)->rect.w==232&&!texture(7024)&&Printed(surface,L"+80")&&!surface.invalidArguments,
          "nonfinite temporary HP cannot produce invalid UVs or add a second health amount in the game theme");
    player.temporaryHealth=79;normal.number.rect.w=1;draw();
    Check(!Printed(surface,L"+80")&&!Printed(surface,L"+8")&&!Printed(surface,L"+"),
          "native health numbers that cannot fit are omitted whole instead of being truncated");
    normal.number.rect.w=80;
    for(const auto unavailable:{0,1,2,3}) {
        if(unavailable==0)normal.ready=false;
        if(unavailable==1){normal.ready=true;normal.width=0;}
        if(unavailable==2){normal.width=200;theme.available=false;}
        if(unavailable==3){theme.available=true;normal.ready=false;player.incapacitated=true;}
        draw();
        Check(!texture(7001)&&surface.textureDrawsWithUv.empty()&&
              !surface.fills.empty()&&surface.fills.front().color==std::array<int,4>{7,10,8,218},
              "unavailable, unsupported or incomplete theme states fall back to the complete current card");
    }
    player.incapacitated=false;normal.ready=true;normal.width=200;theme.available=true;
    for(std::size_t state=1;state<4;++state) {
        theme.states[state]=normal;theme.states[state].images[0].texture.id=7020+static_cast<int>(state);
    }
    player.incapacitated=true;player.health=245;player.temporaryHealth=79;draw();
    Check(texture(7021)&&!texture(7001)&&Printed(surface,L"DOWN 245")&&
          texture(7024)&&texture(7024)->rect.w==236&&!texture(7020),
          "the incapacitated theme keeps live downed state and the complete current health number");
    player.hanging=true;player.health=96;draw();
    Check(texture(7022)&&!texture(7021)&&Printed(surface,L"HANG 96")&&
          texture(7024)&&texture(7024)->rect.w==92&&!texture(7020),
          "hanging theme takes precedence over incapacitation and retains current health");
    player.alive=false;player.health=0;draw();
    Check(texture(7023)&&!texture(7022)&&Printed(surface,L"DEAD")&&!Printed(surface,L"HANG 0"),
          "dead theme takes precedence over both live states without inheriting template text");
    Check(!texture(7020)&&!texture(7024)&&std::all_of(surface.textureDrawsWithUv.begin(),surface.textureDrawsWithUv.end(),
          [](const TextureRecord& record){return record.rect.x>=700&&record.rect.y>=900&&
              record.rect.x+record.rect.w<=1100&&record.rect.y+record.rect.h<=1100;}),
          "dead themes draw no live health segments and every UV draw stays inside the extra card");
    CheckClean(surface);
}
void CheckLegacyHealthInsets() {
    using namespace neh;
    Surface surface;Bind(surface);
    for(int id:{8401,8402,8403,8404,8405})surface.borrowedTextures.insert(id);
    Renderer renderer;renderer.Initialize(&surface);
    auto snapshot=Snapshot("Legacy health",-1);auto& player=snapshot.players.front();
    player.health=100;player.temporaryHealth=0;player.throwable=player.medical=player.pills=0;
    auto layout=Layout();layout.cards.front().bounds={100,200,267,144};
    Settings settings;settings.theme=HudTheme::GameHud;
    GameHudTheme theme;theme.available=true;
    GameHudCardStyle style;style.ready=true;style.width=267;style.height=144;
    style.health.present=true;style.health.rect={63,93,174,12};
    style.bar.present=true;style.bar.inset=6;
    style.bar.white.id=8401;style.bar.grey.id=8402;style.bar.outline.id=8403;
    for(auto& texture:style.bar.fill)texture.id=8404;
    for(auto& texture:style.bar.ticks)texture.id=8405;
    const std::array<int,4> green={10,177,50,255};
    auto draw=[&]() {
        for(auto& state:theme.states)state=style;
        surface.fills.clear();surface.textureDrawsWithUv.clear();surface.texturedDrawIds.clear();
        return renderer.Draw(snapshot,layout,settings,&theme);
    };
    auto texture=[&](int id)->const TextureRecord* {
        const auto found=std::find_if(surface.textureDrawsWithUv.begin(),surface.textureDrawsWithUv.end(),
            [&](const TextureRecord& value){return value.id==id;});
        return found==surface.textureDrawsWithUv.end()?nullptr:&*found;
    };
    auto same=[](const Rect& value,int x,int y,int w,int h) {
        return value.x==x&&value.y==y&&value.w==w&&value.h==h;
    };
    auto inCard=[&](const Rect& rect) {
        const auto& card=layout.cards.front().bounds;
        return rect.w>0&&rect.h>0&&rect.x>=card.x&&rect.y>=card.y&&
            rect.x+rect.w<=card.x+card.w&&rect.y+rect.h<=card.y+card.h;
    };
    draw();const auto solid=texture(8401);
    Check(solid&&same(solid->rect,168,298,164,2)&&solid->color==green&&
          solid->uv==std::array<float,4>{0,0,1,1},
          "a 12px legacy bar with retained 6px inset keeps a visible full-health interior");
    style.bar.white.id=0;draw();
    Check(std::any_of(surface.fills.begin(),surface.fills.end(),[&](const FillRecord& value){
              return value.color==green&&same(value.rect,168,298,164,2);}),
          "the repaired legacy interior also paints solid color when its white texture is absent");
    style.bar.white.id=8401;player.health=80;player.temporaryHealth=40;draw();
    Check(texture(8401)&&same(texture(8401)->rect,168,298,65,2)&&!surface.fills.empty()&&
          surface.fills.front().rect.x==233&&std::all_of(surface.fills.begin(),surface.fills.end(),
              [&](const FillRecord& value){return value.color==green&&value.rect.y==298&&value.rect.h==2&&
                  value.rect.w<=2&&value.rect.x>=233&&value.rect.x+value.rect.w<=299;}),
          "legacy permanent fill and temporary ticks share the repaired interior without double-counting total HP");
    player.health=150;player.incapacitated=true;draw();
    Check(!texture(8401)&&!surface.fills.empty()&&std::all_of(surface.fills.begin(),surface.fills.end(),
              [&](const FillRecord& value){return value.rect.y==298&&value.rect.h==2&&value.rect.w<=2&&
                  value.rect.x>=168&&value.rect.x+value.rect.w<=250;}),
          "downed legacy health retains visible repaired ticks on the 300HP scale without permanent fill");
    player.hanging=true;player.health=96;draw();
    Check(!texture(8401)&&!surface.fills.empty()&&std::all_of(surface.fills.begin(),surface.fills.end(),
              [&](const FillRecord& value){return value.rect.h==2&&value.rect.x+value.rect.w<=220;}),
          "hanging legacy health uses the same nonempty interior and the current 300HP ratio");
    player.alive=false;draw();
    Check(surface.textureDrawsWithUv.empty()&&surface.fills.empty(),
          "dead legacy cards submit no health background, outline, solid fill or ticks");
    player.alive=true;player.incapacitated=player.hanging=false;player.health=100;player.temporaryHealth=0;
    style.bar.inset=1;draw();
    Check(texture(8401)&&same(texture(8401)->rect,164,294,172,10),
          "a legal legacy inset preserves its original geometry exactly");
    for(int inset:{0,-10}) {
        style.bar.inset=inset;draw();
        Check(texture(8401)&&same(texture(8401)->rect,163,293,174,12),
              "zero and negative legacy insets retain the full valid bar without invalid rectangles");
    }
    style.bar.inset=(std::numeric_limits<int>::max)();draw();
    Check(texture(8401)&&same(texture(8401)->rect,168,298,164,2),
          "an excessive legacy inset is bounded before subtraction rather than erasing the health fill");
    style.width=200;style.height=100;style.health.rect={10,10,100,12};style.bar.inset=6;
    layout.cards.front().bounds={100,200,100,50};draw();
    Check(texture(8401)&&same(texture(8401)->rect,108,208,45,1),
          "a half-size legacy bar retains a one-pixel solid fill after limiting its source inset");
    style.health.rect.h=3;style.bar.inset=1;draw();
    Check(texture(8401)&&same(texture(8401)->rect,106,206,49,1),
          "half-size edge rounding cannot erase a one-source-pixel legacy interior");
    style.health.rect={10,10,1,3};draw();
    Check(texture(8401)&&texture(8401)->rect.w==1&&texture(8401)->rect.h>=1&&inCard(texture(8401)->rect),
          "a narrow positive legacy source retains a pixel inside its projected bar");
    style.health.rect={-10,94,220,12};style.bar.inset=0;draw();
    Check(texture(8401)&&same(texture(8401)->rect,100,247,100,3)&&
          std::all_of(surface.textureDrawsWithUv.begin(),surface.textureDrawsWithUv.end(),
              [&](const TextureRecord& value){return inCard(value.rect);})&&
          std::all_of(surface.fills.begin(),surface.fills.end(),
              [&](const FillRecord& value){return inCard(value.rect);}),
          "legacy health textures and fallback pixels remain clipped to the card after interior repair");
    for(const Rect invalid:{Rect{10,10,0,12},Rect{10,10,-1,12},Rect{10,10,100,0}}) {
        style.health.rect=invalid;draw();
        Check(surface.textureDrawsWithUv.empty()&&surface.fills.empty(),
              "nonpositive legacy source dimensions produce no invalid surface rectangles");
    }
    style.health.rect={10,10,100,12};style.bar.inset=(std::numeric_limits<int>::max)();
    style.bar.newMaterial=true;draw();
    Check(texture(8404)&&same(texture(8404)->rect,105,205,50,6)&&!texture(8401)&&!texture(8403),
          "new-material health keeps its full projected geometry and ignores the legacy inset");
    style.bar.newMaterial=false;
    style.bar.white.tint.a=style.bar.grey.tint.a=style.bar.outline.tint.a=0;draw();
    Check(surface.textureDrawsWithUv.empty()&&surface.fills.empty(),
          "legacy interior repair respects deliberately transparent health textures");
    CheckClean(surface);
}
void CheckBorrowedTextureLifetimes() {
    using namespace neh;
    Surface surface;Bind(surface);surface.borrowedTextures.insert(8001);
    surface.borrowedMaterials[8001]="vgui/borrowed_first";surface.borrowedSizes[8001]={64,64};
    GameHudTheme theme;theme.available=true;
    auto& style=theme.states[0];style.ready=true;style.width=100;style.height=100;
    ThemeImage left;left.widget.present=true;left.widget.rect={0,0,50,100};
    left.texture.id=8001;left.texture.material="vgui/borrowed_first";
    ThemeImage right=left;right.widget.rect.x=50;right.widget.order=1;style.images={left,right};
    auto snapshot=Snapshot("Own Survivor",-1);auto layout=Layout();Settings settings;settings.theme=HudTheme::GameHud;
    Renderer renderer;renderer.Initialize(&surface);
    auto draw=[&]() {
        surface.textureDrawsWithUv.clear();surface.texturedDrawIds.clear();surface.printed.clear();
        surface.materialQueries.clear();surface.sizeQueries.clear();g_runtimeCounters={};
        renderer.Draw(snapshot,layout,settings,&theme);
    };
    draw();
    Check(surface.textureDrawsWithUv.size()==2&&surface.materialQueries[8001]==1&&surface.sizeQueries[8001]==1&&
          g_runtimeCounters.themeTextureQueries==1&&g_runtimeCounters.themeTextureMisses==0,
          "one draw validates a borrowed material and size once for multiple widgets sharing its ID");
    surface.borrowedMaterials[8001]="vgui/borrowed_second";draw();
    Check(surface.textureDrawsWithUv.empty()&&surface.materialQueries[8001]==1&&surface.sizeQueries[8001]==0&&
          g_runtimeCounters.themeTextureQueries==1&&g_runtimeCounters.themeTextureMisses==2,
          "the next draw rechecks a reused texture ID and rejects every request for its previous material");
    surface.borrowedMaterials.erase(8001);draw();
    Check(surface.textureDrawsWithUv.empty()&&surface.materialQueries[8001]==1&&g_runtimeCounters.themeTextureMisses==2,
          "deleted borrowed texture IDs cannot reach a textured draw");
    surface.borrowedMaterials[8001]="vgui/borrowed_first";surface.borrowedSizes[8001]={0,64};draw();
    Check(surface.textureDrawsWithUv.empty()&&surface.materialQueries[8001]==1&&surface.sizeQueries[8001]==1&&
          g_runtimeCounters.themeTextureMisses==2,
          "a matching material with zero dimensions is rejected and its invalid size is queried only once per draw");
    settings.theme=HudTheme::Current;draw();
    Check(surface.materialQueries.empty()&&surface.sizeQueries.empty()&&g_runtimeCounters.themeTextureQueries==0&&
          g_runtimeCounters.themeTextureMisses==0&&
          surface.textureDrawsWithUv.empty()&&Printed(surface,L"+73"),
          "current design does no borrowed-material validation even when supplied theme metadata is stale");
    settings.theme=HudTheme::GameHud;surface.borrowedMaterials[8001]="vgui/borrowed_second";
    surface.borrowedSizes[8001]={64,64};
    for(auto& image:style.images)image.texture.material="vgui/borrowed_second";
    draw();
    Check(surface.textureDrawsWithUv.size()==2&&surface.materialQueries[8001]==1&&surface.sizeQueries[8001]==1&&
          g_runtimeCounters.themeTextureMisses==0,
          "newly captured expected material metadata restores a reused ID on the next draw");
    for(const bool staleFirst:{false,true}) {
        style.images[staleFirst?0:1].texture.material="vgui/borrowed_first";
        style.images[staleFirst?1:0].texture.material="vgui/borrowed_second";draw();
        Check(surface.textureDrawsWithUv.size()==1&&surface.materialQueries[8001]==1&&surface.sizeQueries[8001]==1&&
              g_runtimeCounters.themeTextureMisses==1,
              "same-ID caching keeps the actual material independent of request order and lazily validates its size");
    }
    CheckClean(surface);g_runtimeCounters={};
}
struct ItemLookupProbe {
    std::vector<int> requests;
    const neh::ThemeIcon* reply=nullptr;
    std::array<const neh::ThemeIcon*,3> itemReplies{};
};
ItemLookupProbe* activeItemLookup=nullptr;
const neh::ThemeIcon* LookupFixtureItem(int item) {
    activeItemLookup->requests.push_back(item);
    for(const auto* icon:activeItemLookup->itemReplies)if(icon&&icon->item==item)return icon;
    return activeItemLookup->reply;
}

void CheckHeldInventoryAndStateFallbacks() {
    using namespace neh;
    Surface surface;Bind(surface);surface.fonts[900]=12;surface.borrowedTextures.insert(8101);
    Renderer renderer;renderer.Initialize(&surface);
    auto snapshot=Snapshot("Inventory Survivor",-1);auto& player=snapshot.players.front();
    auto layout=Layout();layout.cards.front().bounds={700,900,249,111};
    Settings settings;settings.theme=HudTheme::GameHud;
    GameHudTheme theme;theme.available=true;auto& normal=theme.states[0];
    normal.ready=true;normal.width=249;normal.height=111;
    auto widget=[](Rect rect) {
        ThemeWidget value;value.present=true;value.rect=rect;value.font=900;value.color={230,231,232,255};return value;
    };
    normal.portrait=widget({6,6,50,65});normal.name=widget({6,90,230,16});
    normal.number=widget({90,5,100,16});normal.health=widget({90,63,153,15});normal.bar.present=true;
    ThemeImage background;background.widget=widget({0,0,249,111});background.fill={11,22,33,222};
    normal.images={background};
    auto items=[&](int throwable,int medical,int pills) {player.throwable=throwable;player.medical=medical;player.pills=pills;};
    ItemLookupProbe lookupProbe;
    ItemIconLookup lookup=nullptr;
    auto draw=[&]() {
        surface.fills.clear();surface.printed.clear();surface.textDraws.clear();surface.texturedDrawIds.clear();
        surface.textureDrawsWithUv.clear();surface.materialQueries.clear();surface.sizeQueries.clear();
        surface.texturePaths.clear();lookupProbe.requests.clear();
        renderer.Draw(snapshot,layout,settings,&theme,lookup);
    };
    auto inside=[](const Rect& inner,const Rect& outer) {
        return inner.w>0&&inner.h>0&&inner.x>=outer.x&&inner.y>=outer.y&&
            inner.x+inner.w<=outer.x+outer.w&&inner.y+inner.h<=outer.y+outer.h;
    };
    auto symbols=[&](std::size_t expected) {
        std::vector<Rect> boxes;
        for(const auto& record:surface.fills)if(record.color==std::array<int,4>{22,24,22,230})boxes.push_back(record.rect);
        if(boxes.size()!=expected)return false;
        return std::all_of(boxes.begin(),boxes.end(),[&](const Rect& box) {
            return box.w>=8&&box.h>=8&&inside(box,layout.cards.front().bounds)&&
                std::any_of(surface.fills.begin(),surface.fills.end(),[&](const FillRecord& record) {
                    return record.color!=std::array<int,4>{22,24,22,230}&&record.color[3]>0&&inside(record.rect,box);
                });
        });
    };
    auto drawnTexture=[&](int id)->const TextureRecord* {
        const auto found=std::find_if(surface.textureDrawsWithUv.begin(),surface.textureDrawsWithUv.end(),
            [&](const TextureRecord& record){return record.id==id;});
        return found==surface.textureDrawsWithUv.end()?nullptr:&*found;
    };
    ThemeIcon pills;pills.kind=2;pills.item=15;pills.texture.id=8101;
    theme.icons={pills};normal.items[1]=widget({140,40,12,12});items(0,0,15);draw();
    const auto loaded=drawnTexture(8101);
    Check(loaded&&loaded->rect.w>=8&&loaded->rect.h>=8&&inside(loaded->rect,layout.cards.front().bounds),
          "a held pills icon uses loaded art even when its source widget is missing and another kind supplies geometry");
    normal.items={};theme.icons.clear();items(13,12,23);draw();
    Check(symbols(3),"three held item kinds remain visibly represented inside a compact card when every native item widget is absent");
    for(std::size_t kind=0;kind<3;++kind) {
        normal.items[kind]=widget({130+static_cast<int>(kind)*16,40,12,12});normal.items[kind].color.a=0;
    }
    normal.name.color.a=0;draw();
    Check(symbols(3)&&!Printed(surface,L"Inventory Survivor")&&surface.fills.front().color==std::array<int,4>{11,22,33,222},
          "empty-source item alpha does not hide held items while unrelated native text and background transparency stay unchanged");
    normal.name.color.a=255;
    for(auto& item:normal.items)item.color.a=255;
    pills.color.a=0;theme.icons={pills};items(0,0,15);draw();
    Check(symbols(1)&&!drawnTexture(8101),"a transparent cached bitmap falls back to a visible held-item symbol instead of returning after an invisible draw");
    theme.icons.clear();items(14,12,23);
    for(std::size_t kind=0;kind<3;++kind)normal.items[kind]=widget({130+static_cast<int>(kind)*10,40,7,7});
    draw();
    Check(symbols(3),"projected seven-pixel slots use bounded eight-pixel symbols instead of silently losing held items");
    ThemeIcon invalid;invalid.kind=0;invalid.item=25;invalid.texture.id=8101;invalid.texture.material="vgui/old_item";
    theme.icons={invalid};surface.borrowedMaterials[8101]="vgui/new_item";surface.borrowedSizes[8101]={64,64};
    normal.items={};items(25,0,0);draw();
    Check(symbols(1)&&!drawnTexture(8101)&&surface.materialQueries[8101]==1,
          "a stale borrowed icon material still leaves an opaque symbol for the current throwable");
    ThemeIcon fontIcon;fontIcon.kind=2;fontIcon.item=23;fontIcon.usingFont=true;fontIcon.font=900;fontIcon.glyph=L'P';
    theme.icons={fontIcon};items(0,0,23);draw();
    Check(symbols(1)&&!Printed(surface,L"P"),"unsupported native font icons use visible symbols without speculative glyph rendering");
    items(0,0,0);draw();
    Check(symbols(0)&&!drawnTexture(8101),"consuming or dropping equipment clears the previous frame's icon without copying a source player's items");
    const auto backgroundColor=std::array<int,4>{11,22,33,222};
    const struct {bool alive,down,hanging;int health;const wchar_t* label;} states[]={
        {true,true,false,245,L"DOWN 245"},{true,true,true,96,L"HANG 96"},{false,true,true,0,L"DEAD"}
    };
    for(const auto& state:states) {
        player.alive=state.alive;player.incapacitated=state.down;player.hanging=state.hanging;player.health=state.health;draw();
        Check(!surface.fills.empty()&&surface.fills.front().color==backgroundColor&&Printed(surface,state.label)&&symbols(0)&&
              std::any_of(surface.textDraws.begin(),surface.textDraws.end(),[&](const TextRecord& record) {
                  return record.text==L"Inventory Survivor"&&record.font==900;
              }),"unobserved down, hang and dead states retain the loaded normal background and font with current state text and no dropped-item ghosts");
    }
    theme.states[3]=normal;theme.states[3].number.present=theme.states[3].status.present=theme.states[3].health.present=false;
    theme.states[3].images[0].fill={51,52,53,222};draw();
    Check(surface.fills.front().color==std::array<int,4>{51,52,53,222}&&Printed(surface,L"DEAD"),
          "a dedicated dead template without live number or status widgets still displays the complete DEAD state");
    player.alive=true;player.incapacitated=true;player.hanging=false;player.health=245;
    theme.states[1]=normal;theme.states[1].portrait.present=false;theme.states[1].images[0].fill={41,42,43,222};draw();
    Check(surface.fills.front().color==std::array<int,4>{41,42,43,222}&&Printed(surface,L"DOWN 245"),
          "a sampled dedicated downed style takes priority over normal-theme adaptation without requiring a portrait");
    auto& incapImage=theme.states[1].images[0];incapImage.fill={0,0,0,0};incapImage.characterIncap=true;
    incapImage.texture.id=8102;incapImage.texture.material="vgui/s_panel_namvet_incap";
    incapImage.texture.tint={100,120,140,200};surface.borrowedTextures.insert(8102);
    surface.borrowedMaterials[8102]="vgui/s_panel_namvet_incap";surface.borrowedSizes[8102]={64,64};
    player.character=5;draw();
    Check(surface.texturePaths==std::vector<std::string>{"vgui/s_panel_teenangst_incap","vgui/s_panel_teenangst_incap"}&&
          !drawnTexture(8102)&&surface.materialQueries.count(8102)==0&&
          std::any_of(surface.textureDrawsWithUv.begin(),surface.textureDrawsWithUv.end(),[&](const TextureRecord& record) {
              return record.id>=501&&record.id<=500+surface.textureCreates&&record.rect.x==700&&record.rect.y==900&&
                  record.rect.w==249&&record.rect.h==111&&record.color==std::array<int,4>{100,120,140,200};
          }),"a Bill source incap background resolves the actual extra Zoey material while preserving native bounds and tint");
    player.character=4;draw();
    Check(surface.texturePaths==std::vector<std::string>{"vgui/s_panel_namvet_incap","vgui/s_panel_namvet_incap"}&&!drawnTexture(8102),
          "changing the extra character selects its own incap texture cache instead of retaining the previous survivor's face");
    incapImage.characterIncap=false;draw();
    Check(drawnTexture(8102)&&surface.texturePaths.empty()&&surface.materialQueries[8102]==1,
          "unclassified native decorations retain their validated original material without a guessed character substitution");
    theme.states[1].ready=false;normal.name.present=false;draw();
    Check(surface.fills.front().color==std::array<int,4>{7,10,8,218}&&Printed(surface,L"DOWN 245"),
          "missing normal core labels keep an unsupported state on the complete current-card fallback");
    normal.name.present=true;player.incapacitated=false;player.health=73;settings.theme=HudTheme::Current;items(13,12,15);draw();
    Check(surface.fills.front().color==std::array<int,4>{7,10,8,218}&&Printed(surface,L"+73")&&symbols(3),
          "current design keeps its original number and three inventory symbols after the live-theme fallback changes");

    settings.theme=HudTheme::GameHud;player.character=-1;items(25,0,0);
    normal.items={};normal.items[0]=widget({130,40,24,20});
    normal.images[0].widget.z=10;normal.images[0].texture.id=9103;
    for(int id:{9101,9102,9103}) {
        surface.borrowedTextures.insert(id);
        surface.borrowedMaterials[id]="vgui/item_atlas_"+std::to_string(id);
        surface.borrowedSizes[id]={64,64};
    }
    ThemeIcon sampled;sampled.kind=0;sampled.item=25;
    sampled.texture.id=9101;sampled.texture.material=surface.borrowedMaterials[9101];
    ThemeIcon resolved=sampled;
    resolved.texture.id=9102;resolved.texture.material=surface.borrowedMaterials[9102];
    resolved.texture.u0=.25f;resolved.texture.v0=.125f;resolved.texture.u1=.625f;resolved.texture.v1=.875f;
    resolved.width=40;resolved.height=20;
    activeItemLookup=&lookupProbe;lookup=&LookupFixtureItem;lookupProbe.reply=&resolved;
    auto requestedHeldItem=[&]() {return lookupProbe.requests==std::vector<int>{25};};
    auto registryArtwork=[&]() {
        const auto* icon=drawnTexture(9102);
        return requestedHeldItem()&&icon&&inside(icon->rect,layout.cards.front().bounds)&&icon->rect.w==24&&icon->rect.h==12&&
            icon->uv==std::array<float,4>{.25f,.125f,.625f,.875f}&&symbols(0)&&
            surface.texturedDrawIds==std::vector<int>{9103,9102};
    };
    theme.icons={sampled};theme.icons.front().item=13;draw();
    Check(registryArtwork()&&drawnTexture(9102)->rect.x==830&&drawnTexture(9102)->rect.y==944,
          "an unsampled held item looks up its own ID and fits the actual rectangular slot after decorations");
    theme.icons={sampled};draw();
    Check(lookupProbe.requests.empty()&&drawnTexture(9101)&&!drawnTexture(9102)&&symbols(0),
          "a valid sampled bitmap keeps priority and avoids an inventory registry lookup");
    theme.icons.front().texture.material="vgui/stale_atlas";draw();
    Check(registryArtwork(),"a sampled texture with a stale material retries the held item before drawing a generic symbol");
    theme.icons={sampled};theme.icons.front().color.a=0;draw();
    Check(registryArtwork(),"hidden sampled art does not block a visible registry icon for the held item");
    theme.icons={sampled};theme.icons.front().usingFont=true;
    theme.icons.front().font=900;theme.icons.front().glyph=L'P';draw();
    Check(registryArtwork()&&!Printed(surface,L"P"),"an unsupported sampled glyph retries the held item using the inventory registry");
    lookupProbe.reply=nullptr;draw();
    Check(requestedHeldItem()&&!drawnTexture(9101)&&!drawnTexture(9102)&&symbols(1),
          "an unavailable registry icon preserves the visible generic symbol");
    ThemeIcon wrongItem=resolved;wrongItem.item=13;lookupProbe.reply=&wrongItem;draw();
    Check(requestedHeldItem()&&!drawnTexture(9102)&&symbols(1),
          "registry artwork for a different item cannot replace the held item's symbol");

    ThemeIcon resolvedGlyph=resolved;resolvedGlyph.usingFont=true;resolvedGlyph.font=900;resolvedGlyph.glyph=L'Q';
    resolvedGlyph.width=6;resolvedGlyph.height=12;
    lookupProbe.reply=&resolvedGlyph;draw();
    Check(requestedHeldItem()&&symbols(0)&&std::any_of(surface.textDraws.begin(),surface.textDraws.end(),
              [](const TextRecord& record){return record.text==L"Q"&&record.font==900;}),
          "a fitting registry font icon prints its exact glyph and font without a generic symbol");
    resolvedGlyph.height=21;draw();
    Check(requestedHeldItem()&&!Printed(surface,L"Q")&&symbols(1),
          "a registry glyph that cannot fit leaves the generic symbol without substituting another font");

    theme.available=false;lookupProbe.reply=&resolved;draw();
    Check(requestedHeldItem()&&drawnTexture(9102)&&symbols(0)&&
          surface.fills.front().color==std::array<int,4>{7,10,8,218},
          "the complete game-theme fallback card still draws registry art when inventory has room");
    settings.theme=HudTheme::Current;draw();
    Check(lookupProbe.requests.empty()&&!drawnTexture(9102)&&symbols(1),
          "current design ignores the registry callback and retains its inventory symbol");

    settings.theme=HudTheme::GameHud;theme.available=true;theme.icons.clear();
    normal.items={};normal.items[1]=widget({154,40,24,20});draw();
    Check(registryArtwork()&&drawnTexture(9102)->rect.x==826&&drawnTexture(9102)->rect.y==944,
          "a nearby themed slot still supplies its inferred spacing and rectangular dimensions for registry art");

    normal.items={};items(25,12,15);
    std::array<ThemeIcon,3> restored{resolved,resolved,resolved};
    const std::array<int,3> held{25,12,15};
    for(std::size_t kind=0;kind<restored.size();++kind) {
        restored[kind].item=held[kind];
        restored[kind].width=restored[kind].height=20;
        lookupProbe.itemReplies[kind]=&restored[kind];
    }
    lookupProbe.reply=nullptr;
    auto themedRow=[&](bool onRight) {
        if(lookupProbe.requests.size()!=held.size() ||
           !std::is_permutation(lookupProbe.requests.begin(),lookupProbe.requests.end(),held.begin()) || !symbols(0))return false;
        const Rect bar{700+normal.health.rect.x,900+normal.health.rect.y,normal.health.rect.w,normal.health.rect.h};
        std::vector<Rect> icons;
        for(const auto& record:surface.textureDrawsWithUv)if(record.id==9102)icons.push_back(record.rect);
        if(icons.size()!=3)return false;
        std::sort(icons.begin(),icons.end(),[](const Rect& a,const Rect& b){return a.x<b.x;});
        for(std::size_t n=0;n<icons.size();++n) {
            const auto& icon=icons[n];
            const int gap=bar.y-icon.y-icon.h;
            if(!inside(icon,layout.cards.front().bounds)||icon.w<=12||icon.h!=icon.w||
               icon.x<bar.x||icon.x+icon.w>bar.x+bar.w||gap<0||gap>4||icon.y!=icons.front().y||
               (n>0&&icons[n-1].x+icons[n-1].w>icon.x))return false;
        }
        return onRight?bar.x+bar.w-icons.back().x-icons.back().w<=4:icons.front().x-bar.x<=4;
    };
    normal.number.alignment=0;normal.health.rect.y=56;draw();
    Check(themedRow(true),"with no native item slots, a left-aligned number leaves a readable registry row above the right side of the actual bar");
    normal.number.alignment=2;normal.health.rect.y=48;draw();
    Check(themedRow(false),"with no native item slots, a right-aligned number leaves a readable registry row above the left side of the actual bar");

    activeItemLookup=nullptr;
    CheckClean(surface);
}
void CheckModelPortraits() {
    using namespace neh;
    Surface surface;Bind(surface);surface.fonts[900]=12;FakeMaterialSystem materials;
    auto asset=[&](int id,const std::string& path) {
        surface.borrowedTextures.insert(id);surface.borrowedMaterials[id]=path;surface.borrowedSizes[id]={64,64};
        surface.existingByPath[path]=id;materials.available.insert(path);
    };
    const std::string a="vgui/l4n_s/model_a",b="vgui/l4n_s/model_b";
    asset(9001,a);asset(9002,a+"_incap");asset(9003,b);
    Renderer renderer;renderer.Initialize(&surface,&materials);
    auto snapshot=Snapshot("Model Survivor",4);snapshot.players.reserve(2);auto& player=snapshot.players.front();
    auto& model=player.modelPortrait;model={1,100,9001,9002,a,a+"_incap"};
    auto layout=Layout();Settings settings;GameHudTheme theme;theme.available=true;
    auto& normal=theme.states[0];normal.ready=true;normal.width=320;normal.height=96;
    normal.portrait.present=true;normal.portrait.rect={6,6,56,64};
    normal.name.present=true;normal.name.rect={70,5,230,16};normal.name.font=900;
    normal.number=normal.name;normal.number.rect={70,25,120,16};normal.health.present=true;normal.health.rect={70,65,230,10};
    theme.states[1]=normal;theme.states[1].portrait.present=false;
    ThemeImage incap;incap.widget.present=true;incap.widget.rect={0,0,320,96};incap.characterIncap=true;
    incap.texture.id=9999;incap.texture.material="vgui/s_panel_namvet_incap";theme.states[1].images={incap};
    theme.states[3]=normal;
    auto draw=[&]() {
        surface.texturedDrawIds.clear();surface.textureDrawsWithUv.clear();surface.texturePaths.clear();
        surface.materialQueries.clear();surface.sizeQueries.clear();surface.printed.clear();g_runtimeCounters={};
        renderer.Draw(snapshot,layout,settings,&theme);
    };
    auto drawn=[&](int id){return std::find(surface.texturedDrawIds.begin(),surface.texturedDrawIds.end(),id)!=surface.texturedDrawIds.end();};
    draw();
    Check(drawn(9001)&&materials.lookups.empty()&&surface.lookupsByPath[a]==0&&
          surface.materialQueries[9001]==1&&surface.sizeQueries[9001]==1&&g_runtimeCounters.modelPortraitDraws==1,
          "current design prefers a validated Neko normal portrait ID without any private or material loading call");
    draw();
    Check(drawn(9001)&&surface.materialQueries[9001]==1&&surface.sizeQueries[9001]==1,
          "current design revalidates the copied model material and dimensions on each draw");
    settings.theme=HudTheme::GameHud;draw();
    Check(drawn(9001)&&g_runtimeCounters.modelPortraitDraws==1,"game HUD portraits use the same actual extra survivor model image");
    player.incapacitated=true;player.health=245;draw();
    Check(drawn(9002)&&!drawn(9001)&&!drawn(9999)&&Printed(surface,L"DOWN 245"),
          "downed game HUD backgrounds use that model's incap image instead of the native source survivor's artwork");
    settings.theme=HudTheme::Current;draw();
    Check(drawn(9001)&&!drawn(9002)&&Printed(surface,L"DOWN 245"),
          "current design keeps its square normal model portrait while downed");
    player.alive=false;draw();
    Check(drawn(9001)&&!drawn(9002)&&Printed(surface,L"DEAD"),"dead model survivors keep their normal portrait and dead status");
    player.alive=true;player.incapacitated=false;player.health=73;
    auto second=player;second.index=9;snapshot.players.push_back(second);layout.cards.push_back({9,0,6,{1050,900,320,96}});draw();
    Check(surface.texturedDrawIds==std::vector<int>{9001,9001}&&surface.materialQueries[9001]==1&&g_runtimeCounters.modelPortraitDraws==2,
          "two players sharing one model share validation without downloading or loading the portrait again");
    snapshot.players[1].modelPortrait={2,200,9003,0,b,b+"_incap"};draw();
    Check(surface.texturedDrawIds==std::vector<int>{9001,9003},
          "different applied models never contaminate one another even when their base survivor character is identical");
    snapshot.players.resize(1);layout.cards.resize(1);model.revision=0;draw();
    Check(!drawn(9001)&&g_runtimeCounters.modelPortraitDraws==0,
          "absent current model metadata immediately restores the normal game portrait despite old model IDs");
    model={3,100,9001,0,a,a+"_incap"};surface.borrowedMaterials[9001]="vgui/l4n_s/reused_other";asset(9004,a);draw();
    Check(drawn(9004)&&!drawn(9001)&&surface.lookupsByPath[a]==1&&materials.lookups[a]==1,
          "a recycled copied ID gets one expected-path lookup and recovers an existing correctly identified image");
    draw();
    Check(drawn(9004)&&surface.lookupsByPath[a]==1&&materials.lookups[a]==1,
          "a recovered model path is reused without repeated FindMaterial or Surface lookup calls");
    surface.borrowedMaterials[9004]="vgui/l4n_s/reused_again";draw();draw();
    Check(!drawn(9004)&&!drawn(9001)&&surface.lookupsByPath[a]==1&&materials.lookups[a]==1,
          "recycled resolved IDs fall back safely without repeating the revision's failed load attempt");
    asset(9005,a);model.normalId=9005;draw();
    Check(drawn(9005)&&surface.lookupsByPath[a]==1,"a newly available live Neko cache ID overrides an earlier negative result");

    renderer.Initialize(&surface,nullptr);const std::string existing="vgui/l4n_s/existing_only";asset(9006,existing);
    model={10,10,0,0,existing,{}};const int createsBefore=surface.textureCreates;draw();
    Check(drawn(9006)&&surface.textureCreates==createsBefore&&g_runtimeCounters.modelPortraitLoadAttempts==0,
          "without a verified MaterialSystem only an already-loaded name-and-size-validated Surface ID is used");
    player.character=-1;const std::string missing="vgui/l4n_s/missing";model={11,11,0,0,missing,{}};draw();draw();
    Check(surface.lookupsByPath[missing]==1&&surface.textureCreates==createsBefore&&g_runtimeCounters.modelPortraitDraws==0,
          "missing model paths are negatively cached and never created without a verified MaterialSystem");
    renderer.Initialize(&surface,&materials);const std::string loaded="vgui/l4n_s/pack..v2 with_space";
    materials.available.insert(loaded);model={20,20,0,0,loaded,{}};const int uploadsBefore=surface.textureFiles;draw();
    const int loadedId=surface.existingByPath[loaded];draw();
    Check(loadedId>0&&drawn(loadedId)&&materials.lookups[loaded]==1&&surface.lookupsByPath[loaded]==1&&
          surface.textureFiles==uploadsBefore+1,
          "a valid previously unloaded model image uses verified MaterialSystem71 and material42 once, then one Surface upload");
    const std::string absent="vgui/l4n_s/absent_asset";model={21,21,0,0,absent,{}};const int uploads=surface.textureFiles;draw();draw();
    Check(materials.lookups[absent]==1&&surface.lookupsByPath[absent]==1&&surface.textureFiles==uploads&&
          g_runtimeCounters.modelPortraitDraws==0,"error materials stay negative for the same revision without checkerboards or repeated loading");
    materials.available.insert(absent);draw();
    Check(materials.lookups[absent]==1&&!drawn(surface.existingByPath[absent]),
          "making a file available does not bypass the negative cache while its model revision is unchanged");
    ++model.revision;draw();
    Check(materials.lookups[absent]==2&&g_runtimeCounters.modelPortraitDraws==1,
          "a new model revision retries a previously missing material exactly once");
    const std::string guarded="vgui/l4n_s/guarded";materials.available.insert(guarded);materials.material.throwCheck=true;
    model={30,30,0,0,guarded,{}};draw();draw();materials.material.throwCheck=false;
    Check(materials.lookups[guarded]==1&&surface.lookupsByPath[guarded]==1&&g_runtimeCounters.modelPortraitDraws==0,
          "an exception from an untrusted returned material is guarded and negatively cached");
    const auto beforeInvalid=surface.textureLookups;model={31,31,0,0,"vgui/l4n_s/../other",{}};draw();
    Check(surface.textureLookups==beforeInvalid,"invalid model paths never reach an asset-loading API");
    Surface another;Bind(another);renderer.Initialize(&another,&materials);model={40,40,0,0,loaded,{}};
    const auto priorLoads=materials.lookups[loaded];
    renderer.Draw(snapshot,layout,settings,&theme);
    Check(another.lookupsByPath[loaded]==1&&materials.lookups[loaded]==priorLoads+1&&another.textureFiles==1,
          "changing Surface discards old model ID caches and resolves the new rendering context");
    Check(!materials.invalidArguments,"verified MaterialSystem and material methods retain their exact independent x86 call shapes");
    CheckClean(surface);CheckClean(another);g_runtimeCounters={};
}
void CheckModelMemoryQueries() {
    using namespace neh;
    Surface surface;Bind(surface);
    Renderer renderer;renderer.Initialize(&surface);
    neh::Snapshot snapshot;snapshot.inGame=true;snapshot.localIndex=snapshot.viewIndex=1;
    neh::Layout layout;Settings settings;
    for(int n=0;n<8;++n) {
        const auto path="vgui/l4n_s/query_region_"+std::to_string(n);
        const int id=9200+n;
        surface.borrowedTextures.insert(id);surface.borrowedMaterials[id]=path;surface.borrowedSizes[id]={64,64};
        surface.existingByPath[path]=id;
        Player player;player.index=n+2;player.name="Region survivor";player.character=-1;
        player.alive=true;player.health=100;player.maxHealth=100;
        player.modelPortrait={static_cast<std::uint64_t>(100+n),0,id,0,path,{}};
        snapshot.players.push_back(player);layout.cards.push_back({n+2,0,n,{n*320,900,320,96}});
    }
    g_runtimeCounters={};surface.replaceSizeMethodOnQuery=true;
    Check(renderer.Draw(snapshot,layout,settings),"eight validated model portraits submit a complete draw");
    const auto queries=g_runtimeCounters.virtualQueries;
    std::cout<<"Model region queries for eight portraits: "<<queries<<'\n';
    Check(g_runtimeCounters.modelPortraitDraws==8&&surface.materialQueries.size()==8&&surface.sizeQueries.size()==8,
          "region sharing still reads the live name and size for every distinct borrowed portrait");
    Check(queries>0&&queries<=8,
          "one draw shares bounded object, vtable and code-region queries across distinct model portraits");
    Check(surface.replacementSizeCalls==8,
          "region sharing re-reads a vtable method replaced by an earlier callback within the same draw");
    surface.replaceSizeMethodOnQuery=false;surface.methods[30]=reinterpret_cast<void*>(&TextureSize);
    g_runtimeCounters={};surface.materialQueries.clear();surface.sizeQueries.clear();
    renderer.Draw(snapshot,layout,settings);
    Check(g_runtimeCounters.virtualQueries>0&&g_runtimeCounters.virtualQueries<=8&&
          surface.materialQueries.size()==8&&surface.sizeQueries.size()==8,
          "the next draw performs fresh page queries and live asset validation instead of retaining object or method pointers");
    surface.throwMaterialQuery=true;surface.texturedDrawIds.clear();g_runtimeCounters={};
    renderer.Draw(snapshot,layout,settings);
    Check(surface.texturedDrawIds.empty()&&g_runtimeCounters.modelPortraitDraws==0,
          "guarded material-query exceptions never authorize stale portrait rendering");
    surface.throwMaterialQuery=false;surface.texturedDrawIds.clear();g_runtimeCounters={};
    renderer.Draw(snapshot,layout,settings);
    Check(g_runtimeCounters.virtualQueries>0&&g_runtimeCounters.modelPortraitDraws==8&&surface.texturedDrawIds.size()==8,
          "a draw after guarded asset exceptions starts fresh and recovers the current portraits");
    CheckClean(surface);

    Surface guardedSurface;Bind(guardedSurface);FakeMaterialSystem materials;
    Renderer guardedRenderer;guardedRenderer.Initialize(&guardedSurface,&materials);
    auto guardedSnapshot=::Snapshot("Guarded material",-1);auto guardedLayout=::Layout();
    guardedSnapshot.players.reserve(2);
    auto& model=guardedSnapshot.players.front().modelPortrait;
    auto* memory=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    Check(memory!=nullptr,"a dedicated material page is available for protection-change regressions");
    auto* material=new(memory) FakeMaterial;material->error=false;materials.returnedMaterial=material;
    unsigned revision=500;
    auto attempt=[&]() {
        const auto path="vgui/l4n_s/protection_"+std::to_string(++revision);
        materials.available.insert(path);model={revision,0,0,0,path,{}};
        guardedSurface.texturedDrawIds.clear();g_runtimeCounters={};
        guardedRenderer.Draw(guardedSnapshot,guardedLayout,settings);
    };
    auto protect=[&](void* page,DWORD access) {
        DWORD previous=0;Check(VirtualProtect(page,4096,access,&previous)!=FALSE,
                              "the dedicated test page accepts the requested protection");
    };
    attempt();Check(g_runtimeCounters.modelPortraitDraws==1&&material->checks==1,
                    "a readable material page calls its current executable method");
    protect(memory,PAGE_NOACCESS);attempt();
    Check(g_runtimeCounters.modelPortraitDraws==0&&guardedSurface.texturedDrawIds.empty(),
          "a previously readable material changed to no-access is rejected on the next draw");
    protect(memory,PAGE_READWRITE|PAGE_GUARD);attempt();
    MEMORY_BASIC_INFORMATION page{};VirtualQuery(memory,&page,sizeof(page));
    Check(g_runtimeCounters.modelPortraitDraws==0&&(page.Protect&PAGE_GUARD)!=0,
          "guarded object pages are rejected without consuming their guard protection");
    protect(memory,PAGE_READWRITE);attempt();
    Check(g_runtimeCounters.modelPortraitDraws==1&&material->checks==2,
          "restoring material readability recovers on the next draw without stale denied-region entries");
    void* nonExecutable=VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
    Check(nonExecutable!=nullptr,"a dedicated method page is available for execution-protection regressions");
    material->methods[42]=nonExecutable;attempt();
    Check(g_runtimeCounters.modelPortraitDraws==0&&material->checks==2,
          "a current vtable method in a readable non-executable page is rejected instead of invoked");
    protect(nonExecutable,PAGE_EXECUTE_READ|PAGE_GUARD);attempt();
    VirtualQuery(nonExecutable,&page,sizeof(page));
    Check(g_runtimeCounters.modelPortraitDraws==0&&(page.Protect&PAGE_GUARD)!=0,
          "guarded executable method pages are rejected without entering them");
    protect(nonExecutable,PAGE_NOACCESS);attempt();
    Check(g_runtimeCounters.modelPortraitDraws==0,
          "no-access method pages never reuse executable permission metadata from an earlier draw");
    material->methods[42]=reinterpret_cast<void*>(&IsErrorMaterial);material->throwCheck=true;attempt();
    Check(g_runtimeCounters.modelPortraitDraws==0&&material->checks==3,
          "a live replacement method still runs under the existing exception guard");
    material->throwCheck=false;attempt();
    Check(g_runtimeCounters.modelPortraitDraws==1&&material->checks==4,
          "the next draw recovers after an actual material method exception and re-reads its vtable");
    auto second=guardedSnapshot.players.front();second.index=9;
    const std::string revokedPath="vgui/l4n_s/revoked_during_draw";
    materials.available.insert(revokedPath);second.modelPortrait={700,0,0,0,revokedPath,{}};
    guardedSnapshot.players.push_back(second);guardedLayout.cards.push_back({9,0,1,{1050,900,320,96}});
    material->revokePageAfterCheck=memory;attempt();
    VirtualQuery(memory,&page,sizeof(page));
    Check(g_runtimeCounters.modelPortraitDraws==1&&page.Protect==PAGE_NOACCESS,
          "revoking an already-cached object page within a draw still guards its next actual memory read and omits only that portrait");
    protect(memory,PAGE_READWRITE);material->revokePageAfterCheck=nullptr;
    ++guardedSnapshot.players[1].modelPortrait.revision;attempt();
    Check(g_runtimeCounters.modelPortraitDraws==2&&material->checks==7,
          "both portraits recover on the following draw after a same-draw guarded-read fault invalidates region metadata");
    material->~FakeMaterial();
    Check(VirtualFree(memory,0,MEM_RELEASE)!=FALSE&&VirtualFree(nonExecutable,0,MEM_RELEASE)!=FALSE,
          "dedicated protection-test pages are released after the renderer stops using them");
    CheckClean(guardedSurface);g_runtimeCounters={};
}
} // namespace

int main() {
    static_assert(sizeof(void*)==4,"The game ABI regression must run as x86.");
    neh::Settings settings;
    auto layout=Layout();

    Surface created; Bind(created);
    neh::Renderer renderer; renderer.Initialize(&created);
    auto textured=Snapshot("Texture Survivor",0);
    Check(renderer.Draw(textured,layout,settings),"a submitted current-design card reports success to its caller");
    CheckClean(created);
    Check(created.fontCalls==2&&created.glyphCalls==2,"production draw creates both valid font glyph sets");
    Check(created.textSizeCalls>0&&created.textColorCalls>0,"text uses the correct size and integer-color entry points");
    Check(created.fillCalls>=8&&created.borderCalls>=4,"card, health and all inventory symbols use production rectangles");
    Check(!created.colors.empty()&&created.colors.front()==std::array<int,4>{7,10,8,218},"background RGBA reaches the integer color method intact");
    Check(Printed(created,L"+73")&&Printed(created,L"Texture Survivor"),"production text preserves health and survivor name");
    Check(created.textureLookups==1&&created.textureCreates==1&&created.textureFiles==1&&created.textureSizes==1&&created.textureDraws==1,
          "missing cached portrait loads a texture and draws it");
    Check(created.texturePaths.size()==2&&created.texturePaths[0]=="vgui/s_panel_lobby_gambler"&&
          created.texturePaths[1]==created.texturePaths[0],"portrait lookup and load use the intended character path");
    Check(created.flushCalls==1&&neh::g_drawTrace.surfaceSlot==24&&neh::g_drawTrace.playerIndex==6,
          "completed draw flushes text and retains useful diagnostic trace");
    Check(renderer.Draw(textured,layout,settings),"a warm current-design submission keeps reporting success");
    CheckClean(created);
    Check(created.fontCalls==2&&created.textureCreates==1&&created.textureLookups==1&&created.textureValidations==1&&created.textureDraws==2,
          "second draw reuses fonts and validates its cached portrait");

    Surface existing; existing.textureMode=TextureMode::Existing; Bind(existing);
    renderer.Initialize(&existing);
    renderer.Draw(Snapshot("Existing Portrait",2),layout,settings);
    CheckClean(existing);
    Check(existing.textureLookups==1&&existing.textureCreates==0&&existing.textureFiles==0&&existing.textureDraws==1,
          "existing portrait texture is reused without creation or file upload");
    Check(existing.texturePaths.front()=="vgui/s_panel_lobby_coach","character index selects the matching portrait path");

    // Verified against the installed base-game/DLC material and texture paths.
    // The L4D1 set has NO s_panel_lobby_* materials; a positive texture ID can
    // still represent the engine's error checkerboard, so check exact paths.
    const std::array<const char*,4> classicPaths={
        "vgui/s_panel_namvet","vgui/s_panel_teenangst",
        "vgui/s_panel_biker","vgui/s_panel_manager"
    };
    for(int character=4;character<8;++character) {
        Surface classic; Bind(classic);
        neh::Renderer classicRenderer;classicRenderer.Initialize(&classic);
        classicRenderer.Draw(Snapshot("Classic Survivor",character),layout,settings);
        CheckClean(classic);
        const std::string expected=classicPaths[static_cast<std::size_t>(character-4)];
        Check(classic.texturePaths==std::vector<std::string>{expected,expected},
              "each L4D1 portrait lookup and upload uses its real non-lobby material path");
        Check(classic.textureDraws==1&&!Printed(classic,L"C"),
              "each L4D1 portrait reaches the production textured draw without initial fallback");
    }

    Surface joining;Bind(joining);
    neh::Renderer joiningRenderer;joiningRenderer.Initialize(&joining);
    auto identity=Snapshot("First Player",4);
    auto joiningLayout=Layout();
    joiningRenderer.Draw(identity,joiningLayout,settings);
    identity.players[0].character=5;identity.players[0].name="Replacement Player";
    joiningRenderer.Draw(identity,joiningLayout,settings);
    identity.players[0].character=4;identity.players[0].index=9;identity.players[0].name="New Player";
    joiningLayout.cards[0].playerIndex=9;
    joiningRenderer.Draw(identity,joiningLayout,settings);
    CheckClean(joining);
    Check(joining.texturedDrawIds==std::vector<int>{501,502,501},
          "same-slot character change selects Zoey and a new Bill player selects Bill again");
    Check(joining.textureLookups==2&&joining.textureCreates==2&&joining.textureValidations==1,
          "joining players reuse the texture for their character instead of downloading a personal avatar");
    Check(Printed(joining,L"Replacement Player")&&Printed(joining,L"New Player"),
          "takeover and joining snapshots update the visible player name with the portrait selection");

    Surface missing; missing.textureMode=TextureMode::Missing; Bind(missing);
    renderer.Initialize(&missing);
    renderer.Draw(Snapshot("Fallback Survivor",3),layout,settings);
    CheckClean(missing);
    Check(missing.textureCreates==1&&missing.textureDraws==0&&Printed(missing,L"F")&&Printed(missing,L"Fallback Survivor"),
          "failed texture creation draws the survivor initial and name through production text");

    Surface unknown; Bind(unknown); renderer.Initialize(&unknown);
    renderer.Draw(Snapshot("Guest Survivor",-1),layout,settings);
    CheckClean(unknown);
    Check(unknown.textureLookups==0&&unknown.textureCreates==0&&unknown.textureDraws==0&&Printed(unknown,L"G"),
          "unknown character bypasses texture methods and uses an initial");

    Surface noGlyphs; noGlyphs.rejectGlyphs=true; Bind(noGlyphs); renderer.Initialize(&noGlyphs);
    Check(!renderer.Draw(textured,layout,settings),"glyph setup failure reports no card submission");
    CheckClean(noGlyphs);
    Check(noGlyphs.glyphCalls==2&&noGlyphs.fillCalls==0&&noGlyphs.flushCalls==0,
          "font setup failure prevents drawing with invalid glyph state");
    noGlyphs.rejectGlyphs=false;
    Check(renderer.Draw(textured,layout,settings),"glyph initialization can recover and submit a card on the next draw");

    Surface noFonts;noFonts.nextFont=0;Bind(noFonts);renderer.Initialize(&noFonts);
    Check(!renderer.Draw(textured,layout,settings)&&noFonts.fontCalls==2&&noFonts.glyphCalls==0&&
          noFonts.fillCalls==0&&noFonts.flushCalls==0,
          "a zero CreateFont result reports failure before any card pixels are submitted");
    Check(renderer.Draw(textured,layout,settings),"a subsequent valid font allocation recovers card submission");

    neh::Renderer noSurface;
    Check(!noSurface.Draw(textured,layout,settings),"an uninitialized surface reports no card submission");
    Surface empty;Bind(empty);renderer.Initialize(&empty);auto emptyLayout=Layout();emptyLayout.cards.clear();
    Check(!renderer.Draw(textured,emptyLayout,settings)&&empty.fontCalls==0&&empty.fillCalls==0&&empty.flushCalls==0,
          "an empty layout reports no card submission without touching the surface");
    auto absent=Snapshot("Absent survivor",0);absent.players.clear();
    Check(!renderer.Draw(absent,layout,settings)&&empty.fillCalls==0&&empty.flushCalls==0,
          "layout IDs missing from the current roster cannot report a card submission");
    auto partiallyMatched=Layout();partiallyMatched.cards.insert(partiallyMatched.cards.begin(),{31,0,0,{350,900,320,96}});
    Check(renderer.Draw(textured,partiallyMatched,settings),
          "one matched current player is enough for success when another layout target is absent");

    Surface suppressed; Bind(suppressed); renderer.Initialize(&suppressed);
    layout.suppressed=true;
    Check(!renderer.Draw(textured,layout,settings),"a suppressed layout reports no card submission");
    Check(suppressed.fontCalls==0&&suppressed.fillCalls==0&&suppressed.flushCalls==0,
          "suppressed layout makes no surface calls");
    CheckHealthRendering();
    CheckGameHudThemeRendering();
    CheckLegacyHealthInsets();
    CheckBorrowedTextureLifetimes();
    CheckHeldInventoryAndStateFallbacks();
    CheckModelPortraits();
    CheckModelMemoryQueries();
    std::cout<<"Renderer ABI: "<<checks<<" checks passed (actual Surface031 slots 11/19/72)\n";
    return 0;
}
