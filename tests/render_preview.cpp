#define NOMINMAX
#include <windows.h>
#include "types.hpp"
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>

// A synthetic Surface031 records production renderer output. It never loads
// the game or Neko. Portraits deliberately fall back to initials in this mock.
namespace {
struct Surface {
    void** table;
    int r=255,g=255,b=255,a=255,x=0,y=0,offsetY=0;
    unsigned long font=0,nextFont=1;
    std::map<unsigned long,int> fonts;
    std::ostringstream svg;
};
Surface* S(void* p){return static_cast<Surface*>(p);}
std::string Escape(const std::string& text) {
    std::string out;
    for(char c:text) {if(c=='&')out+="&amp;";else if(c=='<')out+="&lt;";else if(c=='>')out+="&gt;";else if(c=='\"')out+="&quot;";else out+=c;}
    return out;
}
std::string Utf8(const wchar_t* text,int length) {
    int n=WideCharToMultiByte(CP_UTF8,0,text,length,nullptr,0,nullptr,nullptr);
    std::string result(n,'\0');WideCharToMultiByte(CP_UTF8,0,text,length,result.data(),n,nullptr,nullptr);return result;
}
void __fastcall SetColor(void* self,void*,int r,int g,int b,int a){auto s=S(self);s->r=r;s->g=g;s->b=b;s->a=a;}
void Rect(void* self,int x,int y,int right,int bottom,bool border) {
    auto s=S(self);
    s->svg<<"<rect x=\""<<x<<"\" y=\""<<y+s->offsetY<<"\" width=\""<<right-x<<"\" height=\""<<bottom-y
          <<"\" fill=\""<<(border?"none":"rgb("+std::to_string(s->r)+","+std::to_string(s->g)+","+std::to_string(s->b)+")")
          <<"\" opacity=\""<<s->a/255.0<<"\"";
    if(border)s->svg<<" stroke=\"rgb("<<s->r<<','<<s->g<<','<<s->b<<")\" stroke-width=\"1\"";
    s->svg<<"/>\n";
}
void __fastcall Fill(void* self,void*,int x,int y,int r,int b){Rect(self,x,y,r,b,false);}
void __fastcall Border(void* self,void*,int x,int y,int r,int b){Rect(self,x,y,r,b,true);}
void __fastcall SetFont(void* self,void*,unsigned long f){S(self)->font=f;}
void __fastcall SetPos(void* self,void*,int x,int y){S(self)->x=x;S(self)->y=y;}
void __fastcall Print(void* self,void*,const wchar_t* text,int n,int){auto s=S(self);int size=s->fonts[s->font];
    s->svg<<"<text x=\""<<s->x<<"\" y=\""<<s->y+s->offsetY+size<<"\" font-size=\""<<size
          <<"\" fill=\"rgb("<<s->r<<','<<s->g<<','<<s->b<<")\" opacity=\""<<s->a/255.0
          <<"\" font-family=\"Segoe UI, sans-serif\">"<<Escape(Utf8(text,n))<<"</text>\n";}
void __fastcall Flush(void*,void*){}
int __fastcall TextureId(void*,void*,const char*){return 0;}
int __fastcall CreateTexture(void*,void*,bool){return 0;}
unsigned long __fastcall PreviewCreateFont(void* self,void*){return S(self)->nextFont++;}
bool __fastcall SetGlyph(void* self,void*,unsigned long f,const char*,int h,int,int,int,int,int,int){S(self)->fonts[f]=h;return true;}
void __fastcall TextSize(void* self,void*,unsigned long f,const wchar_t* text,int* w,int* h){*h=S(self)->fonts[f];*w=static_cast<int>(std::wcslen(text)*(*h)*0.58);}
void Heading(Surface& s,int y,const std::string& title,const std::string& detail) {
    s.svg<<"<text x=\"28\" y=\""<<y<<"\" font-family=\"Segoe UI,sans-serif\" font-size=\"23\" fill=\"#eef2e6\">"<<Escape(title)<<"</text>\n";
    s.svg<<"<text x=\"28\" y=\""<<y+27<<"\" font-family=\"Segoe UI,sans-serif\" font-size=\"15\" fill=\"#9daa9b\">"<<Escape(detail)<<"</text>\n";
}
} // namespace
int main(int argc,char** argv) {
    if(argc!=2){std::cerr<<"Usage: render_preview.exe output.svg\n";return 1;}
    std::array<void*,80> table{};
    table[11]=reinterpret_cast<void*>(&SetColor);table[12]=reinterpret_cast<void*>(&Fill);table[14]=reinterpret_cast<void*>(&Border);
    table[17]=reinterpret_cast<void*>(&SetFont);table[19]=reinterpret_cast<void*>(&SetColor);table[20]=reinterpret_cast<void*>(&SetPos);
    table[22]=reinterpret_cast<void*>(&Print);table[24]=reinterpret_cast<void*>(&Flush);table[25]=reinterpret_cast<void*>(&TextureId);
    table[34]=reinterpret_cast<void*>(&CreateTexture);table[63]=reinterpret_cast<void*>(&PreviewCreateFont);table[64]=reinterpret_cast<void*>(&SetGlyph);
    table[72]=reinterpret_cast<void*>(&TextSize);
    Surface surface{};surface.table=table.data();
    surface.svg<<"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1600\" height=\"930\" viewBox=\"0 0 1600 930\">\n"
               <<"<rect width=\"1600\" height=\"930\" fill=\"#111712\"/>\n";
    Heading(surface,38,u8"Neko 추가 생존자 HUD · 예상 배치",u8"실제 배치·그리기 코드로 생성한 참고 화면입니다. 게임 캡처가 아니며 초상화는 임시 문자입니다.");
    const char* names[]={"You","Bill","Louis","Zoey","Rochelle","Steve","Nick","Ellis","Coach","Francis"};
    neh::Renderer renderer;renderer.Initialize(&surface);
    for(int scenario=0;scenario<3;++scenario) {
        surface.offsetY=96+scenario*265;
        const int count=scenario==0?7:9;
        neh::Snapshot snap;snap.inGame=true;snap.localIndex=snap.viewIndex=1;
        snap.hasSelfHudBounds=true;snap.selfHudBounds={1390,154,170,70};
        for(int i=1;i<=count+1;++i){neh::Player p;p.index=i;p.name=names[i-1];p.health=100-i*6;p.maxHealth=100;p.alive=true;p.character=-1;p.medical=12;p.pills=i%2?15:0;p.throwable=i%3?13:0;snap.players.push_back(p);}
        std::vector<neh::HudSlot> native;
        for(int i=0;i<4;++i)native.push_back({static_cast<unsigned>(100+i),2+i,{28+i*151,160,145,64},true});
        neh::Settings cfg;cfg.overflow=scenario==2;
        auto layout=neh::PlanLayout(snap,native,cfg,1600,265);
        if(layout.cards.size()!=(scenario==2?5u:3u)){std::cerr<<"Unexpected preview layout: "<<layout.reason;return 2;}
        const std::string title=scenario==0?u8"팀원 7명 + 본인 1명 | 초과 표시 끄기":scenario==1?u8"팀원 9명 + 본인 1명 | 초과 표시 끄기":u8"팀원 9명 + 본인 1명 | 초과 표시 켜기";
        Heading(surface,surface.offsetY+25,title,scenario==2?u8"8·9번째 팀원을 윗줄에 표시합니다.":scenario==1?u8"아랫줄은 7명까지 유지하고, 초과 팀원 2명은 숨깁니다.":u8"기존 4칸 옆에 5·6·7번째 팀원을 추가합니다.");
        neh::Layout original;
        for(const auto& slot:native)original.cards.push_back({slot.playerIndex,0,slot.playerIndex-1,slot.bounds});
        renderer.Draw(snap,original,cfg);
        renderer.Draw(snap,layout,cfg);
        neh::Layout local;local.cards.push_back({1,0,0,snap.selfHudBounds});renderer.Draw(snap,local,cfg);
        surface.svg<<"<text x=\"28\" y=\""<<surface.offsetY+247<<u8"\" font-size=\"13\" fill=\"#8b978a\" font-family=\"Segoe UI,sans-serif\">왼쪽 4칸은 기존 HUD의 설명용 표현입니다. 실제 플러그인은 기존 4칸과 본인 HUD를 변경하지 않습니다.</text>\n";
        surface.svg<<"<line x1=\"28\" y1=\""<<surface.offsetY+259<<"\" x2=\"1570\" y2=\""<<surface.offsetY+259<<"\" stroke=\"#303b30\"/>\n";
    }
    surface.svg<<"</svg>\n";
    std::ofstream out(argv[1],std::ios::binary);out<<surface.svg.str();
    std::cout<<"Production renderer preview recorded successfully.\n";
    return out?0:3;
}
