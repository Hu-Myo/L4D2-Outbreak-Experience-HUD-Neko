#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#pragma warning(push)
#pragma warning(disable:4100)
#include "../sdk/l4n_plugin.h"
#pragma warning(pop)
int main(int argc,char** argv) {
    if(argc!=2)return 1;
    auto module=LoadLibraryA(argv[1]);if(!module){std::printf("Load failed: %lu\n",GetLastError());return 2;}
    auto get=reinterpret_cast<GetL4NPluginInstanceFunc>(GetProcAddress(module,"GetL4NPluginInstance"));
    if(!get)return 3;
    auto plugin=get();
    if(!plugin||plugin->GetInterfaceVersion()!=2||std::strcmp(plugin->GetName(),"NekoExtraHUD")!=0)return 4;
    if(!plugin->RequestHudMenu(true)||!std::strstr(plugin->RequestHudMenu(false),"callback"))return 5;
    // Deliberately do not invoke game/module callbacks outside the game.
    FreeLibrary(module);
    std::puts("x86 DLL export, official SDK vtable and menu smoke tests passed; no game callbacks invoked.");return 0;
}
