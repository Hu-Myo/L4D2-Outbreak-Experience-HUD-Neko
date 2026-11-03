#include "paint_context.hpp"
#include <cstdio>
#include <cstring>
namespace {
struct Fake {void** table;int depth=0;int pushes=0;int pops=0;};
void __fastcall Push(void* p,void*,std::uint32_t,bool){auto f=static_cast<Fake*>(p);++f->depth;++f->pushes;}
void __fastcall Pop(void* p,void*,std::uint32_t){auto f=static_cast<Fake*>(p);--f->depth;++f->pops;}
void Good(void*){}
neh::DrawTrace captured;
int Capture(){captured=neh::g_drawTrace;return EXCEPTION_EXECUTE_HANDLER;}
void Fault(void*){neh::g_drawTrace.stage="fixture_draw";neh::g_drawTrace.surfaceSlot=72;RaiseException(0xE0000001,0,0,nullptr);}
bool FaultIsCaught(Fake* fake){__try{neh::WithPaintContext(fake,1,Fault,nullptr);return false;}__except(Capture()){return true;}}
}
int main(){void* table[10]{};table[8]=reinterpret_cast<void*>(&Push);table[9]=reinterpret_cast<void*>(&Pop);Fake fake{table};
    neh::WithPaintContext(&fake,1,Good,nullptr);
    if(fake.depth!=0||fake.pushes!=1||fake.pops!=1)return 1;
    if(!FaultIsCaught(&fake)||fake.depth!=0||fake.pushes!=2||fake.pops!=2)return 2;
    if(std::strcmp(captured.stage,"fixture_draw")!=0||captured.surfaceSlot!=72||neh::g_drawTrace.surfaceSlot!=9)return 3;
    std::puts("Paint context returns to its original state after success and native fault.");return 0;
}
