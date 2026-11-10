#include "../src/runtime.cpp"
#include <iostream>
#include <cstdlib>
#include <limits>

int assertions=0;
void Check(bool ok,const char* label) {
    if (!ok) { std::cerr<<"FAIL: "<<label<<"\n"; std::exit(1); }
    ++assertions;
}
namespace {
// The current L4D2 client has no Release entry in IClientNetworkable.
// Keep this fixture independent of the adapter's slot constants: slot 1 and 10
// were verified in the installed client.dll, not inferred from an old header.
struct FakeNetworkable {
    void** vtable;
    neh::ClientClass* klass;
    void* data;
};
struct FakeEntityList {
    void** vtable;
    FakeNetworkable* resource;
    int index;
    std::unordered_map<int,FakeNetworkable*> players;
};
struct FakeEngine {
    void** vtable;
    bool inGame=true,console=false;
    int localIndex=1,maxPlayers=12;
    int width=2560,height=1440;
};
struct FakeVgui;
std::uint32_t __fastcall FakePanelToHandle(FakeVgui*,void*,std::uint32_t);
std::uint32_t __fastcall FakeHandleToPanel(FakeVgui*,void*,std::uint32_t);
struct FakeVgui {
    void** vtable;
    void* methods[17]{};
    std::uint32_t nextHandle=0;
    std::unordered_map<std::uint32_t,std::uint32_t> current,handles,verified;
    int captures=0,resolves=0,invalidCaptures=0;
    FakeVgui() : vtable(methods) {
        methods[15]=reinterpret_cast<void*>(&FakePanelToHandle);
        methods[16]=reinterpret_cast<void*>(&FakeHandleToPanel);
    }
    void Add(std::uint32_t raw) {
        if(raw&&current.find(raw)==current.end()) {
            const auto handle=nextHandle++;
            current[raw]=handle;handles[handle]=raw;
        }
    }
    void Retire(std::uint32_t raw) {
        const auto found=current.find(raw);
        if(found!=current.end()) {handles.erase(found->second);current.erase(found);}
        verified.erase(raw);
    }
    void Clear() {current.clear();handles.clear();verified.clear();captures=resolves=invalidCaptures=0;}
    bool Live(std::uint32_t raw) const {return current.find(raw)!=current.end();}
    bool Resolved(std::uint32_t raw) const {
        const auto live=current.find(raw),resolved=verified.find(raw);
        return live!=current.end()&&resolved!=verified.end()&&live->second==resolved->second;
    }
};
std::uint32_t __fastcall FakePanelToHandle(FakeVgui* self,void*,std::uint32_t raw) {
    ++self->captures;
    const auto found=self->current.find(raw);
    if(found==self->current.end()) {++self->invalidCaptures;return 0xffffffff;}
    return found->second;
}
std::uint32_t __fastcall FakeHandleToPanel(FakeVgui* self,void*,std::uint32_t handle) {
    ++self->resolves;
    const auto found=self->handles.find(handle);
    if(found==self->handles.end())return 0;
    const auto live=self->current.find(found->second);
    if(live==self->current.end()||live->second!=handle)return 0;
    self->verified[found->second]=handle;
    return found->second;
}
void __fastcall FakeScreen(FakeEngine* self,void*,int* width,int* height) { *width=self->width; *height=self->height; }
int unexpectedCalls=0;
void* __fastcall FakeClass(FakeNetworkable* self,void*) { return self->klass; }
void* __fastcall FakeData(FakeNetworkable* self,void*) { return self->data; }
void* __fastcall WrongSlot(void*,void*) { ++unexpectedCalls; return nullptr; }
void* __fastcall FakeEntity(FakeEntityList* self,void*,int index) {
    if(index==self->index)return self->resource;
    const auto found=self->players.find(index);
    return found!=self->players.end()?found->second:nullptr;
}
int __fastcall FakeHighest(FakeEntityList* self,void*) { return self->index; }
bool __fastcall FakeInGame(FakeEngine* self,void*) { return self->inGame; }
bool __fastcall FakeConsole(FakeEngine* self,void*) { return self->console; }
int __fastcall FakeLocal(FakeEngine* self,void*) { return self->localIndex; }
int __fastcall FakeMaxPlayers(FakeEngine* self,void*) { return self->maxPlayers; }
bool __fastcall FakePlayerInfo(FakeEngine*,void*,int index,void* output) {
    const auto name="Player "+std::to_string(index);
    std::memcpy(static_cast<unsigned char*>(output)+8,name.c_str(),name.size()+1);
    return true;
}
struct ResourceData {
    unsigned char connected[33]{};
    int team[33]{};
    unsigned char alive[33]{};
    int health[33]{},maxHealth[33]{};
    unsigned char incap[33]{};
    int grenade[33]{},medical[33]{},pills[33]{};
};
struct FakeSurvivorClient {
    static constexpr std::size_t setOffset=0x7c46f0;
    std::vector<unsigned char> bytes=std::vector<unsigned char>(0x7c5000);
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS32 nt{};
    FakeSurvivorClient() {
        dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=0x80;
        nt.Signature=IMAGE_NT_SIGNATURE;nt.FileHeader.Machine=IMAGE_FILE_MACHINE_I386;
        nt.FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER32);
        nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR32_MAGIC;
        nt.OptionalHeader.SizeOfImage=static_cast<DWORD>(bytes.size());
        Headers();Set(2);
    }
    void Headers() {
        std::memcpy(bytes.data(),&dos,sizeof(dos));
        std::memcpy(bytes.data()+0x80,&nt,sizeof(nt));
    }
    void Set(int value) {std::memcpy(bytes.data()+setOffset,&value,sizeof(value));}
    HMODULE Module() {return reinterpret_cast<HMODULE>(bytes.data());}
};
void CheckSurvivorCharacterContext() {
    using namespace neh;
    constexpr int legacy[]={4,5,7,6};
    for(int raw=0;raw<8;++raw) {
        Check(NormalizeSurvivorCharacter(raw,1)==(raw<4?legacy[raw]:raw),
              "L4D1 legacy and explicit character IDs normalize to Bill, Zoey, Louis and Francis correctly");
        Check(NormalizeSurvivorCharacter(raw,2)==raw,
              "the L4D2 survivor set preserves all eight explicit character IDs");
        Check(NormalizeSurvivorCharacter(raw,0)==(raw<4?-1:raw)&&
              NormalizeSurvivorCharacter(raw,3)==(raw<4?-1:raw),
              "unknown survivor context rejects ambiguous legacy slots but preserves explicit L4D1 identities");
    }
    for(int raw:{-1,8,(std::numeric_limits<int>::min)(),(std::numeric_limits<int>::max)()})
        Check(NormalizeSurvivorCharacter(raw,1)==-1&&NormalizeSurvivorCharacter(raw,2)==-1&&
              NormalizeSurvivorCharacter(raw,0)==-1,"out-of-range character IDs never index a portrait or policy array");
    const auto priorClient=g.client;FakeSurvivorClient client;g.client=client.Module();
    for(int set:{1,2,0,1,3,-1,(std::numeric_limits<int>::max)(),2}) {
        client.Set(set);
        Check(ReadSurvivorSet()==(set==1||set==2?set:0),
              "survivor-set reads use the current value and never retain a prior valid context");
    }
    auto reject=[&](const char* label) {
        client.Headers();Check(ReadSurvivorSet()==0,label);
        client=FakeSurvivorClient();g.client=client.Module();
    };
    client.dos.e_magic=0;reject("a bad DOS signature cannot supply survivor context");
    for(LONG offset:{-1L,0L,1024L*1024+1}) {
        client.dos.e_lfanew=offset;reject("negative, overlapping and oversized PE header offsets fail closed");
    }
    client.nt.Signature=0;reject("a bad NT signature cannot supply survivor context");
    client.nt.FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;reject("a non-x86 image cannot supply the pinned survivor-set RVA");
    client.nt.FileHeader.SizeOfOptionalHeader=0;reject("a truncated optional header cannot supply survivor context");
    client.nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;reject("a PE32+ image is not interpreted using PE32 offsets");
    client.nt.OptionalHeader.SizeOfImage=static_cast<DWORD>(FakeSurvivorClient::setOffset);
    reject("the entire survivor-set integer must lie inside the declared image");
    client.nt.OptionalHeader.SizeOfImage=1024u*1024u*1024u+1;
    reject("an implausibly large declared image fails the survivor-set bound");
    g.client=nullptr;Check(ReadSurvivorSet()==0,"a missing client module yields unknown survivor context");
    g.client=reinterpret_cast<HMODULE>(1);Check(ReadSurvivorSet()==0,"an unreadable module header fails without dereferencing invalid memory");
    auto* protectedClient=static_cast<unsigned char*>(VirtualAlloc(nullptr,client.bytes.size(),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(protectedClient!=nullptr,"survivor context unreadability fixture owns a complete image allocation");
    std::memcpy(protectedClient,client.bytes.data(),client.bytes.size());g.client=reinterpret_cast<HMODULE>(protectedClient);
    DWORD previous=0,ignored=0;
    Check(VirtualProtect(protectedClient+FakeSurvivorClient::setOffset,sizeof(int),PAGE_NOACCESS,&previous)!=FALSE,
          "survivor-set value can become unreadable while valid PE headers remain readable");
    Check(ReadSurvivorSet()==0,"an unreadable survivor-set integer yields unknown instead of retaining the last value");
    Check(VirtualProtect(protectedClient+FakeSurvivorClient::setOffset,sizeof(int),previous,&ignored)!=FALSE&&ReadSurvivorSet()==2,
          "survivor context recovers from a readable current value after access is restored");
    Check(VirtualProtect(protectedClient,sizeof(IMAGE_DOS_HEADER),PAGE_NOACCESS,&previous)!=FALSE,
          "the current module's DOS header can become unreadable independently of its context field");
    Check(ReadSurvivorSet()==0,"an unreadable current PE header cannot reuse earlier context validation");
    Check(VirtualProtect(protectedClient,sizeof(IMAGE_DOS_HEADER),previous,&ignored)!=FALSE&&
          VirtualFree(protectedClient,0,MEM_RELEASE)!=FALSE,"survivor-set memory fixture restores and releases its allocation");
    g.client=priorClient;
}
void CheckResourceAdapter() {
    using namespace neh;
    FakeVgui vgui;g.vgui=&vgui;
    RecvProp props[9]{};
    const char* names[]={"m_bConnected","m_iTeam","m_bAlive","m_iHealth","m_maxHealth","m_isIncapacitated",
                         "m_grenade","m_firstAidSlot","m_pillsSlot"};
    const int offsets[]={offsetof(ResourceData,connected),offsetof(ResourceData,team),offsetof(ResourceData,alive),
                         offsetof(ResourceData,health),offsetof(ResourceData,maxHealth),offsetof(ResourceData,incap),
                         offsetof(ResourceData,grenade),offsetof(ResourceData,medical),offsetof(ResourceData,pills)};
    const int strides[]={1,4,1,4,4,1,4,4,4};
    for (int i=0;i<9;++i) {
        props[i].name=names[i]; props[i].offset=offsets[i];
        props[i].stride=strides[i]; props[i].elements=33;
    }
    RecvTable table{props,9,nullptr,"DT_TerrorPlayerResource"};
    ClientClass klass{nullptr,nullptr,"CTerrorPlayerResource",&table,nullptr,1};
    ResourceData data;
    data.connected[12]=1; data.team[12]=2; data.alive[12]=1;
    data.health[12]=73; data.maxHealth[12]=100;
    void* networkTable[12]{};
    networkTable[1]=reinterpret_cast<void*>(&FakeClass);
    networkTable[2]=reinterpret_cast<void*>(&WrongSlot);
    networkTable[10]=reinterpret_cast<void*>(&FakeData);
    networkTable[11]=reinterpret_cast<void*>(&WrongSlot);
    FakeNetworkable network{networkTable,&klass,&data};
    void* entityTable[7]{};
    entityTable[0]=reinterpret_cast<void*>(&FakeEntity);
    entityTable[6]=reinterpret_cast<void*>(&FakeHighest);
    FakeEntityList entities{entityTable,&network,40};
    g.entities=&entities; g.resourceIndex=-1; g.nextResourceScan=0; g.resourceTable=nullptr;
    void* base=nullptr;
    ClientClass found{};
    Check(EntityInfo(40,base,found),"current L4D2 networkable class/data vtable slots");
    Check(unexpectedCalls==0,"obsolete networkable slots never invoked");
    Check(base==&data&&found.table==&table,"entity info returns datatable base and class");
    Check(Resource(base)&&g.resourceIndex==40,"resource discovered beyond player entity slots");
    int health=0;
    Check(Array(base,g.resourceFields.health,12,health)&&health==73,"discovered resource reads twelfth survivor health");
    Check(Resource(base)&&base==&data,"cached resource remains readable");
    void* engineTable[27]{};
    engineTable[8]=reinterpret_cast<void*>(&FakePlayerInfo);
    engineTable[11]=reinterpret_cast<void*>(&FakeConsole);
    engineTable[12]=reinterpret_cast<void*>(&FakeLocal);
    engineTable[21]=reinterpret_cast<void*>(&FakeMaxPlayers);
    engineTable[26]=reinterpret_cast<void*>(&FakeInGame);
    FakeEngine engine{engineTable};
    g.engine=&engine; g.ready=true; g.selfPanel=0;
    g.nextSelfScan=GetTickCount()+60000;
    for (int i=1;i<=12;++i) {
        data.connected[i]=data.alive[i]=1; data.team[i]=2;
        data.health[i]=73; data.maxHealth[i]=100;
    }
    Runtime runtime;
    std::string error;
    data.grenade[12]=13;data.medical[12]=12;data.pills[12]=15;
    const auto snapshot=runtime.ReadSnapshot(error);
    Check(error.empty()&&snapshot.inGame&&snapshot.players.size()==12,"full snapshot reads twelve survivors through actual ABI fixture");
    Check(snapshot.localIndex==1&&snapshot.viewIndex==1,"snapshot retains local player identity");
    Check(snapshot.players.back().index==12&&snapshot.players.back().health==73&&snapshot.players.back().name=="Player 12",
          "twelfth survivor identity and health survive adapter path");
    Check(snapshot.players.back().throwable==13&&snapshot.players.back().medical==12&&snapshot.players.back().pills==15,
          "actual resource item arrays survive the complete snapshot path instead of being injected only into renderer mocks");
    for(const auto& items:std::array<std::array<int,2>,2>{{{14,23},{25,15}}}) {
        data.grenade[12]=items[0];data.pills[12]=items[1];
        const auto equipped=runtime.ReadSnapshot(error);
        Check(equipped.players.back().throwable==items[0]&&equipped.players.back().medical==12&&
              equipped.players.back().pills==items[1],
              "snapshot follows live throwable and pills/adrenaline item IDs on subsequent reads");
    }
    data.grenade[12]=data.medical[12]=data.pills[12]=0;
    const auto dropped=runtime.ReadSnapshot(error);
    Check(dropped.players.back().throwable==0&&dropped.players.back().medical==0&&dropped.players.back().pills==0,
          "dropping or consuming items clears all snapshot slots without retaining previous equipment");
    data.alive[12]=0;data.health[12]=0;
    const auto deadSurvivor=runtime.ReadSnapshot(error);
    Check(error.empty()&&deadSurvivor.inGame&&deadSurvivor.players.size()==12&&
          deadSurvivor.players.back().index==12&&!deadSurvivor.players.back().alive&&deadSurvivor.players.back().health==0,
          "survivor death keeps the connected player in the HUD roster with dead state");
    data.alive[1]=0;data.health[1]=0;
    const auto deadLocal=runtime.ReadSnapshot(error);
    Check(error.empty()&&deadLocal.inGame&&deadLocal.players.size()==12&&!deadLocal.players.front().alive&&g.ready,
          "local-player death does not disable snapshot updates or filter the survivor roster");

    data.maxHealth[11]=0;
    data.health[12]=-1;data.maxHealth[12]=0;data.incap[12]=255;
    const auto transition=runtime.ReadSnapshot(error);
    Check(transition.inGame&&transition.rosterIncomplete&&transition.players.size()==11&&
          transition.players[9].index==10&&transition.players.back().index==12,
          "an initializing survivor does not discard the rest of the roster during a death");
    Check(!transition.players.back().alive&&!transition.players.back().incapacitated&&
          transition.players.back().health==0&&transition.players.back().maxHealth==100,
          "connected dead survivor with invalid HP and stale incap state keeps a normalized dead card");
    Check(error.find("slot 11 m_maxHealth=0 (skipped)")!=std::string::npos&&
          error.find("slot 12 m_iHealth=-1 (normalized dead health)")!=std::string::npos,
          "snapshot warnings distinguish a skipped slot from normalized dead health");

    data.maxHealth[11]=150;data.health[12]=0;data.incap[12]=0;
    const auto repaired=runtime.ReadSnapshot(error);
    Check(repaired.inGame&&!repaired.rosterIncomplete&&repaired.players.size()==12&&
          repaired.players[10].index==11&&repaired.players[10].maxHealth==150,
          "the previously skipped slot recovers on its next coherent snapshot");
    g.resourceFields.maxHealth.elements=12;
    const auto deadWithoutMaxHealth=runtime.ReadSnapshot(error);
    Check(!deadWithoutMaxHealth.rosterIncomplete&&deadWithoutMaxHealth.players.size()==12&&
          deadWithoutMaxHealth.players.back().maxHealth==100&&
          error.find("slot 12 m_maxHealth=unreadable (normalized dead health)")!=std::string::npos,
          "an unavailable HP field does not remove a coherent dead slot");
    data.connected[12]=0;
    const auto disconnected=runtime.ReadSnapshot(error);
    Check(!disconnected.rosterIncomplete&&disconnected.players.size()==11&&error.empty(),
          "a disconnected slot never retains its old dead card");
    data.connected[12]=data.alive[12]=1;
    const auto reused=runtime.ReadSnapshot(error);
    Check(reused.inGame&&reused.rosterIncomplete&&reused.players.size()==11&&
          error.find("slot 12 m_maxHealth=unreadable (skipped)")!=std::string::npos,
          "an unreadable new occupant does not inherit the previous dead card");
    g.resourceFields.maxHealth.elements=33;
    data.maxHealth[12]=110;data.health[12]=90;
    const auto joined=runtime.ReadSnapshot(error);
    Check(error.empty()&&!joined.rosterIncomplete&&joined.players.size()==12&&
          joined.players.back().alive&&joined.players.back().health==90&&joined.players.back().maxHealth==110,
          "the new occupant draws only after its current resource values are readable");

    data.alive[1]=data.alive[12]=1;data.health[1]=data.health[12]=73;
    data.team[12]=3;
    Check(runtime.ReadSnapshot(error).players.size()==11,"snapshot excludes infected team");
    data.connected[11]=0;
    Check(runtime.ReadSnapshot(error).players.size()==10,"snapshot excludes disconnected players");
    engine.inGame=false;
    Check(!runtime.ReadSnapshot(error).inGame,"map exit clears cached live snapshot");
    engine.inGame=true;
    network.data=nullptr;
    Check(!EntityInfo(40,base,found),"missing networked data base rejected");
    network.data=&data;
    props[4].name="missing_max_health";
    g.resourceIndex=-1; g.nextResourceScan=0;
    Check(!Resource(base),"resource missing a required field rejected");
    Check(g.resourceError.find("m_maxHealth")!=std::string::npos,"missing field diagnostic names the failed property");
    g.resourceIndex=-1; g.nextResourceScan=0;
    klass.name="CTerrorPlayer";
    Check(!Resource(base),"unrelated entity is not accepted as a player resource");
    Check(g.resourceError.find("entity not found")!=std::string::npos,"missing resource diagnostic differs from missing field");
    g.entities=nullptr; g.engine=nullptr; g.vgui=nullptr;g.ready=false;
    g.resourceIndex=-1; g.resourceTable=nullptr; g.nextResourceScan=0; g.nextSelfScan=0;
}
struct FakeSurvivorHealth {
    void** vtable;
    int team=2,character=0,health=1;
    float buffer=79.75f;
    unsigned char hanging=0,thirdStrike=0;
    int permanent=1,total=80,permanentCalls=0,totalCalls=0;
    bool throwTotal=false;
};
int __fastcall FakePermanentHealth(FakeSurvivorHealth* self,void*) {
    ++self->permanentCalls;return self->permanent;
}
int __fastcall FakeTotalHealth(FakeSurvivorHealth* self,void*) {
    ++self->totalCalls;
    if(self->throwTotal)RaiseException(0xE0000003,0,0,nullptr);
    return self->total;
}
void CheckSurvivorHealthAdapter() {
    using namespace neh;
    void* healthTable[118]{};
    healthTable[116]=reinterpret_cast<void*>(&FakePermanentHealth);
    healthTable[117]=reinterpret_cast<void*>(&FakeTotalHealth);
    FakeSurvivorHealth survivor{healthTable};
    RecvProp playerProps[6]{};
    const char* playerNames[]={"m_iHealth","m_survivorCharacter","m_iTeamNum","m_healthBuffer",
                               "m_isHangingFromLedge","m_bIsOnThirdStrike"};
    const int playerOffsets[]={offsetof(FakeSurvivorHealth,health),offsetof(FakeSurvivorHealth,character),
        offsetof(FakeSurvivorHealth,team),offsetof(FakeSurvivorHealth,buffer),
        offsetof(FakeSurvivorHealth,hanging),offsetof(FakeSurvivorHealth,thirdStrike)};
    for(int i=0;i<6;++i) {playerProps[i].name=playerNames[i];playerProps[i].offset=playerOffsets[i];}
    RecvTable playerTable{playerProps,6,nullptr,"DT_TerrorPlayer"};
    ClientClass playerClass{nullptr,nullptr,"CTerrorPlayer",&playerTable,nullptr,1};
    const PlayerFields fullFields(&playerTable);
    int permanent=-7,total=-8;
    Check(ReadSurvivorHealth(&survivor,playerClass,fullFields,permanent,total)&&permanent==1&&total==80&&
          survivor.permanentCalls==1&&survivor.totalCalls==1,
          "verified primary slots 116 and 117 return permanent and already-summed integer health");
    for(int missing=0;missing<4;++missing) {
        auto fields=fullFields;
        Field* required[]={&fields.health,&fields.character,&fields.team,&fields.tempBuffer};
        *required[missing]=Field{};survivor.permanentCalls=survivor.totalCalls=0;permanent=-7;total=-8;
        Check(!ReadSurvivorHealth(&survivor,playerClass,fields,permanent,total)&&permanent==-7&&total==-8&&
              survivor.permanentCalls==0&&survivor.totalCalls==0,
              "missing survivor identity or health netprop rejects getters before either virtual call");
    }
    playerClass.name="CTerrorPlayerResource";survivor.permanentCalls=survivor.totalCalls=0;
    Check(!ReadSurvivorHealth(&survivor,playerClass,fullFields,permanent,total)&&
          survivor.permanentCalls==0&&survivor.totalCalls==0,
          "unrelated entity classes never receive survivor health virtual calls");
    playerClass.name="CTerrorPlayer";

    RecvProp resourceProps[6]{};
    const char* resourceNames[]={"m_bConnected","m_iTeam","m_bAlive","m_iHealth","m_maxHealth","m_isIncapacitated"};
    const int resourceOffsets[]={offsetof(ResourceData,connected),offsetof(ResourceData,team),offsetof(ResourceData,alive),
        offsetof(ResourceData,health),offsetof(ResourceData,maxHealth),offsetof(ResourceData,incap)};
    const int strides[]={1,4,1,4,4,1};
    for(int i=0;i<6;++i) {
        resourceProps[i].name=resourceNames[i];resourceProps[i].offset=resourceOffsets[i];
        resourceProps[i].stride=strides[i];resourceProps[i].elements=33;
    }
    RecvTable resourceTable{resourceProps,6,nullptr,"DT_TerrorPlayerResource"};
    ClientClass resourceClass{nullptr,nullptr,"CTerrorPlayerResource",&resourceTable,nullptr,2};
    ResourceData resource;
    for(int index:{1,6}) {
        resource.connected[index]=resource.alive[index]=1;resource.team[index]=2;
        resource.health[index]=37;resource.maxHealth[index]=100;
    }
    void* networkTable[12]{};
    networkTable[1]=reinterpret_cast<void*>(&FakeClass);networkTable[10]=reinterpret_cast<void*>(&FakeData);
    FakeNetworkable resourceNetwork{networkTable,&resourceClass,&resource};
    FakeNetworkable playerNetwork{networkTable,&playerClass,&survivor};
    void* entityTable[7]{};
    entityTable[0]=reinterpret_cast<void*>(&FakeEntity);entityTable[6]=reinterpret_cast<void*>(&FakeHighest);
    FakeEntityList entities{entityTable,&resourceNetwork,40};entities.players[6]=&playerNetwork;
    void* engineTable[27]{};
    engineTable[5]=reinterpret_cast<void*>(&FakeScreen);engineTable[8]=reinterpret_cast<void*>(&FakePlayerInfo);
    engineTable[11]=reinterpret_cast<void*>(&FakeConsole);engineTable[12]=reinterpret_cast<void*>(&FakeLocal);
    engineTable[21]=reinterpret_cast<void*>(&FakeMaxPlayers);engineTable[26]=reinterpret_cast<void*>(&FakeInGame);
    FakeEngine engine{engineTable};FakeVgui vgui;FakeSurvivorClient client;
    g=State();g.ready=true;g.engine=&engine;g.entities=&entities;g.vgui=&vgui;g.client=client.Module();
    g.nextSelfScan=GetTickCount()+60000;g_runtimeCounters={};
    Runtime runtime;std::string error;int lastSurvivorSet=-1;
    auto read=[&]() {
        const auto snapshot=runtime.ReadSnapshot(error);
        lastSurvivorSet=snapshot.survivorSet;
        const auto found=std::find_if(snapshot.players.begin(),snapshot.players.end(),
                                      [](const Player& p){return p.index==6;});
        if(!error.empty()||!snapshot.inGame||snapshot.players.size()!=2||found==snapshot.players.end()) {
            std::cerr<<"FAIL: survivor health snapshot fixture lost its live roster\n";std::exit(1);
        }
        return *found;
    };
    auto player=read();
    Check(player.health==80&&player.temporaryHealth==79&&player.alive&&
          g_runtimeCounters.nativeHealthReads==1&&g_runtimeCounters.resourceHealthReads==1,
          "snapshot uses native total once and stores its included temporary portion instead of adding buffer again");
    Check(lastSurvivorSet==2&&player.rawCharacter==0&&player.character==0,
          "the complete L4D2 snapshot fixture supplies real current context rather than assuming raw slots are canonical");
    for(const auto& context:std::array<std::array<int,3>,7>{{{1,0,4},{1,2,7},{1,3,6},{2,0,0},{0,0,-1},{0,4,4},{2,8,-1}}}) {
        client.Set(context[0]);survivor.character=context[1];player=read();
        Check(lastSurvivorSet==context[0]&&player.rawCharacter==context[1]&&player.character==context[2]&&
              player.index==6&&player.name=="Player 6"&&player.alive&&!player.incapacitated&&!player.hanging&&
              player.health==80&&player.temporaryHealth==79&&player.maxHealth==100,
              "live context changes normalize only character identity while preserving raw diagnostics, player ID, name and health state");
    }
    client.Set(1);survivor.character=0;player=read();
    FakeSurvivorHealth localSurvivor{healthTable};localSurvivor.character=4;
    FakeNetworkable localNetwork{networkTable,&playerClass,&localSurvivor};entities.players[1]=&localNetwork;
    for(int set:{1,2}) {
        client.Set(set);const auto shared=runtime.ReadSnapshot(error);
        Check(error.empty()&&shared.survivorSet==set&&shared.players.size()==2&&
              shared.players[0].index==1&&shared.players[0].rawCharacter==4&&shared.players[0].character==4&&
              shared.players[1].index==6&&shared.players[1].rawCharacter==0&&shared.players[1].character==(set==1?4:0)&&
              shared.localIndex==1&&shared.viewIndex==1,
              "every player in a snapshot shares current map context while preserving local and teammate raw identity");
    }
    entities.players.erase(1);client.Set(1);player=read();
    client.dos.e_magic=0;client.Headers();player=read();
    Check(lastSurvivorSet==0&&player.rawCharacter==0&&player.character==-1&&player.health==80&&g.ready,
          "invalid live client metadata yields an unknown character without losing the roster, health or running adapter");
    client.dos.e_magic=IMAGE_DOS_SIGNATURE;client.Headers();player=read();
    engine.inGame=false;const auto exited=runtime.ReadSnapshot(error);
    Check(!exited.inGame&&exited.survivorSet==0&&exited.players.empty(),
          "map exit resets snapshot context rather than publishing the previous L4D1 survivor set");
    engine.inGame=true;client.Set(2);player=read();
    Check(lastSurvivorSet==2&&player.rawCharacter==0&&player.character==0,
          "a new live map reads L4D2 context without reusing the preceding L4D1 normalization");
    survivor.total=79;player=read();
    Check(player.health==79&&player.temporaryHealth==78,
          "each snapshot reads the engine's newly decayed and integer-rounded total without recomputing raw buffer");
    playerClass.name="SurvivorBot";survivor.total=1;player=read();
    Check(player.health==1&&player.temporaryHealth==0,
          "supported survivor bots use the same getters and an exhausted buffer has no temporary portion");
    playerClass.name="CTerrorPlayer";survivor.permanent=245;survivor.total=300;resource.incap[6]=1;
    resource.maxHealth[6]=300;player=read();
    Check(player.incapacitated&&player.health==245&&player.temporaryHealth==0,
          "downed survivors retain their current permanent state health without temporary-health duplication");
    resource.incap[6]=0;survivor.hanging=1;survivor.permanent=96;survivor.total=130;player=read();
    Check(player.hanging&&player.health==96&&player.temporaryHealth==0,
          "hanging survivors retain current state health and exclude the temporary segment");
    survivor.hanging=0;survivor.permanent=1;survivor.total=80;survivor.throwTotal=true;player=read();
    Check(player.health==37&&player.temporaryHealth==0&&g.ready,
          "guarded native getter failure falls back to resource health without disabling HUD updates");
    survivor.throwTotal=false;
    const std::array<std::array<int,2>,4> invalid={{{-1,80},{10,9},{1,100001},
        {std::numeric_limits<int>::min(),std::numeric_limits<int>::min()}}};
    for(const auto& values:invalid) {
        survivor.permanent=values[0];survivor.total=values[1];survivor.buffer=std::numeric_limits<float>::quiet_NaN();
        player=read();
        Check(player.health==37&&player.temporaryHealth==0,
              "negative, reversed, oversized and nonfinite-conversion integer results use safe resource fallback");
    }
    survivor.permanent=1;survivor.total=80;survivor.buffer=79.75f;
    g.playerFields[&playerTable].tempBuffer=Field{};survivor.permanentCalls=survivor.totalCalls=0;player=read();
    Check(player.health==37&&player.temporaryHealth==0&&survivor.permanentCalls==0&&survivor.totalCalls==0,
          "snapshot with missing buffer metadata preserves resource fallback without probing getters");
    g.playerFields[&playerTable]=fullFields;playerClass.name="UnrelatedPlayer";
    survivor.permanentCalls=survivor.totalCalls=0;player=read();
    Check(player.health==37&&player.temporaryHealth==0&&survivor.permanentCalls==0&&survivor.totalCalls==0,
          "snapshot keeps unsupported classes on resource health without speculative virtual calls");
    playerClass.name="CTerrorPlayer";resource.alive[6]=0;resource.health[6]=0;
    survivor.permanentCalls=survivor.totalCalls=0;player=read();
    Check(!player.alive&&player.health==0&&player.temporaryHealth==0&&
          survivor.permanentCalls==0&&survivor.totalCalls==0&&g.ready,
          "dead survivors stay in the roster with dead resource state and no health getter calls");
    g=State();g_runtimeCounters={};
}
struct FakePanelNode {
    const char* name="";
    const char* klass="";
    std::uint32_t parent=0;
    neh::Rect bounds,clip;
    bool visible=false;
    std::vector<std::uint32_t> children;
    std::array<unsigned char,0x268> clientObject{};
    int z=0;std::uint32_t scheme=1;
    bool hasLocalPos=false;
    int localX=0,localY=0;
    std::array<int,4> insets{};
};
struct FakePanels {
    void** vtable;
    std::vector<FakePanelNode> nodes=std::vector<FakePanelNode>(32);
    FakeVgui* vgui=nullptr;
    int visibleCalls=0,childCountCalls=0,childCalls=0,classCalls=0,nameCalls=0;
    int retiredCalls=0,unresolvedCalls=0;
    std::uint32_t watched=0;
    std::unordered_map<std::uint32_t,int> callsByPanel;
};
FakePanelNode& Node(FakePanels* p,std::uint32_t id) {
    ++p->callsByPanel[id];
    if(p->vgui&&!p->vgui->Live(id))++p->retiredCalls;
    if(id==p->watched&&p->vgui&&!p->vgui->Resolved(id))++p->unresolvedCalls;
    return p->nodes[id<p->nodes.size()?id:0];
}
void __fastcall FakePanelSize(FakePanels* p,void*,std::uint32_t id,int* w,int* h) {
    *w=Node(p,id).bounds.w; *h=Node(p,id).bounds.h;
}
void __fastcall FakePanelPos(FakePanels* p,void*,std::uint32_t id,int* x,int* y) {
    *x=Node(p,id).bounds.x; *y=Node(p,id).bounds.y;
}
void __fastcall FakePanelLocalPos(FakePanels* p,void*,std::uint32_t id,int* x,int* y) {
    const auto& node=Node(p,id);
    const auto& parent=p->nodes[node.parent];
    *x=node.hasLocalPos?node.localX:node.bounds.x-parent.bounds.x-parent.insets[0];
    *y=node.hasLocalPos?node.localY:node.bounds.y-parent.bounds.y-parent.insets[1];
}
void __fastcall FakePanelInset(FakePanels* p,void*,std::uint32_t id,int* left,int* top,int* right,int* bottom) {
    const auto& insets=Node(p,id).insets;
    *left=insets[0];*top=insets[1];*right=insets[2];*bottom=insets[3];
}
void __fastcall FakePanelClip(FakePanels* p,void*,std::uint32_t id,int* left,int* top,int* right,int* bottom) {
    const auto& clip=Node(p,id).clip;
    *left=clip.x; *top=clip.y; *right=clip.x+clip.w; *bottom=clip.y+clip.h;
}
bool __fastcall FakePanelVisible(FakePanels* p,void*,std::uint32_t id) { ++p->visibleCalls; return Node(p,id).visible; }
int __fastcall FakeChildCount(FakePanels* p,void*,std::uint32_t id) {
    ++p->childCountCalls;
    return static_cast<int>(Node(p,id).children.size());
}
std::uint32_t __fastcall FakeChild(FakePanels* p,void*,std::uint32_t id,int index) {
    ++p->childCalls;
    const auto& children=Node(p,id).children;
    return index>=0&&static_cast<std::size_t>(index)<children.size()?children[index]:0;
}
std::uint32_t __fastcall FakeParent(FakePanels* p,void*,std::uint32_t id) { return Node(p,id).parent; }
const char* __fastcall FakePanelName(FakePanels* p,void*,std::uint32_t id) { ++p->nameCalls; return Node(p,id).name; }
const char* __fastcall FakePanelClass(FakePanels* p,void*,std::uint32_t id) { ++p->classCalls; return Node(p,id).klass; }
void* __fastcall FakeClientPanel(FakePanels* p,void*,std::uint32_t id,const char* module) {
    return module&&std::strcmp(module,"ClientDLL")==0 ? Node(p,id).clientObject.data() : nullptr;
}
void* __fastcall MissingClientPanel(FakePanels*,void*,std::uint32_t,const char*) {return nullptr;}
void* __fastcall InvalidClientPanel(FakePanels*,void*,std::uint32_t,const char*) {return reinterpret_cast<void*>(1);}
void* __fastcall ReplacedClientPanel(FakePanels* p,void*,std::uint32_t id,const char* module) {
    void* object=FakeClientPanel(p,nullptr,id,module);p->vgui->Retire(id);p->vgui->Add(id);return object;
}
int __fastcall FakePanelZ(FakePanels* p,void*,std::uint32_t id) {return Node(p,id).z;}
std::uint32_t __fastcall FakePanelScheme(FakePanels* p,void*,std::uint32_t id) {return Node(p,id).scheme;}
void* __fastcall FakeExistingEntity(void* self,void*,int index) { return index>=1&&index<=32?self:nullptr; }
int __fastcall MemoryValueA(void*,void*) { return 11; }
int __fastcall MemoryValueB(void*,void*) { return 22; }
int __fastcall MemoryFault(void*,void*) { RaiseException(0xE0000002,0,0,nullptr);return 0; }
void CheckHudBoundsAdapter() {
    using namespace neh;
    void* engineTable[27]{};
    engineTable[5]=reinterpret_cast<void*>(&FakeScreen);
    FakeEngine engine{engineTable};
    void* panelTable[56]{};
    panelTable[5]=reinterpret_cast<void*>(&FakePanelSize);
    panelTable[10]=reinterpret_cast<void*>(&FakePanelPos);
    panelTable[11]=reinterpret_cast<void*>(&FakePanelClip);
    panelTable[15]=reinterpret_cast<void*>(&FakePanelVisible);
    panelTable[17]=reinterpret_cast<void*>(&FakeChildCount);
    panelTable[18]=reinterpret_cast<void*>(&FakeChild);
    panelTable[20]=reinterpret_cast<void*>(&FakeParent);
    panelTable[26]=reinterpret_cast<void*>(&FakePanelVisible);
    panelTable[36]=reinterpret_cast<void*>(&FakePanelName);
    panelTable[37]=reinterpret_cast<void*>(&FakePanelClass);
    FakePanels panels{}; panels.vtable=panelTable;
    FakeVgui vgui;panels.vgui=&vgui;
    auto add=[&](std::uint32_t id,std::uint32_t parent,const char* name,const char* klass,Rect bounds) {
        vgui.Add(id);
        panels.nodes[id]={name,klass,parent,bounds,{0,0,2560,1440},true,{}};
        if (parent) panels.nodes[parent].children.push_back(id);
    };
    add(1,0,"Root","Panel",{0,0,2560,1440});
    add(2,1,"Teammate","TeamDisplayPlayer",{15,1221,450,450});
    panels.nodes[2].clip={15,1221,450,219};
    add(3,2,"BackgroundImage","ImagePanel",{9,1254,420,204});
    panels.nodes[3].clip=panels.nodes[2].clip;
    g.engine=&engine; g.panel=&panels;g.vgui=&vgui;
    Rect visual{};
    const auto visibilityBefore=panels.visibleCalls;
    Check(!SelfPanel(1,1,visual)&&panels.visibleCalls==visibilityBefore,
          "unrelated panel class is rejected before any visibility or ancestor work");
    Check(VisualCardBounds(2,{15,1221,450,219},visual),"named teammate background supplies visual anchor");
    Check(visual.x==15&&visual.y==1254&&visual.w==414&&visual.h==186,
          "visual anchor uses actual background clip instead of square container");
    Check(!Contains({0,0,10,10},{5,5,10,10}),"visual bound containment rejects oversize child");
    add(4,2,"BackgroundImage","ImagePanel",{20,1254,400,180});
    Check(!VisualCardBounds(2,{15,1221,450,219},visual),"multiple visible backgrounds require container fallback");
    panels.nodes[4].visible=false;
    Check(VisualCardBounds(2,{15,1221,450,219},visual),"hidden transition background does not create ambiguity");
    panels.nodes[3].parent=1;
    Check(!VisualCardBounds(2,{15,1221,450,219},visual),"background without matching teammate ancestor is rejected");
    panels.nodes[3].parent=2;
    panels.nodes[3].name="OtherImage";
    Check(!VisualCardBounds(2,{15,1221,450,219},visual),"unknown teammate art keeps conservative container fallback");
    panels.nodes[3].name="BackgroundImage";
    add(5,1,"CHudLocalPlayerDisplay","CHudLocalPlayerDisplay",{2080,72,2560,1440});
    add(6,5,"LocalPlayer","LocalPlayerPanel",{2080,1152,480,360});
    auto allPanels=[&](){std::vector<std::uint32_t> result;Traverse(1,result,128);return result;};
    std::uint32_t selected=0;
    Rect self{};
    Check(FindNamedSelfPanel(allPanels(),selected,self)&&selected==6,
          "unique named local HUD is found without a speculative player getter");
    Check(self.x==2080&&self.y==1152&&self.w==480&&self.h==288,
          "local HUD collision bounds are clipped to the current screen");
    add(7,1,"CHudLocalPlayerDisplay","CHudLocalPlayerDisplay",{2080,72,2560,1440});
    add(8,7,"LocalPlayer","LocalPlayerPanel",{2080,1152,480,360});
    Check(!FindNamedSelfPanel(allPanels(),selected,self),"multiple visible local HUD roots are ambiguous");
    panels.nodes[7].visible=false;
    Check(FindNamedSelfPanel(allPanels(),selected,self)&&selected==6,
          "hidden local HUD root is excluded from fallback selection");
    add(9,5,"LocalPlayer","LocalPlayerPanel",{2080,1152,480,360});
    Check(!FindNamedSelfPanel(allPanels(),selected,self),"multiple visible LocalPlayer children are ambiguous");
    panels.nodes[9].visible=false;
    add(10,2,"LocalPlayer","LocalPlayerPanel",{15,1221,450,450});
    Check(FindNamedSelfPanel(allPanels(),selected,self)&&selected==6,
          "teammate-owned LocalPlayer name cannot substitute for self HUD");
    panels.nodes[6].visible=false;
    Check(!FindNamedSelfPanel(allPanels(),selected,self),"missing visible local player does not adopt teammate decoration");
    panels.nodes[6].visible=true; panels.nodes[6].parent=6;
    Check(!NamedSelfPanelBounds(6,self),"cyclic local HUD ancestry is rejected");
    g.engine=nullptr; g.panel=nullptr;g.vgui=nullptr;g.selfPanel=0; g.selfPanelIsNamed=false;
}
void CheckPanelDiscoveryCache() {
    using namespace neh;
    void* engineTable[27]{};
    engineTable[5]=reinterpret_cast<void*>(&FakeScreen);
    engineTable[26]=reinterpret_cast<void*>(&FakeInGame);
    FakeEngine engine{engineTable};
    void* panelTable[56]{};
    panelTable[5]=reinterpret_cast<void*>(&FakePanelSize);
    panelTable[10]=reinterpret_cast<void*>(&FakePanelPos);
    panelTable[11]=reinterpret_cast<void*>(&FakePanelClip);
    panelTable[15]=reinterpret_cast<void*>(&FakePanelVisible);
    panelTable[17]=reinterpret_cast<void*>(&FakeChildCount);
    panelTable[18]=reinterpret_cast<void*>(&FakeChild);
    panelTable[20]=reinterpret_cast<void*>(&FakeParent);
    panelTable[26]=reinterpret_cast<void*>(&FakePanelVisible);
    panelTable[36]=reinterpret_cast<void*>(&FakePanelName);
    panelTable[37]=reinterpret_cast<void*>(&FakePanelClass);
    panelTable[55]=reinterpret_cast<void*>(&FakeClientPanel);
    FakePanels panels{};panels.vtable=panelTable;
    FakeVgui vgui;panels.vgui=&vgui;
    auto add=[&](std::uint32_t id,std::uint32_t parent,const char* name,const char* klass,Rect bounds) {
        vgui.Add(id);
        panels.nodes[id]={name,klass,parent,bounds,{0,0,2560,1440},true,{}};
        if(parent)panels.nodes[parent].children.push_back(id);
    };
    auto target=[&](std::uint32_t panel,int index) {
        std::memcpy(panels.nodes[panel].clientObject.data()+0x264,&index,sizeof(index));
    };
    add(1,0,"Root","Panel",{0,0,2560,1440});
    add(2,1,"CHudTeamDisplay","CHudTeamDisplay",{0,0,2560,1440});
    for(std::uint32_t n=0;n<4;++n) {
        const Rect bounds{20+static_cast<int>(n)*150,920,140,64};
        add(3+n,2,"Teammate","TeamDisplayPlayer",bounds);
        add(7+n,3+n,"BackgroundImage","ImagePanel",bounds);
        target(3+n,static_cast<int>(n)+2);
    }
    add(11,2,"Decoration","Panel",{0,0,2560,1440});
    void* entityTable[7]{};
    entityTable[3]=reinterpret_cast<void*>(&FakeExistingEntity);
    struct ExistingEntities {void** vtable;} entities{entityTable};
    std::vector<unsigned char> neko(0x207c10);
    std::array<unsigned char,0x34> allowHud{};
    void* allow=allowHud.data();int enabled=1;
    std::memcpy(neko.data()+0x207c0c,&allow,sizeof(allow));
    std::memcpy(allowHud.data()+0x30,&enabled,sizeof(enabled));
    g=State();g.ready=true;g.engine=&engine;g.panel=&panels;g.vgui=&vgui;
    g.entities=&entities;g.neko=reinterpret_cast<HMODULE>(neko.data());
    ResetPanelCaches();g_runtimeCounters={};

    const auto cold=NativePanelCandidates(2,2560,1440,1000);
    Check(cold.size()==4&&g_runtimeCounters.nativeDiscoveries==1&&g_runtimeCounters.traversedNodes>4,
          "native discovery finds all four actual controls once");
    g_runtimeCounters={};
    const auto warm=NativePanelCandidates(2,2560,1440,1010);
    Check(warm==cold&&g_runtimeCounters.nativeDiscoveries==0&&g_runtimeCounters.traversedNodes==0,
          "warm native lookup validates handles without traversing the tree again");

    add(26,3,"BackgroundImage","ImagePanel",{20,928,130,40});panels.nodes[26].visible=false;
    add(27,3,"Decoration","Panel",{20,920,140,64});
    Rect visual{};const Rect container=panels.nodes[3].bounds;
    Check(CachedVisualCardBounds(3,container,visual,1000)&&visual.w==140,
          "visual discovery includes the visible background and remembers hidden alternatives");
    g_runtimeCounters={};
    Check(CachedVisualCardBounds(3,container,visual,1010)&&g_runtimeCounters.visualDiscoveries==0&&
          g_runtimeCounters.traversedNodes==0,
          "warm visual lookup avoids descendant discovery");
    panels.nodes[7].bounds={22,922,136,60};
    Check(CachedVisualCardBounds(3,container,visual,1020)&&visual.x==22&&visual.h==60,
          "cached visual handles still read fresh bounds every frame");
    panels.nodes[7].visible=false;panels.nodes[26].visible=true;
    Check(CachedVisualCardBounds(3,container,visual,1030)&&visual.y==928&&visual.h==40&&
          g_runtimeCounters.visualDiscoveries==0,
          "normal to incap visibility changes switch cached background immediately");
    panels.nodes[7].visible=true;
    Check(!CachedVisualCardBounds(3,container,visual,1040),
          "two visible cached backgrounds retain conservative ambiguous fallback");
    panels.nodes[26].visible=false;panels.nodes[7].parent=1;
    Check(!CachedVisualCardBounds(3,container,visual,1050),
          "cached background moved outside its teammate is not reused");
    panels.nodes[7].parent=3;panels.nodes[7].name="OtherImage";
    Check(!CachedVisualCardBounds(3,container,visual,1600),
          "renamed background is excluded on rediscovery");
    panels.nodes[7].name="BackgroundImage";
    Check(CachedVisualCardBounds(3,container,visual,2200),
          "periodic visual discovery can recover a renamed or newly available background");
    add(28,27,"BackgroundImage","ImagePanel",{30,928,120,40});
    Check(CachedVisualCardBounds(3,container,visual,2210),
          "new nested background is discovered within the bounded refresh interval");
    Check(!CachedVisualCardBounds(3,container,visual,2800),
          "periodic visual refresh notices a second visible nested background");
    panels.nodes[28].visible=false;

    std::string error;Runtime runtime;
    ResetPanelCaches();
    const auto first=runtime.ReadNativeSlots(2,error);
    Check(error.empty()&&first.size()==4&&first[0].playerIndex==2,
          "cached native adapter reads actual target indices from current ClientPanel objects");
    enabled=0;std::memcpy(allowHud.data()+0x30,&enabled,sizeof(enabled));
    Check(runtime.TeamHudVisible(2)&&g.nativePanels.valid,
          "the Neko paint toggle does not hide the logical root or clear its discovery cache");
    Check(runtime.ReadNativeSlots(2,error).size()==4&&error.empty(),
          "native source targets stay readable while the original Neko teammate painting is disabled");
    enabled=1;std::memcpy(allowHud.data()+0x30,&enabled,sizeof(enabled));
    entityTable[3]=nullptr;
    target(3,12);
    const auto takeover=runtime.ReadNativeSlots(2,error);
    Check(error.empty()&&takeover.size()==4&&takeover[0].playerIndex==12&&takeover[0].bounds.w>0,
          "an updated native target keeps its reserved bounds while its entity is unavailable");
    entityTable[3]=reinterpret_cast<void*>(&FakeExistingEntity);
    panels.nodes[3].visible=false;
    const auto hidden=runtime.ReadNativeSlots(2,error);
    Check(hidden.size()==3,
          "a hidden native panel is not silently substituted from cached discovery");
    panels.nodes[3].visible=true;target(3,2);

    ResetPanelCaches();NativePanelCandidates(2,2560,1440,1000);g_runtimeCounters={};
    add(15,2,"Replacement","TeamDisplayPlayer",panels.nodes[3].bounds);target(15,15);
    panels.nodes[2].children.pop_back();
    panels.nodes[2].children[0]=15;
    const auto replaced=NativePanelCandidates(2,2560,1440,1010);
    Check(replaced.size()==4&&std::find(replaced.begin(),replaced.end(),15u)!=replaced.end()&&
          std::find(replaced.begin(),replaced.end(),3u)==replaced.end()&&g_runtimeCounters.nativeDiscoveries==1,
          "same-count direct child replacement invalidates cached native handles");
    panels.nodes[15].klass="Panel";
    Check(NativePanelCandidates(2,2560,1440,1020).size()==3,
          "reused handle with a different class is removed immediately");
    panels.nodes[15].klass="TeamDisplayPlayer";
    Check(NativePanelCandidates(2,2560,1440,1600).size()==4,
          "native periodic rediscovery restores a valid new card");
    panels.nodes[15].parent=1;
    Check(NativePanelCandidates(2,2560,1440,1610).size()==3,
          "cached native no longer owned by the team root is rejected");
    panels.nodes[15].parent=2;
    NativePanelCandidates(2,2560,1440,2200);g_runtimeCounters={};
    NativePanelCandidates(2,1920,1080,2210);
    Check(g_runtimeCounters.nativeDiscoveries==1&&g.nativePanels.width==1920,
          "screen size change forces fresh panel discovery");
    add(21,1,"NewTeamRoot","CHudTeamDisplay",{0,0,2560,1440});
    for(std::uint32_t n=0;n<4;++n)add(22+n,21,"NewCard","TeamDisplayPlayer",{20,920,140,64});
    const auto newRoot=NativePanelCandidates(21,2560,1440,2220);
    Check(newRoot.size()==4&&newRoot[0]==22&&g.nativePanels.root==21,
          "new map or replacement root cannot reuse old team controls");
    NativePanelCandidates(2,2560,1440,2800);
    add(12,11,"UnexpectedFifth","TeamDisplayPlayer",{620,920,140,64});target(12,12);
    Check(NativePanelCandidates(2,2560,1440,2810).size()==4,
          "nested structural additions have a bounded discovery interval");
    Check(NativePanelCandidates(2,2560,1440,3400).size()==5,
          "periodic native discovery notices a newly nested fifth control");
    const auto tooMany=runtime.ReadNativeSlots(2,error);
    Check(tooMany.empty()&&error.find("More than four")!=std::string::npos,
          "unsupported extra native controls still suppress drawing after rediscovery");
    panels.nodes[2].visible=false;
    Check(runtime.ReadNativeSlots(2,error).empty()&&!g.nativePanels.valid&&g.visualPanels.empty(),
          "hidden team root clears all discovery caches");
    panels.nodes[2].visible=true;
    NativePanelCandidates(2,2560,1440,4000);
    engine.inGame=false;
    Check(!runtime.ReadSnapshot(error).inGame&&!g.nativePanels.valid&&g.visualPanels.empty(),
          "map exit clears native and visual discovery handles");
    g=State();g_runtimeCounters={};
}
void CheckNativeHudPaintState() {
    using namespace neh;
    g=State();g_runtimeCounters={};
    void* engineTable[27]{};engineTable[5]=reinterpret_cast<void*>(&FakeScreen);
    FakeEngine engine{engineTable};
    void* panelTable[56]{};
    panelTable[5]=reinterpret_cast<void*>(&FakePanelSize);panelTable[10]=reinterpret_cast<void*>(&FakePanelPos);
    panelTable[11]=reinterpret_cast<void*>(&FakePanelClip);panelTable[15]=reinterpret_cast<void*>(&FakePanelVisible);
    panelTable[20]=reinterpret_cast<void*>(&FakeParent);panelTable[26]=reinterpret_cast<void*>(&FakePanelVisible);
    panelTable[36]=reinterpret_cast<void*>(&FakePanelName);panelTable[37]=reinterpret_cast<void*>(&FakePanelClass);
    panelTable[55]=reinterpret_cast<void*>(&FakeClientPanel);
    FakePanels panels{};panels.vtable=panelTable;
    FakeVgui vgui;vgui.Add(2);panels.vgui=&vgui;panels.watched=2;
    panels.nodes[2]={"CHudTeamDisplay","CHudTeamDisplay",0,{0,0,2560,1440},{0,0,2560,1440},true,{}};
    std::vector<unsigned char> neko(0x207c10);std::array<unsigned char,0x34> allowHud{};
    void* allow=allowHud.data();int enabled=1;
    std::memcpy(neko.data()+0x207c0c,&allow,sizeof(allow));
    std::memcpy(allowHud.data()+0x30,&enabled,sizeof(enabled));
    void* ownerTable[4]{};ownerTable[3]=neko.data()+0x67a70;
    void** tablePointer=ownerTable;std::memcpy(panels.nodes[2].clientObject.data(),&tablePointer,sizeof(tablePointer));
    g.ready=true;g.engine=&engine;g.panel=&panels;g.vgui=&vgui;g.neko=reinterpret_cast<HMODULE>(neko.data());
    Runtime runtime;NativeHudPaintState state;
    Check(runtime.ReadNativeHudPaintState(2,state)&&state.enabled&&state.rootHandle==0,
          "the exact Neko class paint wrapper authorizes a fresh root, including valid HPanel handle zero");
    const auto initialHandle=state.rootHandle;
    enabled=0;std::memcpy(allowHud.data()+0x30,&enabled,sizeof(enabled));
    Check(runtime.ReadNativeHudPaintState(2,state)&&!state.enabled&&state.rootHandle==initialHandle&&runtime.TeamHudVisible(2),
          "Neko menu OFF is paint-disabled but remains a logically visible root for generated cards");
    enabled=1;std::memcpy(allowHud.data()+0x30,&enabled,sizeof(enabled));
    Check(runtime.ReadNativeHudPaintState(2,state)&&state.enabled,
          "the current Neko menu value is reread instead of cached across frames");
    panels.nodes[2].klass="Panel";
    Check(runtime.IsTeamRoot(2)&&!runtime.ReadNativeHudPaintState(2,state)&&state.enabled&&state.rootHandle==0xffffffffu,
          "a matching name alone cannot authorize native suppression and failure clears an earlier state token");
    panels.nodes[2].klass="CHudTeamDisplay";
    panelTable[55]=reinterpret_cast<void*>(&MissingClientPanel);
    Check(!runtime.ReadNativeHudPaintState(2,state),"a root without the ClientDLL owner cannot authorize paint replacement");
    panelTable[55]=reinterpret_cast<void*>(&InvalidClientPanel);
    Check(!runtime.ReadNativeHudPaintState(2,state),"an unreadable ClientDLL owner fails guarded paint-state inspection");
    panelTable[55]=reinterpret_cast<void*>(&FakeClientPanel);
    ownerTable[3]=neko.data()+0x67a71;
    Check(!runtime.ReadNativeHudPaintState(2,state),"another wrapper address is rejected even inside the supported Neko module");
    ownerTable[3]=reinterpret_cast<void*>(&MemoryFault);
    Check(!runtime.ReadNativeHudPaintState(2,state),"another addon paint method is only compared and never invoked");
    ownerTable[3]=neko.data()+0x67a70;
    tablePointer=reinterpret_cast<void**>(1);std::memcpy(panels.nodes[2].clientObject.data(),&tablePointer,sizeof(tablePointer));
    Check(!runtime.ReadNativeHudPaintState(2,state),"an unreadable primary vtable is rejected before its method slot is used");
    tablePointer=ownerTable;std::memcpy(panels.nodes[2].clientObject.data(),&tablePointer,sizeof(tablePointer));
    for(const int bad:{-1,2}) {
        std::memcpy(allowHud.data()+0x30,&bad,sizeof(bad));
        Check(!runtime.ReadNativeHudPaintState(2,state)&&state.rootHandle==0xffffffffu,
              "out-of-contract Neko boolean values cannot authorize hiding native cards");
    }
    std::memcpy(allowHud.data()+0x30,&enabled,sizeof(enabled));
    void* invalid=reinterpret_cast<void*>(1);std::memcpy(neko.data()+0x207c0c,&invalid,sizeof(invalid));
    Check(!runtime.ReadNativeHudPaintState(2,state),"an unreadable Neko ConVar parent remains a guarded failure");
    void* empty=nullptr;std::memcpy(neko.data()+0x207c0c,&empty,sizeof(empty));
    Check(!runtime.ReadNativeHudPaintState(2,state),"a missing Neko ConVar parent cannot authorize replacement");
    std::memcpy(neko.data()+0x207c0c,&allow,sizeof(allow));
    const auto rootCalls=panels.callsByPanel[2];vgui.Retire(2);
    Check(!runtime.ReadNativeHudPaintState(2,state)&&panels.callsByPanel[2]==rootCalls,
          "an expired input HPanel is rejected before any class or owner IPanel access");
    vgui.Add(2);
    Check(runtime.ReadNativeHudPaintState(2,state)&&state.rootHandle!=initialHandle,
          "raw root address reuse is represented by a new serial-numbered lifetime token");
    panelTable[55]=reinterpret_cast<void*>(&ReplacedClientPanel);
    Check(!runtime.ReadNativeHudPaintState(2,state)&&state.rootHandle==0xffffffffu,
          "root replacement during owner lookup invalidates the complete paint-state result");
    panelTable[55]=reinterpret_cast<void*>(&FakeClientPanel);
    Check(runtime.ReadNativeHudPaintState(2,state)&&panels.retiredCalls==0&&panels.unresolvedCalls==0,
          "a new current lifetime recovers without any stale or unresolved panel dereferences");
    g.neko=nullptr;
    Check(!runtime.ReadNativeHudPaintState(2,state),"missing Neko module cannot authorize paint suppression");
    g.neko=reinterpret_cast<HMODULE>(neko.data());g.ready=false;
    Check(!runtime.ReadNativeHudPaintState(2,state),"an uninitialized adapter cannot authorize native replacement");
    g=State();g_runtimeCounters={};AbortMemoryQueries();
}
void CheckIncrementalSelfDiscovery() {
    using namespace neh;
    void* engineTable[27]{};engineTable[5]=reinterpret_cast<void*>(&FakeScreen);
    FakeEngine engine{engineTable};
    void* panelTable[56]{};
    panelTable[5]=reinterpret_cast<void*>(&FakePanelSize);
    panelTable[10]=reinterpret_cast<void*>(&FakePanelPos);
    panelTable[11]=reinterpret_cast<void*>(&FakePanelClip);
    panelTable[15]=reinterpret_cast<void*>(&FakePanelVisible);
    panelTable[17]=reinterpret_cast<void*>(&FakeChildCount);
    panelTable[18]=reinterpret_cast<void*>(&FakeChild);
    panelTable[20]=reinterpret_cast<void*>(&FakeParent);
    panelTable[26]=reinterpret_cast<void*>(&FakePanelVisible);
    panelTable[36]=reinterpret_cast<void*>(&FakePanelName);
    panelTable[37]=reinterpret_cast<void*>(&FakePanelClass);
    panelTable[55]=reinterpret_cast<void*>(&FakeClientPanel);
    FakePanels panels{};panels.vtable=panelTable;
    FakeVgui vgui;panels.vgui=&vgui;
    auto add=[&](std::uint32_t id,std::uint32_t parent,const char* name,const char* klass,Rect bounds) {
        vgui.Add(id);
        panels.nodes[id]={name,klass,parent,bounds,{0,0,2560,1440},true,{}};
        if(parent)panels.nodes[parent].children.push_back(id);
    };
    auto reset=[&](std::size_t size) {
        panels.nodes.assign(size,FakePanelNode{});
        vgui.Clear();panels.callsByPanel.clear();panels.retiredCalls=panels.unresolvedCalls=0;panels.watched=0;
        panels.visibleCalls=panels.childCountCalls=panels.childCalls=panels.classCalls=panels.nameCalls=0;
        g=State();g.engine=&engine;g.panel=&panels;g.vgui=&vgui;
        ResetPanelCaches();g_runtimeCounters={};
        add(1,0,"Root","Panel",{0,0,2560,1440});
        add(2,1,"CHudTeamDisplay","CHudTeamDisplay",{0,0,2560,1440});
    };
    std::uint32_t root=1,teamRoot=2;int width=2560,height=1440,view=1;
    Rect bounds{};bool bounded=true;
    auto step=[&](DWORD now) {
        const auto visited=g_runtimeCounters.traversedNodes;
        const auto edges=panels.childCalls;
        const bool found=SelfDiscoveryStep(root,teamRoot,width,height,view,now,bounds);
        bounded=bounded&&g_runtimeCounters.traversedNodes-visited<=16&&panels.childCalls-edges<=64&&
                g.selfDiscovery.queue.size()<=4096&&g.selfDiscovery.seen.size()<=4096;
        return found;
    };
    auto finish=[&](DWORD now) {
        bool found=step(now);
        for(int i=0;i<10000&&g.selfDiscovery.pending;++i)found=step(now);
        return found;
    };
    reset(512);
    for(std::uint32_t id=3;id<=250;++id)add(id,1,"Decoration","Panel",{0,0,16,16});
    add(300,1,"CHudLocalPlayerDisplay","CHudLocalPlayerDisplay",{2080,72,480,1368});
    add(301,300,"LocalPlayer","LocalPlayerPanel",{2080,1152,480,360});
    Check(!step(1000)&&g.selfDiscovery.pending&&g.selfPanel==0,
          "self discovery does not publish a candidate before the complete tree is checked");
    Check(finish(1000)&&!g.selfDiscovery.pending&&g.selfPanel==301&&g.selfPanelIsNamed,
          "incremental discovery eventually commits the unique named self HUD");
    Check(bounded&&g_runtimeCounters.selfDiscoveries==1&&g.selfDiscovery.visited>=250,
          "self tree work stays within sixteen classified nodes and sixty-four child edges per step");
    panels.nodes[301].bounds={2050,1100,420,240};g_runtimeCounters={};
    Check(step(1010)&&bounds.x==2050&&bounds.y==1100&&bounds.w==420&&bounds.h==240&&
          g_runtimeCounters.selfDiscoveries==0&&g_runtimeCounters.traversedNodes==0,
          "warm self handles use current bounds without another full-tree discovery");

    add(320,1,"CHudLocalPlayerDisplay","CHudLocalPlayerDisplay",{2080,72,480,1368});
    add(321,320,"LocalPlayer","LocalPlayerPanel",{2080,1152,480,288});
    Check(!finish(31000)&&!g.selfDiscovery.pending&&g.selfPanel==0,
          "periodic self refresh rejects duplicate visible local HUD roots");
    const auto deadline=g.nextSelfScan;
    const auto discoveries=g_runtimeCounters.selfDiscoveries;
    g_runtimeCounters.traversedNodes=0;
    Check(!step(deadline-1)&&g_runtimeCounters.selfDiscoveries==discoveries&&
          g_runtimeCounters.traversedNodes==0&&deadline>=61000,
          "failed self discovery waits thirty seconds without restarting a full scan");
    panels.nodes[320].visible=false;
    Check(finish(deadline)&&g.selfPanel==301,
          "self discovery retries at the deadline and excludes hidden duplicate roots");
    panels.nodes[301].visible=false;
    Check(!step(deadline+1)&&g.selfPanel==0,
          "a newly hidden cached self HUD cannot keep stale collision bounds");
    panels.nodes[301].visible=true;ResetPanelCaches();
    Check(finish(deadline+2)&&g.selfPanel==301,"self discovery recovers after cache reset");

    auto changedKey=[&](const char* label) {
        const auto count=g_runtimeCounters.selfDiscoveries;
        step(deadline+3);
        Check(g_runtimeCounters.selfDiscoveries==count+1,label);
    };
    width=1920;changedKey("self screen-width changes begin a fresh bounded discovery");
    height=1080;changedKey("self screen-height changes begin a fresh bounded discovery");
    view=2;changedKey("view-player changes discard an in-flight self search");
    teamRoot=3;changedKey("team-root changes discard an in-flight self search");
    root=300;changedKey("surface-root changes discard an in-flight self search");
    FakePanels replacement=panels;g.panel=&replacement;
    changedKey("panel-interface changes cannot reuse an in-flight self search");
    FakeVgui replacementVgui=vgui;g.vgui=&replacementVgui;
    changedKey("IVGui-interface changes cannot reuse an in-flight self search");
    g.vgui=&vgui;
    g.panel=&panels;ResetPanelCaches();
    Check(!g.selfDiscovery.pending&&g.selfPanel==0&&g.nextSelfScan==0,
          "panel cache reset also clears incremental self discovery and retry state");

    root=1;teamRoot=2;width=2560;height=1440;view=1;bounded=true;
    reset(5000);std::uint32_t leaf=103;
    for(std::uint32_t branch=3;branch<=102;++branch)add(branch,1,"Branch","Panel",{0,0,16,16});
    for(std::uint32_t branch=3;branch<=102;++branch)
        for(int i=0;i<45;++i)add(leaf++,branch,"Leaf","Panel",{0,0,16,16});
    Check(!finish(5000)&&!g.selfDiscovery.pending&&std::strcmp(g.selfDiscovery.lastResult,"node_limit")==0,
          "oversized self trees fail closed after the fixed node limit");
    Check(bounded&&g.selfDiscovery.visited<=4096&&g.selfDiscovery.queue.size()<=4096,
          "node-limit failure preserves the per-step and total storage bounds");
    const auto limitedCount=g_runtimeCounters.selfDiscoveries;
    Check(!step(g.nextSelfScan-1)&&g_runtimeCounters.selfDiscoveries==limitedCount,
          "oversized trees cannot trigger repeated scans during the retry delay");

    reset(128);bounded=true;
    for(std::uint32_t id=10;id<43;++id)
        add(id,1,"CHudLocalPlayerDisplay","CHudLocalPlayerDisplay",{2080,72,480,1368});
    Check(!finish(7000)&&!g.selfDiscovery.pending&&
          std::strcmp(g.selfDiscovery.lastResult,"candidate_limit")==0&&bounded,
          "excess candidate roots fail closed without unbounded candidate storage");
    reset(8);bounded=true;
    panels.nodes[1].children.assign(4097,0);
    Check(!finish(7100)&&!g.selfDiscovery.pending&&
          std::strcmp(g.selfDiscovery.lastResult,"read_failed")==0&&bounded,
          "invalid child counts stop incremental discovery without adopting partial results");
    reset(8);bounded=true;
    add(3,1,"Decoration","Panel",{0,0,16,16});panels.nodes[3].children.push_back(1);
    Check(!finish(7200)&&!g.selfDiscovery.pending&&g.selfDiscovery.visited==3&&bounded,
          "cyclic child edges are deduplicated and cannot keep a self scan running forever");
    reset(512);bounded=true;
    for(std::uint32_t id=3;id<=250;++id)add(id,1,"Decoration","Panel",{0,0,16,16});
    step(7300);
    for(int i=0;i<10&&g.selfDiscovery.pending&&g.selfDiscovery.queue.front().nextChild==0;++i)step(7300);
    const bool partial=g.selfDiscovery.pending&&g.selfDiscovery.queue.front().inspected&&
                       g.selfDiscovery.queue.front().nextChild<g.selfDiscovery.queue.front().childCount;
    panels.nodes[1].children.pop_back();const auto edgesBefore=panels.childCalls;
    Check(partial&&!step(7301)&&!g.selfDiscovery.pending&&
          std::strcmp(g.selfDiscovery.lastResult,"tree_changed")==0&&panels.childCalls==edgesBefore,
          "resuming a changed child list stops before using stale GetChild indices");
    auto primeCache=[&](DWORD now) {
        reset(5000);bounded=true;
        add(4800,1,"CHudLocalPlayerDisplay","CHudLocalPlayerDisplay",{2080,72,480,1368});
        add(4801,4800,"LocalPlayer","LocalPlayerPanel",{2080,1152,480,288});
        return finish(now)&&g.selfPanel==4801;
    };
    const bool nodeCache=primeCache(9000);leaf=103;
    for(std::uint32_t branch=3;branch<=102;++branch)add(branch,1,"Branch","Panel",{0,0,16,16});
    for(std::uint32_t branch=3;branch<=102;++branch)
        for(int i=0;i<45;++i)add(leaf++,branch,"Leaf","Panel",{0,0,16,16});
    panels.nodes[4801].bounds={2050,1100,420,240};
    Check(nodeCache&&finish(39000)&&g.selfPanel==4801&&bounds.x==2050&&bounds.h==240&&
          std::strcmp(g.selfDiscovery.lastResult,"node_limit")==0&&bounded,
          "incomplete node-limited refresh preserves an independently fresh self handle");
    const auto preservedCount=g_runtimeCounters.selfDiscoveries;
    Check(step(g.nextSelfScan-1)&&g.selfPanel==4801&&g_runtimeCounters.selfDiscoveries==preservedCount,
          "preserved self handles avoid repeating an incomplete scan during backoff");
    const bool candidateCache=primeCache(10000);
    for(std::uint32_t id=10;id<43;++id)
        add(id,1,"CHudLocalPlayerDisplay","CHudLocalPlayerDisplay",{2080,72,480,1368});
    Check(candidateCache&&finish(40000)&&g.selfPanel==4801&&
          std::strcmp(g.selfDiscovery.lastResult,"candidate_limit")==0&&bounded,
          "incomplete candidate-limited refresh preserves a fresh previous self handle");
    const bool readCache=primeCache(11000);panels.nodes[1].children.assign(4097,0);
    Check(readCache&&finish(41000)&&g.selfPanel==4801&&
          std::strcmp(g.selfDiscovery.lastResult,"read_failed")==0&&bounded,
          "failed tree read preserves only the separately revalidated previous self handle");
    root=0;
    Check(!step(41001)&&!g.selfDiscovery.pending&&g.selfPanel==0,
          "missing surface root clears self discovery instead of traversing an invalid handle");
    g=State();g_runtimeCounters={};
}
void CheckSafePanelLifetimes() {
    using namespace neh;
    FakeVgui vgui;g=State();g.vgui=&vgui;
    vgui.Add(1);SafePanel original{},replacement{};
    Check(CaptureSafePanel(1,original)&&original.raw==1&&original.handle==0&&ResolveSafePanel(original)==1,
          "IVGui slot fifteen and sixteen accept a valid zero HPanel and round-trip its raw panel");
    vgui.Retire(1);
    Check(ResolveSafePanel(original)==0,"deleted panels no longer resolve from their saved serial handle");
    vgui.Add(1);
    Check(CaptureSafePanel(1,replacement)&&replacement.handle!=original.handle&&
          ResolveSafePanel(original)==0&&ResolveSafePanel(replacement)==1,
          "reusing a raw panel address never revives a reference from its earlier generation");
    vgui.handles[replacement.handle]=2;vgui.current[2]=replacement.handle;
    Check(ResolveSafePanel(replacement)==0,
          "resolved panels must match the saved raw identity as well as having a valid handle");
    g.vgui=nullptr;SafePanel unavailable{};
    Check(!CaptureSafePanel(1,unavailable)&&ResolveSafePanel(original)==0,
          "missing IVGui rejects lifetime references without calling an IPanel method");

    void* engineTable[27]{};
    engineTable[5]=reinterpret_cast<void*>(&FakeScreen);
    engineTable[26]=reinterpret_cast<void*>(&FakeInGame);
    FakeEngine engine{engineTable};
    void* panelTable[56]{};
    panelTable[5]=reinterpret_cast<void*>(&FakePanelSize);
    panelTable[10]=reinterpret_cast<void*>(&FakePanelPos);
    panelTable[11]=reinterpret_cast<void*>(&FakePanelClip);
    panelTable[15]=reinterpret_cast<void*>(&FakePanelVisible);
    panelTable[17]=reinterpret_cast<void*>(&FakeChildCount);
    panelTable[18]=reinterpret_cast<void*>(&FakeChild);
    panelTable[20]=reinterpret_cast<void*>(&FakeParent);
    panelTable[26]=reinterpret_cast<void*>(&FakePanelVisible);
    panelTable[36]=reinterpret_cast<void*>(&FakePanelName);
    panelTable[37]=reinterpret_cast<void*>(&FakePanelClass);
    panelTable[55]=reinterpret_cast<void*>(&FakeClientPanel);
    FakePanels panels{};panels.vtable=panelTable;panels.vgui=&vgui;
    void* entityTable[7]{};entityTable[3]=reinterpret_cast<void*>(&FakeExistingEntity);
    struct ExistingEntities {void** vtable;} entities{entityTable};
    std::vector<unsigned char> neko(0x207c10);std::array<unsigned char,0x34> allowHud{};
    void* allow=allowHud.data();int enabled=1;
    std::memcpy(neko.data()+0x207c0c,&allow,sizeof(allow));
    std::memcpy(allowHud.data()+0x30,&enabled,sizeof(enabled));
    auto add=[&](std::uint32_t id,std::uint32_t parent,const char* name,const char* klass,Rect bounds) {
        vgui.Add(id);panels.nodes[id]={name,klass,parent,bounds,{0,0,2560,1440},true,{}};
        if(parent)panels.nodes[parent].children.push_back(id);
    };
    auto reset=[&]() {
        vgui.Clear();panels.nodes.assign(512,FakePanelNode{});panels.callsByPanel.clear();
        panels.retiredCalls=panels.unresolvedCalls=0;panels.watched=0;
        g=State();g.ready=true;g.engine=&engine;g.panel=&panels;g.vgui=&vgui;
        g.entities=&entities;g.neko=reinterpret_cast<HMODULE>(neko.data());
        add(1,0,"Root","Panel",{0,0,2560,1440});
        add(2,1,"CHudTeamDisplay","CHudTeamDisplay",{0,0,2560,1440});
        for(std::uint32_t i=0;i<4;++i) {
            const Rect rect{20+static_cast<int>(i)*150,920,140,64};
            add(3+i,2,"Teammate","TeamDisplayPlayer",rect);
            add(7+i,3+i,"BackgroundImage","ImagePanel",rect);
            const int target=static_cast<int>(i)+2;
            std::memcpy(panels.nodes[3+i].clientObject.data()+0x264,&target,sizeof(target));
        }
        add(20,1,"CHudLocalPlayerDisplay","CHudLocalPlayerDisplay",{2080,72,480,1368});
        add(21,20,"LocalPlayer","LocalPlayerPanel",{2080,1152,480,288});
        ResetPanelCaches();g_runtimeCounters={};
    };
    auto detach=[&](std::uint32_t raw) {
        auto& children=panels.nodes[panels.nodes[raw].parent].children;
        children.erase(std::remove(children.begin(),children.end(),raw),children.end());
        vgui.Retire(raw);
    };
    Rect bounds{};
    auto step=[&](DWORD now) {return SelfDiscoveryStep(1,2,2560,1440,1,now,bounds);};
    auto finish=[&](DWORD now) {
        bool found=step(now);
        for(int i=0;i<10000&&g.selfDiscovery.pending;++i)found=step(now);
        return found;
    };
    reset();
    for(std::uint32_t id=100;id<=250;++id)add(id,1,"Decoration","Panel",{0,0,16,16});
    step(1000);
    for(int i=0;i<100&&g.selfDiscovery.pending&&
        std::none_of(g.selfDiscovery.queue.begin(),g.selfDiscovery.queue.end(),
                     [](const SelfDiscoveryNode& node){return node.panel.raw==100;});++i)step(1000);
    const auto queued=std::find_if(g.selfDiscovery.queue.begin(),g.selfDiscovery.queue.end(),
                                  [](const SelfDiscoveryNode& node){return node.panel.raw==100;});
    const bool savedQueued=queued!=g.selfDiscovery.queue.end()&&!queued->inspected;
    const auto oldQueued=savedQueued?queued->panel:SafePanel{};
    auto& rootChildren=panels.nodes[1].children;
    std::replace(rootChildren.begin(),rootChildren.end(),100u,400u);
    add(400,0,"Replacement","Panel",{0,0,16,16});panels.nodes[400].parent=1;
    vgui.Retire(100);vgui.Add(100);panels.watched=100;
    const auto queuedCalls=panels.callsByPanel[100],invalidCaptures=vgui.invalidCaptures;
    Check(savedQueued&&ResolveSafePanel(oldQueued)==0&&!finish(1010)&&!g.selfDiscovery.pending&&
          std::strcmp(g.selfDiscovery.lastResult,"tree_changed")==0&&
          panels.callsByPanel[100]==queuedCalls&&panels.unresolvedCalls==0&&
          vgui.invalidCaptures==invalidCaptures&&g_runtimeCounters.expiredPanelRefs>0,
          "expired queued generations abort before class, name, child-count or GetChild calls");
    std::replace(rootChildren.begin(),rootChildren.end(),400u,100u);
    panels.nodes[100].parent=1;
    Check(finish(g.nextSelfScan)&&g.selfPanel==21&&g.ready&&panels.unresolvedCalls==0,
          "scheduled discovery recovers after UI recreation without a manual cache reset");
    Runtime runtime;std::string error;
    Check(runtime.ReadNativeSlots(2,error).size()==4&&error.empty()&&g.ready,
          "native HUD updates continue after an expired incremental self scan");

    reset();add(100,1,"Branch","Panel",{0,0,16,16});
    for(std::uint32_t id=101;id<=250;++id)add(id,100,"Leaf","Panel",{0,0,16,16});
    bool partial=false;
    for(int i=0;i<1000&&!partial;++i) {
        step(2000);
        if(!g.selfDiscovery.pending)break;
        if(g.selfDiscovery.cursor<g.selfDiscovery.queue.size()) {
            const auto& node=g.selfDiscovery.queue[g.selfDiscovery.cursor];
            partial=node.panel.raw==100&&node.inspected&&node.nextChild<node.childCount;
        }
    }
    detach(100);const auto currentCalls=panels.callsByPanel[100];
    Check(partial&&!step(2010)&&!g.selfDiscovery.pending&&
          panels.callsByPanel[100]==currentCalls&&panels.retiredCalls==0,
          "an expired partially expanded node is rejected before resuming its child-count guard");

    reset();
    Check(finish(3000)&&g.selfPanel==21,"positive self lifetime fixture discovers its current panel");
    const auto oldSelf=g.selfRef;vgui.Retire(21);vgui.Add(21);panels.watched=21;
    panels.nodes[21].bounds={2050,1100,420,240};
    Check(finish(3010)&&g.selfPanel==21&&g.selfRef.handle!=oldSelf.handle&&
          ResolveSafePanel(oldSelf)==0&&bounds.x==2050&&bounds.h==240&&panels.unresolvedCalls==0,
          "positive self cache resolves generation before names or bounds and rediscovers a reused address");
    detach(21);const auto selfCalls=panels.callsByPanel[21];
    Check(!finish(3020)&&g.selfPanel==0&&panels.callsByPanel[21]==selfCalls&&panels.retiredCalls==0&&g.ready,
          "deleted positive self panels are cleared before any IPanel access while runtime stays enabled");

    reset();NativePanelCandidates(2,2560,1440,4000);
    const auto oldNative=g.nativePanels.panelRefs.front();
    vgui.Retire(3);vgui.Add(3);panels.watched=3;g_runtimeCounters={};
    Check(NativePanelCandidates(2,2560,1440,4010).size()==4&&
          g.nativePanels.panelRefs.front().handle!=oldNative.handle&&ResolveSafePanel(oldNative)==0&&
          g_runtimeCounters.nativeDiscoveries==1&&panels.unresolvedCalls==0,
          "native cache rejects an old generation even when direct child raw values are unchanged");
    detach(3);detach(7);const auto nativeCalls=panels.callsByPanel[3];
    Check(NativePanelCandidates(2,2560,1440,4020).size()==3&&panels.callsByPanel[3]==nativeCalls&&
          panels.retiredCalls==0,"deleted native controls are never queried during cache rediscovery");

    reset();const auto container=panels.nodes[3].bounds;
    CachedVisualCardBounds(3,container,bounds,5000);
    const auto oldBackground=g.visualPanels[3].backgroundRefs.front();
    vgui.Retire(7);vgui.Add(7);panels.watched=7;g_runtimeCounters={};
    Check(CachedVisualCardBounds(3,container,bounds,5010)&&
          g.visualPanels[3].backgroundRefs.front().handle!=oldBackground.handle&&
          ResolveSafePanel(oldBackground)==0&&g_runtimeCounters.visualDiscoveries==1&&panels.unresolvedCalls==0,
          "visual cache resolves background generations before asking for their names or visibility");
    detach(7);const auto backgroundCalls=panels.callsByPanel[7];
    Check(!CachedVisualCardBounds(3,container,bounds,5020)&&panels.callsByPanel[7]==backgroundCalls&&
          panels.retiredCalls==0,"deleted backgrounds are excluded without querying their retired raw panel");

    reset();CachedVisualCardBounds(3,container,bounds,6000);
    const auto oldOwner=g.visualPanels[3].ownerRef;
    vgui.Retire(3);vgui.Add(3);panels.watched=3;
    Check(CachedVisualCardBounds(3,container,bounds,6010)&&
          g.visualPanels[3].ownerRef.handle!=oldOwner.handle&&panels.unresolvedCalls==0,
          "visual owner generation is checked before traversing its children");
    NativePanelCandidates(2,2560,1440,7000);const auto oldRoot=g.nativePanels.rootRef;
    vgui.Retire(2);vgui.Add(2);panels.watched=2;
    Check(NativePanelCandidates(2,2560,1440,7010).size()==4&&
          g.nativePanels.rootRef.handle!=oldRoot.handle&&panels.unresolvedCalls==0,
          "native root generations are checked before direct-child validation");
    g=State();g_runtimeCounters={};
}
struct FakeThemeSurface {
    void** vtable;bool material=true;int width=64,height=64,lookups=0,sizes=0;
    std::string path="vgui/native_fixture_background";
};
bool __fastcall FakeThemeTextureFile(FakeThemeSurface* self,void*,int id,char* output,int capacity) {
    ++self->lookups;
    if(!self->material||id!=601||capacity<=static_cast<int>(self->path.size()))return false;
    std::memcpy(output,self->path.c_str(),self->path.size()+1);return true;
}
void __fastcall FakeThemeTextureSize(FakeThemeSurface* self,void*,int,int* width,int* height) {
    ++self->sizes;*width=self->width;*height=self->height;
}
struct FakeHudResourceFiles {
    void** vtable;
    std::string normal,incap;
    int opens=0,reads=0,closes=0;
};
void* __fastcall FakeHudResourceOpen(FakeHudResourceFiles* self,void*,const char* path,const char* mode,
                                     const char* pathId) {
    ++self->opens;
    if (pathId || std::strcmp(mode,"rb")) return nullptr;
    if (!std::strcmp(path,"resource/UI/HUD/TeammatePanel.res")) return &self->normal;
    if (!std::strcmp(path,"resource/UI/HUD/TeammatePanel_Incap.res")) return &self->incap;
    return nullptr;
}
int __fastcall FakeHudResourceRead(FakeHudResourceFiles* self,void*,void* buffer,int capacity,void* handle) {
    ++self->reads;
    if (capacity<=0 || (handle!=&self->normal && handle!=&self->incap)) return 0;
    const auto& text=*static_cast<const std::string*>(handle);
    const auto bytes=(std::min)(text.size(),static_cast<std::size_t>(capacity));
    std::memcpy(buffer,text.data(),bytes);
    return static_cast<int>(bytes);
}
void __fastcall FakeHudResourceClose(FakeHudResourceFiles* self,void*,void* handle) {
    if (handle==&self->normal || handle==&self->incap) ++self->closes;
}
struct FakeHudResourceSchemeManager {
    void** vtable;
    void* scheme;
    void* bitmap;
    std::uint32_t handle=1;
};
void* __fastcall FakeHudResourceScheme(FakeHudResourceSchemeManager* self,void*,std::uint32_t handle) {
    return handle==self->handle?self->scheme:nullptr;
}
int __fastcall FakeHudResourceScale(FakeHudResourceSchemeManager* self,void*,std::uint32_t handle,int value) {
    return handle==self->handle?5*value:0;
}
void* __fastcall FakeHudResourceImage(FakeHudResourceSchemeManager* self,void*,const char* name,bool scale) {
    return scale && !std::strcmp(name,"hud/scratch_fixture")?self->bitmap:nullptr;
}
bool __fastcall FakeHudResourceProportional(void*,void*) {return true;}
unsigned long __fastcall FakeHudResourceFont(void*,void*,const char* name,bool proportional) {
    return proportional && !std::strcmp(name,"DownFixtureFont")?910ul:0ul;
}
std::uint32_t* __fastcall FakeHudResourceColor(void*,void*,std::uint32_t* out,const char* name,std::uint32_t fallback) {
    *out=!std::strcmp(name,"DownFixtureColor")?0xc81e140au:fallback;
    return out;
}
void CheckGameHudItemIconLookup() {
    using namespace neh;
    constexpr int pipeBomb=14,adrenaline=23,vomitJar=25;
    constexpr std::uint16_t invalidHandle=0xffff;
    std::vector<unsigned char> client(0x7b4900);
    std::array<unsigned char,16*4> records{};
    std::array<unsigned char,0x884> pipeInfo{},adrenalineInfo{};
    std::array<unsigned char,0xac> pipeArt{},adrenalineArt{};
    const auto put=[](auto& bytes,std::size_t offset,const auto& value) {
        std::memcpy(bytes.data()+offset,&value,sizeof(value));
    };
    const auto setHandle=[&](int item,std::uint16_t handle) {
        put(client,0x7b4838+2*static_cast<std::size_t>(item),handle);
    };
    for (int item=0;item<=55;++item) setHandle(item,invalidHandle);
    put(client,0x7b4830,std::uint8_t(1));
    put(client,0x6dfdf2,std::uint16_t(3));
    put(client,0x6dfde4,static_cast<void*>(records.data()));
    put(records,16+0xc,static_cast<void*>(pipeInfo.data()));
    put(records,32+0xc,static_cast<void*>(adrenalineInfo.data()));
    // A readable decoy past the stated capacity makes a missed bounds check observable.
    put(records,48+0xc,static_cast<void*>(pipeInfo.data()));
    setHandle(pipeBomb,1);setHandle(adrenaline,2);setHandle(vomitJar,3);
    put(pipeInfo,0x880,static_cast<void*>(pipeArt.data()));
    put(adrenalineInfo,0x880,static_cast<void*>(adrenalineArt.data()));
    put(pipeArt,0x88,601);
    const std::array<float,4> uv={0.125f,0.25f,0.5f,0.75f};
    std::memcpy(pipeArt.data()+0x8c,uv.data(),sizeof(uv));
    put(pipeArt,0xa0,24);put(pipeArt,0xa8,32);
    put(adrenalineArt,0x80,static_cast<unsigned char>(1));
    put(adrenalineArt,0x82,static_cast<signed char>('P'));
    put(adrenalineArt,0x84,900ul);
    put(adrenalineArt,0xa0,12);put(adrenalineArt,0xa8,14);

    void* surfaceTable[33]{};
    surfaceTable[26]=reinterpret_cast<void*>(&FakeThemeTextureFile);
    surfaceTable[30]=reinterpret_cast<void*>(&FakeThemeTextureSize);
    FakeThemeSurface surface{surfaceTable};
    surface.path="vgui/native_fixture_pipebomb";
    g=State();g_runtimeCounters={};
    g.client=reinterpret_cast<HMODULE>(client.data());g.surface=&surface;g.ready=true;

    for (int item:{-1,0,56,(std::numeric_limits<int>::max)()})
        Check(!GameHudItemIconStep(item,1000),"invalid item ID cannot resolve an inventory icon");
    Check(!GameHudItemIconStep(13,1000)&&!GameHudItemIconStep(vomitJar,1000)&&surface.lookups==0,
          "invalid and out-of-capacity weapon handles cannot reach Surface");

    surface.material=false;
    Check(!GameHudItemIconStep(pipeBomb,1000)&&surface.lookups==1&&surface.sizes==0,
          "a cold item texture miss does not yield art from another item");
    const auto* glyph=GameHudItemIconStep(adrenaline,1000);
    Check(glyph&&glyph->item==adrenaline&&glyph->usingFont&&glyph->font==900ul&&glyph->glyph==L'P'&&surface.lookups==1,
          "an independent item resolves its exact weapon font icon without a native panel or texture lookup");
    surface.material=true;
    Check(!GameHudItemIconStep(pipeBomb,1249)&&surface.lookups==1,
          "a missing item icon does not query Surface again before its retry deadline");
    const auto* image=GameHudItemIconStep(pipeBomb,1250);
    Check(image&&image->item==pipeBomb&&!image->usingFont&&image->texture.id==601&&
          image->texture.material==surface.path&&image->texture.u0==uv[0]&&image->texture.v1==uv[3]&&
          image->width==24&&image->height==32&&surface.lookups==2&&surface.sizes==1,
          "the same item recovers its loaded weapon texture and crop when the miss retry expires");

    surface.path="vgui/native_fixture_pipebomb_updated";
    const auto* cached=GameHudItemIconStep(pipeBomb,2249);
    Check(cached&&cached->texture.material!=surface.path&&surface.lookups==2,
          "a successful item lookup reuses its copied descriptor through its cache window");
    const auto* refreshed=GameHudItemIconStep(pipeBomb,2250);
    Check(refreshed&&refreshed->texture.material==surface.path&&surface.lookups==3,
          "a successful item lookup refreshes against the current Surface texture at its deadline");

    ResetPanelCaches();surface.material=false;
    Check(!GameHudItemIconStep(pipeBomb,2251)&&surface.lookups==4,
          "a map reset drops the cached item icon even before its success deadline");
    ResetPanelCaches();put(client,0x7b4830,std::uint8_t(0));surface.material=true;
    Check(!GameHudItemIconStep(pipeBomb,2252)&&surface.lookups==4,
          "an uninitialized weapon table never reaches Surface");
    g=State();g_runtimeCounters={};
}
void CheckGameHudThemeCapture() {
    using namespace neh;
    std::vector<unsigned char> client(0x7ccc00),vguiModule(0x43500);
    void* engineTable[27]{};engineTable[5]=reinterpret_cast<void*>(&FakeScreen);
    FakeEngine engine{engineTable};FakeVgui vgui;
    void* panelTable[56]{};
    panelTable[3]=reinterpret_cast<void*>(&FakePanelLocalPos);panelTable[13]=reinterpret_cast<void*>(&FakePanelInset);
    panelTable[5]=reinterpret_cast<void*>(&FakePanelSize);panelTable[9]=reinterpret_cast<void*>(&FakePanelZ);
    panelTable[10]=reinterpret_cast<void*>(&FakePanelPos);panelTable[11]=reinterpret_cast<void*>(&FakePanelClip);
    panelTable[15]=reinterpret_cast<void*>(&FakePanelVisible);panelTable[17]=reinterpret_cast<void*>(&FakeChildCount);
    panelTable[18]=reinterpret_cast<void*>(&FakeChild);panelTable[20]=reinterpret_cast<void*>(&FakeParent);
    panelTable[26]=reinterpret_cast<void*>(&FakePanelVisible);panelTable[27]=reinterpret_cast<void*>(&FakePanelScheme);
    panelTable[36]=reinterpret_cast<void*>(&FakePanelName);panelTable[37]=reinterpret_cast<void*>(&FakePanelClass);
    panelTable[55]=reinterpret_cast<void*>(&FakeClientPanel);
    FakePanels panels{};panels.vtable=panelTable;panels.vgui=&vgui;
    void* surfaceTable[33]{};
    surfaceTable[26]=reinterpret_cast<void*>(&FakeThemeTextureFile);surfaceTable[30]=reinterpret_cast<void*>(&FakeThemeTextureSize);
    FakeThemeSurface surface{surfaceTable};
    auto put=[&](std::uint32_t id,int offset,const auto& value) {
        std::memcpy(panels.nodes[id].clientObject.data()+offset,&value,sizeof(value));
    };
    auto add=[&](std::uint32_t id,std::uint32_t parent,const char* name,const char* klass,Rect rect,int tableRva=0) {
        vgui.Add(id);panels.nodes[id]={name,klass,parent,rect,{0,0,2560,1440},true,{}};
        if(parent)panels.nodes[parent].children.push_back(id);
        if(tableRva) {void* table=client.data()+tableRva;put(id,0,table);}
        const float alpha=255;put(id,0x118,alpha);
        const std::array<unsigned char,4> color={10,20,30,240};put(id,0x8c,color);
    };
    add(1,0,"Root","Panel",{0,0,2560,1440});
    add(2,1,"CHudTeamDisplay","CHudTeamDisplay",{0,0,2560,1440});
    add(3,2,"Teammate","TeamDisplayPlayer",{20,920,200,100});
    add(4,3,"Head","ImagePanel",{25,925,30,40},0x65d98c);
    add(5,3,"Name","Label",{65,925,145,12},0x65dfdc);
    const int alignment=3,insetX=3,insetY=2;put(5,0x154,alignment);put(5,0x170,insetX);put(5,0x174,insetY);
    add(6,3,"HealthNumber","Label",{65,940,80,15},0x65dfdc);
    add(7,3,"BackgroundImage","ImagePanel",{18,918,204,104},0x65d98c);
    add(8,3,"Health","HealthPanel",{65,975,145,15},0x5c8cbc);
    add(10,3,"IconForSlot_Grenade","CIconPanel",{165,980,20,15},0x590654);panels.nodes[10].visible=false;
    const std::array<unsigned char,4> emptyItemTint={210,211,212,200};put(10,0x1d6,emptyItemTint);
    std::array<std::array<unsigned char,0x40>,2> textImages{};
    for(std::size_t i=0;i<textImages.size();++i) {
        void* table=client.data()+0x66cb5c;const unsigned long font=900+static_cast<unsigned long>(i);
        std::memcpy(textImages[i].data(),&table,sizeof(table));
        std::memcpy(textImages[i].data()+0x20,&font,sizeof(font));
        void* image=textImages[i].data();put(5+static_cast<std::uint32_t>(i),0x158,image);
    }
    std::array<unsigned char,8> bitmap{};
    void* bitmapTable=vguiModule.data()+0x434f0;const int textureId=601;
    std::memcpy(bitmap.data(),&bitmapTable,sizeof(bitmapTable));std::memcpy(bitmap.data()+4,&textureId,sizeof(textureId));
    void* image=bitmap.data();put(7,0x150,image);
    const unsigned char scaleImage=1;put(7,0x160,scaleImage);
    const std::array<unsigned char,4> tint={101,102,103,200},numberColor={9,99,177,221};
    put(7,0x16c,tint);put(6,0x8c,numberColor);const float alpha=128;put(7,0x118,alpha);
    g=State();g.ready=true;g.engine=&engine;g.panel=&panels;g.vgui=&vgui;g.surface=&surface;
    g.client=reinterpret_cast<HMODULE>(client.data());g.vguiModule=reinterpret_cast<HMODULE>(vguiModule.data());
    NativePanelCandidates(2,2560,1440,1000);
    Snapshot snapshot;snapshot.inGame=true;snapshot.localIndex=snapshot.viewIndex=1;
    Player player;player.index=2;player.alive=true;player.health=80;player.temporaryHealth=79;player.maxHealth=100;
    snapshot.players.push_back(player);
    HudSlot slot;slot.panel=3;slot.playerIndex=2;slot.visible=true;slot.bounds=panels.nodes[3].bounds;
    slot.hasVisibleBounds=true;slot.visibleBounds=slot.bounds;std::vector<HudSlot> slots={slot};
    std::string error;g_runtimeCounters={};
    const auto cold=GameHudThemeStep(snapshot,slots,2560,1440,1000,error);
    Check(error.empty()&&cold.available&&cold.states[0].ready&&cold.states[0].width==200&&cold.states[0].height==100&&
          cold.states[0].portrait.present&&cold.states[0].name.present&&cold.states[0].number.present&&
          cold.states[0].health.present&&cold.states[0].bar.present&&
          g_runtimeCounters.themeCaptures==1,
          "native theme capture reads only verified current controls into a ready value-only normal style");
    Check(cold.states[0].name.rect.x==45&&cold.states[0].name.rect.y==5&&cold.states[0].name.rect.w==145&&
          cold.states[0].name.font==900&&cold.states[0].number.font==901&&
          cold.states[0].name.alignment==3&&cold.states[0].name.insetX==3&&cold.states[0].name.insetY==2&&
          cold.states[0].number.color.r==9&&cold.states[0].number.color.g==99&&cold.states[0].number.color.b==177&&
          cold.numberPaletteObserved[0]&&cold.numberPalette[0].a==221,
          "copied native label bounds, fonts and permanent-health color band retain their loaded values");
    Check(cold.states[0].images.size()==1&&cold.states[0].images[0].texture.id==601&&
          !cold.states[0].images[0].characterIncap&&
          cold.states[0].images[0].texture.material=="vgui/native_fixture_background"&&
          std::fabs(cold.states[0].images[0].texture.u0-1.0f/102)<0.00001f&&
          std::fabs(cold.states[0].images[0].texture.v0-1.0f/52)<0.00001f&&
          cold.states[0].images[0].texture.tint.a==100,
          "native bitmap capture preserves loaded texture identity, clipping UVs and effective alpha");
    Check(cold.states[0].items[0].present&&cold.states[0].items[0].rect.x==145&&
          cold.states[0].items[0].rect.y==60&&cold.states[0].items[0].color.a==200&&cold.icons.empty(),
          "empty hidden source item slots preserve native position and tint without borrowing another item's texture");
    {
        auto savedTheme=g.gameTheme;
        const auto savedSlot=panels.nodes[10];
        const auto savedParent=panels.nodes[3];
        auto& itemPanel=panels.nodes[10];
        auto& itemParent=panels.nodes[3];
        std::array<unsigned char,0xac> oldIcon{};
        const auto putIcon=[&](std::size_t offset,const auto& value) {
            std::memcpy(oldIcon.data()+offset,&value,sizeof(value));
        };
        // This otherwise valid image can be left on the hidden panel by an old item.
        const std::array<float,4> oldUv={0,0,1,1};
        putIcon(0x88,textureId);putIcon(0x8c,oldUv);putIcon(0xa0,20);putIcon(0xa8,15);
        put(10,0x150,static_cast<void*>(oldIcon.data()));put(10,0x1d4,static_cast<unsigned char>(1));
        snapshot.players[0].throwable=14;
        const auto isItemAt=[](const GameHudTheme& theme,int x,int y) {
            const auto& item=theme.states[0].items[0];
            return theme.available&&item.present&&item.rect.x==x&&item.rect.y==y&&item.rect.w==20&&item.rect.h==15;
        };
        g.gameTheme={};
        const auto held=GameHudThemeStep(snapshot,slots,2560,1440,2000,error);
        Check(isItemAt(held,145,60)&&held.icons.empty(),
              "a held item keeps verified hidden slot geometry without borrowing stale native art");

        itemPanel.bounds.x=1;itemPanel.bounds.y=1;itemPanel.hasLocalPos=true;
        itemPanel.localX=93;itemPanel.localY=33;itemParent.insets={7,9,11,13};
        g.gameTheme={};
        const auto reconstructed=GameHudThemeStep(snapshot,slots,2560,1440,2500,error);
        Check(isItemAt(reconstructed,100,42)&&reconstructed.icons.empty(),
              "stale hidden absolute bounds recover from local position and the visible parent's inset");
        const auto captures=g_runtimeCounters.themeCaptures;
        auto& itemChildren=itemParent.children;
        itemChildren.erase(std::remove(itemChildren.begin(),itemChildren.end(),10u),itemChildren.end());
        const auto missing=GameHudThemeStep(snapshot,slots,2560,1440,3000,error);
        Check(isItemAt(missing,100,42)&&missing.icons.empty()&&g_runtimeCounters.themeCaptures==captures+1,
              "a fresh compatible capture retains previous geometry when a held item's slot disappears");

        itemChildren=savedParent.children;
        itemPanel.localX=205;g.gameTheme={};
        const auto outside=GameHudThemeStep(snapshot,slots,2560,1440,3500,error);
        Check(outside.available&&!outside.states[0].items[0].present,
              "local reconstruction rejects a slot outside the native teammate card");
        itemPanel.localX=93;itemPanel.bounds.w=0;g.gameTheme={};
        const auto empty=GameHudThemeStep(snapshot,slots,2560,1440,4000,error);
        Check(empty.available&&!empty.states[0].items[0].present,
              "local reconstruction never accepts a zero-width native item slot");

        snapshot.players[0].throwable=0;
        itemPanel=savedSlot;itemParent=savedParent;g.gameTheme=std::move(savedTheme);
    }
    {
        auto savedTheme=std::move(g.gameTheme);
        const auto savedParent=panels.nodes[3];
        void* playerTable[22]{};
        playerTable[21]=reinterpret_cast<void*>(&FakeHudResourceProportional);
        put(3,0,static_cast<void*>(playerTable));
        void* filesystemTable[4]{};
        filesystemTable[0]=reinterpret_cast<void*>(&FakeHudResourceRead);
        filesystemTable[2]=reinterpret_cast<void*>(&FakeHudResourceOpen);
        filesystemTable[3]=reinterpret_cast<void*>(&FakeHudResourceClose);
        FakeHudResourceFiles files{filesystemTable,R"kv(
            "TeammatePanel" {
                "Health" { xpos 9 ypos 11 wide 29 tall 3 }
            }
        )kv",R"kv(
            "TeammatePanel_Incap" {
                "BackgroundImage" { xpos 2 ypos 3 wide 32 tall 16 drawcolor "171 172 173 174" }
                "Name" { xpos 5 ypos 17 wide 24 tall 2 font "DownFixtureFont" textinsetx 1 }
                "HealthNumber" { xpos 18 ypos 6 wide 8 tall 3 fgcolor_override "DownFixtureColor" }
                "Health" { xpos 5 ypos 13 wide 23 tall 2 }
                "IconForSlot_Grenade" { xpos 18 ypos 11 wide 3 tall 2 visible 0 }
                "TopScratch" { xpos 8 ypos 11 wide 10 tall 3 image "hud/scratch_fixture" scaleImage 1 }
            }
        )kv"};
        void* schemeTable[6]{};
        schemeTable[3]=reinterpret_cast<void*>(&FakeHudResourceFont);
        schemeTable[5]=reinterpret_cast<void*>(&FakeHudResourceColor);
        void** schemeObject=schemeTable;
        void* managerTable[14]{};
        managerTable[6]=reinterpret_cast<void*>(&FakeHudResourceImage);
        managerTable[8]=reinterpret_cast<void*>(&FakeHudResourceScheme);
        managerTable[13]=reinterpret_cast<void*>(&FakeHudResourceScale);
        FakeHudResourceSchemeManager manager{managerTable,&schemeObject,image,savedParent.scheme};
        g.filesystem=&files;g.schemeManager=&manager;g.gameTheme={};
        const auto preloaded=GameHudThemeStep(snapshot,slots,2560,1440,4100,error);
        const auto& prepared=preloaded.preparedIncap;
        Check(error.empty()&&preloaded.states[0].ready&&!preloaded.states[1].ready&&prepared.ready&&
              preloaded.status.find("down_source=resource")!=std::string::npos&&
              files.opens==2&&files.reads==2&&files.closes==2,
              "the first healthy native capture preloads exact downed resources through the mounted filesystem");
        Check(prepared.width==160&&prepared.height==80&&prepared.name.rect.x==15&&prepared.name.rect.y==70&&
              prepared.name.font==910&&prepared.name.insetX==5&&prepared.number.rect.x==80&&prepared.number.rect.y==15&&
              prepared.number.color.r==10&&prepared.number.color.a==200&&
              prepared.health.rect.w==115&&prepared.health.rect.h==10&&prepared.items[0].present&&
              prepared.items[0].rect.x==80&&prepared.items[0].rect.y==40&&
              prepared.images.size()==2&&prepared.images[0].characterIncap&&prepared.images[0].texture.tint.a==174&&
              prepared.images[1].texture.id==601&&prepared.images[1].texture.material==surface.path,
              "prepared downed layout uses its own scaled geometry, scheme font and color, and mounted bitmap");

        const auto captures=g_runtimeCounters.themeCaptures;
        const auto cached=GameHudThemeStep(snapshot,slots,2560,1440,4200,error);
        Check(cached.preparedIncap.ready&&files.opens==2&&g_runtimeCounters.themeCaptures==captures,
              "a warm theme read preserves the prepared layout without reopening resources or recapturing controls");
        const auto resampled=GameHudThemeStep(snapshot,slots,2560,1440,4600,error);
        Check(resampled.preparedIncap.ready&&files.opens==2&&g_runtimeCounters.themeCaptures==captures+1,
              "periodic healthy control sampling preserves the prepared layout without reopening resources");

        files.incap.replace(files.incap.find("18 ypos 6"),2,"19");
        manager.handle=++panels.nodes[3].scheme;
        const auto reloaded=GameHudThemeStep(snapshot,slots,2560,1440,4610,error);
        Check(reloaded.preparedIncap.ready&&reloaded.preparedIncap.number.rect.x==85&&prepared.number.rect.x==80&&
              !reloaded.states[1].ready&&files.opens==4&&files.reads==4&&files.closes==4,
              "changing the native scheme reloads current downed resources without mutating a copied theme");
        g.filesystem=nullptr;g.schemeManager=nullptr;g.gameTheme=std::move(savedTheme);
        panels.nodes[3]=savedParent;
    }
    const unsigned long changedFont=902;std::memcpy(textImages[1].data()+0x20,&changedFont,sizeof(changedFont));
    g_runtimeCounters={};const auto warm=GameHudThemeStep(snapshot,slots,2560,1440,1010,error);
    Check(warm.states[0].number.font==901&&g_runtimeCounters.themeCaptures==0&&g_runtimeCounters.traversedNodes==0,
          "warm theme reads reuse copied values without traversing controls inside the five-hundred-millisecond interval");
    const auto refreshed=GameHudThemeStep(snapshot,slots,2560,1440,1500,error);
    Check(refreshed.states[0].number.font==902&&cold.states[0].number.font==901&&g_runtimeCounters.themeCaptures==1,
          "periodic theme capture refreshes changed controls without mutating previous copied snapshots");
    ++panels.nodes[3].scheme;g_runtimeCounters={};
    GameHudThemeStep(snapshot,slots,2560,1440,1510,error);
    Check(g_runtimeCounters.themeCaptures==1&&g.gameTheme.nextCapture==2010,
          "native scheme changes invalidate the theme before its periodic capture deadline");
    snapshot.players[0].incapacitated=true;g_runtimeCounters={};
    const auto down=GameHudThemeStep(snapshot,slots,2560,1440,1520,error);
    Check(down.states[0].ready&&down.states[1].ready&&g_runtimeCounters.themeCaptures==1,
          "newly observed downed controls are sampled immediately while the normal style stays available");
    snapshot.players[0].incapacitated=false;
    const unsigned long reappliedFont=903;std::memcpy(textImages[0].data()+0x20,&reappliedFont,sizeof(reappliedFont));
    const auto reapplied=GameHudThemeStep(snapshot,slots,2560,1440,1525,error);
    Check(reapplied.states[0].ready&&reapplied.states[0].name.font==903&&reapplied.states[1].ready&&panels.nodes[3].scheme==2,
          "one same-scheme core change refreshes its own state without discarding another state before confirmation");
    const auto confirmed=GameHudThemeStep(snapshot,slots,2560,1440,2025,error);
    Check(confirmed.states[0].ready&&!confirmed.states[1].ready,
          "a repeated same-source same-state font reload invalidates older unobserved resources after confirmation");
    const auto screen=GameHudThemeStep(snapshot,slots,1920,1080,1530,error);
    Check(screen.states[0].ready&&!screen.states[1].ready&&g.gameTheme.width==1920,
          "screen changes discard earlier state samples before capturing their new coordinate space");
    surface.material=false;
    const auto missing=GameHudThemeStep(snapshot,slots,1920,1080,2030,error);
    Check(missing.available&&missing.states[0].images.size()==1&&missing.states[0].name.present&&
          missing.status.find("retained=1000")!=std::string::npos&&g.gameTheme.nextCapture==2130,
          "one missing optional bitmap retains the last compatible appearance and schedules a bounded retry");
    surface.material=true;void* wrongTable=client.data()+0x65dfdd;put(5,0,wrongTable);
    const unsigned long noFont=0;std::memcpy(textImages[1].data()+0x20,&noFont,sizeof(noFont));
    g.gameTheme={};
    const auto invalid=GameHudThemeStep(snapshot,slots,1920,1080,2530,error);
    Check(!invalid.available&&!invalid.states[0].ready,
          "unsupported required control vtables and zero native fonts make the state fall back instead of dropping player information");
    void* labelTable=client.data()+0x65dfdc;put(5,0,labelTable);
    const unsigned long numberFont=901;std::memcpy(textImages[1].data()+0x20,&numberFont,sizeof(numberFont));
    add(9,3,"Name","Label",{65,925,145,12},0x65dfdc);void* duplicateText=textImages[0].data();put(9,0x158,duplicateText);
    const auto ambiguous=GameHudThemeStep(snapshot,slots,1920,1080,3030,error);
    Check(!ambiguous.available&&!ambiguous.states[0].ready,
          "ambiguous duplicate required native labels fall back rather than silently choosing another survivor's name widget");
    panels.nodes[3].children.pop_back();vgui.Retire(9);
    const auto sourceCalls=panels.callsByPanel[3];vgui.Retire(3);panels.nodes[2].children.clear();
    const auto expired=GameHudThemeStep(snapshot,slots,1920,1080,3040,error);
    Check(!expired.available&&panels.callsByPanel[3]==sourceCalls&&panels.retiredCalls==0,
          "expired source HPanel clears copied theme data before any stale IPanel access");
    vgui.Add(3);panels.nodes[2].children.push_back(3);panels.watched=3;
    const auto reused=GameHudThemeStep(snapshot,slots,1920,1080,3050,error);
    Check(!reused.available&&panels.callsByPanel[3]==sourceCalls&&panels.unresolvedCalls==0,
          "reusing a raw source address does not promote the old native lifetime reference into a new theme source");
    NativePanelCandidates(2,1920,1080,3060);
    Check(GameHudThemeStep(snapshot,slots,1920,1080,3060,error).available&&panels.unresolvedCalls==0,
          "a fresh native discovery supplies a new safe source generation and restores theme capture");

    // The live failure used the first source's empty slots although another
    // compatible source had the same-state occupied widgets and loaded icons.
    panels.nodes[10].bounds.w=0;
    add(11,2,"SecondTeammate","TeamDisplayPlayer",{260,920,200,100});
    panels.nodes[11].scheme=panels.nodes[3].scheme;
    for(std::uint32_t source=4;source<=8;++source) {
        const auto id=source+8;vgui.Add(id);panels.nodes[id]=panels.nodes[source];
        panels.nodes[id].parent=11;panels.nodes[id].bounds.x+=240;
        panels.nodes[11].children.push_back(id);
    }
    add(17,11,"IconForSlot_Grenade","CIconPanel",{405,980,20,15},0x590654);
    add(18,11,"IconForSlot_Pills","CIconPanel",{430,980,20,15},0x590654);
    add(19,3,"IconForSlot_First_Aid","CIconPanel",{125,980,20,15},0x590654);
    const std::array<unsigned char,4> itemTint={230,231,232,255};
    for(const auto id:{17u,18u,19u})put(id,0x1d6,itemTint);
    std::array<std::array<unsigned char,0xac>,2> iconImages{};
    const unsigned char iconScale=1;const int zero=0,iconWidth=20,iconHeight=15;
    const std::array<float,4> iconUv={0,0,1,1};
    for(std::size_t i=0;i<iconImages.size();++i) {
        auto& bytes=iconImages[i];
        std::memcpy(bytes.data()+0x88,&textureId,sizeof(textureId));
        std::memcpy(bytes.data()+0x8c,iconUv.data(),sizeof(iconUv));
        std::memcpy(bytes.data()+0x9c,&zero,sizeof(zero));std::memcpy(bytes.data()+0xa0,&iconWidth,sizeof(iconWidth));
        std::memcpy(bytes.data()+0xa4,&zero,sizeof(zero));std::memcpy(bytes.data()+0xa8,&iconHeight,sizeof(iconHeight));
        void* icon=bytes.data();put(17+static_cast<std::uint32_t>(i),0x150,icon);put(17+static_cast<std::uint32_t>(i),0x1d4,iconScale);
    }
    snapshot.players[0].medical=12;
    Player equipped=snapshot.players[0];equipped.index=7;equipped.throwable=14;equipped.medical=0;equipped.pills=23;
    snapshot.players.push_back(equipped);
    HudSlot second=slot;second.panel=11;second.playerIndex=7;second.bounds=panels.nodes[11].bounds;second.visibleBounds=second.bounds;
    slots={slot,second};NativePanelCandidates(2,1920,1080,4000);g.gameTheme={};
    auto merged=GameHudThemeStep(snapshot,slots,1920,1080,4000,error);
    auto mergedItems=[](const GameHudTheme& value) {
        return value.available&&value.states[0].items[0].present&&value.states[0].items[1].present&&
            value.states[0].items[2].present&&value.states[0].items[0].rect.x==145&&
            value.states[0].items[1].rect.x==105&&value.states[0].items[2].rect.x==170;
    };
    Check(mergedItems(merged)&&std::any_of(merged.icons.begin(),merged.icons.end(),
          [](const ThemeIcon& icon){return icon.kind==0&&icon.item==14&&icon.texture.id==601;})&&
          std::any_of(merged.icons.begin(),merged.icons.end(),
          [](const ThemeIcon& icon){return icon.kind==2&&icon.item==23&&icon.texture.id==601;}),
          "later compatible occupied sources supply missing throwable and pills geometry together with their loaded icons");
    {
        // Real-server regression: two survivors are down, then one revives.
        // A different native representative and its screen clipping must not
        // discard the still-downed survivor's learned artwork or item atlas.
        snapshot.players[0].incapacitated=snapshot.players[1].incapacitated=true;
        panels.nodes[4].visible=panels.nodes[12].visible=false;
        surface.path="vgui/s_panel_namvet_incap";g.gameTheme={};
        const auto bothDown=GameHudThemeStep(snapshot,slots,1920,1080,10000,error);
        Check(bothDown.states[1].ready&&!bothDown.states[0].ready&&bothDown.icons.size()==2,
              "two downed native sources establish an independent down-state template and item atlas");
        snapshot.players[0].incapacitated=false;panels.nodes[4].visible=true;
        const auto revived=GameHudThemeStep(snapshot,slots,1920,1080,10010,error);
        Check(revived.states[0].ready&&revived.states[1].ready&&revived.icons.size()==2,
              "reviving one of two downed survivors does not remove the other survivor's down-state appearance or icons");
        panels.nodes[3].bounds.x-=23;
        for (const auto child:panels.nodes[3].children) panels.nodes[child].bounds.x-=23;
        slots[0].bounds=panels.nodes[3].bounds;slots[0].visibleBounds={0,920,197,100};
        const auto clipped=GameHudThemeStep(snapshot,slots,1920,1080,10510,error);
        const auto clippedAgain=GameHudThemeStep(snapshot,slots,1920,1080,11010,error);
        Check(clipped.states[0].width==197&&clipped.states[1].ready&&clippedAgain.states[1].ready&&
              clippedAgain.icons.size()==2,
              "repeated screen clipping changes the projected style without confirming a resource reload in another state");
        std::reverse(slots.begin(),slots.end());g_runtimeCounters={};
        const auto reordered=GameHudThemeStep(snapshot,slots,1920,1080,11020,error);
        Check(reordered.states[1].ready&&g_runtimeCounters.themeCaptures==0,
              "native source reordering keeps learned states without forcing another tree capture");
        const auto retiredCalls=panels.callsByPanel[3];
        auto& nativeChildren=panels.nodes[2].children;
        nativeChildren.erase(std::remove(nativeChildren.begin(),nativeChildren.end(),3u),nativeChildren.end());
        vgui.Retire(3);slots={second};
        const auto oneSource=GameHudThemeStep(snapshot,slots,1920,1080,11030,error);
        Check(oneSource.states[0].ready&&oneSource.states[1].ready&&oneSource.icons.size()==2&&
              panels.callsByPanel[3]==retiredCalls&&panels.retiredCalls==0,
              "retiring a sampling source preserves copied values without querying its old panel generation");
        void* badLabel=client.data()+0x65dfdd;put(13,0,badLabel);
        const auto failed=GameHudThemeStep(snapshot,slots,1920,1080,11530,error);
        Check(failed.states[1].ready&&failed.status.find("retained=0100")!=std::string::npos&&
              failed.status.find("failed=0100")!=std::string::npos&&g.gameTheme.nextCapture==11630,
              "one incomplete down-state capture retains its last complete value and retries after one hundred milliseconds");
        snapshot.players[1].pills=0;g_runtimeCounters={};
        Check(GameHudThemeStep(snapshot,slots,1920,1080,11531,error).states[1].ready&&
              g_runtimeCounters.themeCaptures==0&&g.gameTheme.nextCapture==11630,
              "item churn cannot bypass the hundred-millisecond budget while a failed capture is pending");
        snapshot.players[1].pills=23;
        Check(GameHudThemeStep(snapshot,slots,1920,1080,11630,error).states[1].ready,
              "a repeated transient state-capture failure remains within the bounded recovery window");
        const auto timedOut=GameHudThemeStep(snapshot,slots,1920,1080,13530,error);
        Check(timedOut.states[0].ready&&!timedOut.states[1].ready&&timedOut.icons.size()==2&&
              g.gameTheme.nextCapture==14030,
              "two seconds of failed captures expire only the failing state and restore the normal retry interval");
        void* goodLabel=client.data()+0x65dfdc;put(13,0,goodLabel);
        const auto recovered=GameHudThemeStep(snapshot,slots,1920,1080,14030,error);
        Check(recovered.states[0].ready&&recovered.states[1].ready&&
              recovered.status.find("failed=0000")!=std::string::npos,
              "the next successful capture restores the expired state without a full HUD reset");
        const float transparent=0,opaque=255;put(15,0x118,transparent);
        const auto faded=GameHudThemeStep(snapshot,slots,1920,1080,14530,error);
        Check(faded.states[1].images.size()==1&&faded.states[1].images[0].texture.tint.a>0&&
              faded.status.find("appearance=0100")!=std::string::npos,
              "a transient transparent downed background keeps its last visible appearance without forcing opaque new artwork");
        put(15,0x118,opaque);
        Check(GameHudThemeStep(snapshot,slots,1920,1080,14630,error).status.find("failed=0000")!=std::string::npos,
              "a recovered background ends the short appearance-retention window");
        put(15,0x118,transparent);GameHudThemeStep(snapshot,slots,1920,1080,15130,error);
        const auto persistentFade=GameHudThemeStep(snapshot,slots,1920,1080,17130,error);
        Check(persistentFade.states[1].ready&&persistentFade.states[1].images[0].texture.tint.a==0&&
              persistentFade.status.find("failed=0000")!=std::string::npos,
              "a persistently transparent custom background takes effect after two seconds rather than being overridden indefinitely");
        put(15,0x118,opaque);GameHudThemeStep(snapshot,slots,1920,1080,17630,error);
        ++panels.nodes[11].scheme;
        const auto schemeChanged=GameHudThemeStep(snapshot,slots,1920,1080,18130,error);
        Check(schemeChanged.states[1].ready&&!schemeChanged.states[0].ready,
              "a real scheme change immediately discards earlier state templates");
        snapshot.players[1].incapacitated=false;panels.nodes[12].visible=true;
        GameHudThemeStep(snapshot,slots,1920,1080,18140,error);
        snapshot.players[1].incapacitated=true;panels.nodes[12].visible=false;
        GameHudThemeStep(snapshot,slots,1920,1080,18240,error);
        ++panels.nodes[13].bounds.x;
        const auto geometryCandidate=GameHudThemeStep(snapshot,slots,1920,1080,18740,error);
        const auto geometryReload=GameHudThemeStep(snapshot,slots,1920,1080,19240,error);
        Check(geometryCandidate.states[0].ready&&geometryCandidate.states[1].ready&&
              !geometryReload.states[0].ready&&geometryReload.states[1].ready,
              "only a repeated uncut core geometry change in the same source and state confirms a same-scheme reload");
        --panels.nodes[13].bounds.x;
        snapshot.players[1].incapacitated=false;panels.nodes[12].visible=true;
        GameHudThemeStep(snapshot,slots,1920,1080,19245,error);
        Check(g.gameTheme.theme.states[0].ready&&g.gameTheme.theme.states[1].ready,
              "the source-generation turnover fixture contains both live and unobserved down templates");
        const auto oldGeneration=g.gameTheme.generationSources.front();
        vgui.Retire(11);vgui.Add(11);NativePanelCandidates(2,1920,1080,19246);
        const auto replacedSources=GameHudThemeStep(snapshot,slots,1920,1080,19246,error);
        Check(replacedSources.states[0].ready&&!replacedSources.states[1].ready&&
              g.gameTheme.generationSources.front().handle!=oldGeneration.handle&&panels.unresolvedCalls==0,
              "all native source generations changing under the same root and scheme starts a fresh theme generation");
        const auto secondCalls=panels.callsByPanel[11];vgui.Retire(11);nativeChildren.clear();slots.clear();
        Check(GameHudThemeStep(snapshot,slots,1920,1080,19250,error).available&&
              !GameHudThemeStep(snapshot,slots,1920,1080,21250,error).available&&panels.callsByPanel[11]==secondCalls,
              "all sampling sources disappearing has a bounded value-only grace period with no retired panel access");
        vgui.Add(3);vgui.Add(11);nativeChildren={3,11};
        panels.nodes[3].bounds.x+=23;
        for (const auto child:panels.nodes[3].children) panels.nodes[child].bounds.x+=23;
        panels.nodes[11].scheme=panels.nodes[3].scheme;
        panels.nodes[4].visible=panels.nodes[12].visible=true;
        for (auto& current:snapshot.players) current.incapacitated=false;
        surface.path="vgui/native_fixture_background";g.gameTheme={};slots={slot,second};
        NativePanelCandidates(2,1920,1080,22000);
    }
    std::reverse(slots.begin(),slots.end());g.gameTheme={};
    Check(mergedItems(GameHudThemeStep(snapshot,slots,1920,1080,4010,error)),
          "same-state inventory geometry is complete when the active and empty native source order is reversed");
    slots={slot,second};++panels.nodes[13].bounds.x;g.gameTheme={};
    const auto incompatible=GameHudThemeStep(snapshot,slots,1920,1080,4020,error);
    Check(incompatible.available&&!incompatible.states[0].items[0].present&&
          incompatible.states[0].items[1].present&&!incompatible.states[0].items[2].present,
          "inventory geometry is never merged across incompatible core label layouts");
    --panels.nodes[13].bounds.x;g.gameTheme={};GameHudThemeStep(snapshot,slots,1920,1080,4030,error);
    auto& sourceChildren=panels.nodes[11].children;
    sourceChildren.erase(std::remove(sourceChildren.begin(),sourceChildren.end(),17u),sourceChildren.end());
    sourceChildren.erase(std::remove(sourceChildren.begin(),sourceChildren.end(),18u),sourceChildren.end());
    snapshot.players[1].throwable=snapshot.players[1].pills=0;
    Check(mergedItems(GameHudThemeStep(snapshot,slots,1920,1080,4040,error)),
          "previous compatible inventory positions survive while every current native source has an empty slot");
    const std::array<const char*,8> characters={"gambler","producer","coach","mechanic","namvet","teenangst","biker","manager"};
    Check(std::all_of(characters.begin(),characters.end(),[](const char* character) {
        return CharacterIncapMaterial("vgui/s_panel_"+std::string(character)+"_incap");
    })&&!CharacterIncapMaterial("vgui/custom_neko_incap")&&!CharacterIncapMaterial("vgui/s_panel_namvet_incap_extra"),
          "only the eight exact base-game incap portrait materials receive character-aware background handling");
    Check(CharacterIncapMaterial("materials\\VGUI\\S_PANEL_NAMVET_INCAP.vmt"),
          "known incap material normalization accepts engine path separators, case and material extension");
    surface.path="vgui/s_panel_namvet_incap";
    for(auto& current:snapshot.players) {current.incapacitated=true;current.character=4;}
    panels.nodes[4].visible=panels.nodes[12].visible=false;g.gameTheme={};
    const auto downWithoutHead=GameHudThemeStep(snapshot,slots,1920,1080,4050,error);
    Check(downWithoutHead.available&&downWithoutHead.states[1].ready&&!downWithoutHead.states[1].portrait.present&&
          downWithoutHead.states[1].name.present&&downWithoutHead.states[1].number.present&&downWithoutHead.states[1].health.present,
          "native downed states remain supported when their resource intentionally has no portrait control");
    Check(downWithoutHead.states[1].images.size()==1&&downWithoutHead.states[1].images[0].characterIncap,
          "a downed native BackgroundImage using Bill's base-game incap material is classified without borrowing its identity");
    auto anotherCharacter=downWithoutHead.states[1];anotherCharacter.images[0].texture.id=602;
    anotherCharacter.images[0].texture.material="vgui/s_panel_teenangst_incap";
    const bool sameSkin=!ThemeSkinChanged(downWithoutHead.states[1],anotherCharacter);
    anotherCharacter.images[0].characterIncap=false;
    Check(sameSkin&&ThemeSkinChanged(downWithoutHead.states[1],anotherCharacter),
          "base incap character changes preserve shared state templates while a changed decoration role still invalidates the skin");
    surface.path="vgui/custom_neko_incap";g.gameTheme={};
    const auto customDown=GameHudThemeStep(snapshot,slots,1920,1080,4055,error);
    Check(customDown.states[1].ready&&customDown.states[1].images.size()==1&&!customDown.states[1].images[0].characterIncap,
          "custom incap decorations retain their original semantics instead of being guessed as a base survivor portrait");
    surface.path="vgui/s_panel_namvet_incap";
    for(auto& current:snapshot.players)current.alive=false;
    panels.nodes[4].visible=panels.nodes[12].visible=true;
    panels.nodes[6].visible=panels.nodes[14].visible=panels.nodes[8].visible=panels.nodes[16].visible=false;g.gameTheme={};
    const auto deadWithoutHealth=GameHudThemeStep(snapshot,slots,1920,1080,4060,error);
    Check(deadWithoutHealth.available&&deadWithoutHealth.states[3].ready&&deadWithoutHealth.states[3].portrait.present&&
          deadWithoutHealth.states[3].name.present&&!deadWithoutHealth.states[3].number.present&&!deadWithoutHealth.states[3].health.present&&
          !deadWithoutHealth.states[3].images[0].characterIncap,
          "native dead states keep their own portrait and name without requiring hidden live health controls");
    panels.nodes[4].visible=panels.nodes[12].visible=false;
    surface.path="vgui/native_fixture_dead";
    add(20,3,"Dead","ImagePanel",{20,920,200,100},0x65d98c);put(20,0x150,image);
    put(20,0x160,scaleImage);const std::array<unsigned char,4> deadTint={255,255,255,255};put(20,0x16c,deadTint);
    const auto deadOverlay=GameHudThemeStep(snapshot,slots,1920,1080,4570,error);
    Check(deadOverlay.states[3].ready&&!deadOverlay.states[3].portrait.present&&deadOverlay.states[3].name.present&&
          !deadOverlay.states[3].number.present&&!deadOverlay.states[3].health.present&&deadOverlay.states[3].images.size()==2,
          "a real loaded Dead overlay and name are sufficient for the native dead theme without portrait or health controls");
    surface.material=false;
    const auto missingDead=GameHudThemeStep(snapshot,slots,1920,1080,5080,error);
    Check(missingDead.states[3].ready&&missingDead.status.find("retained=0001")!=std::string::npos,
          "a briefly unresolved Dead overlay retains the prior complete value during its recovery window");
    Check(!GameHudThemeStep(snapshot,slots,1920,1080,7080,error).states[3].ready,
          "a persistently unresolved Dead overlay cannot keep a name-only state complete beyond the grace period");
    ResetPanelCaches();
    Check(!g.gameTheme.theme.available&&g.gameTheme.sources.empty(),"map and panel cache reset discard all native theme samples");
    g=State();g_runtimeCounters={};
}
void CheckModelPortraitAdapter() {
    using namespace neh;
    g=State();g_runtimeCounters={};
    std::vector<unsigned char> neko(0x20c840),entity(0x940),model(300),header(1024),node(64),sentinel(64);
    std::array<std::uint32_t,8> buckets{};
    const auto address=[](const void* value) {return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(value));};
    const auto put=[](std::vector<unsigned char>& bytes,std::size_t offset,const auto& value) {
        std::memcpy(bytes.data()+offset,&value,sizeof(value));
    };
    const auto path=[&](const std::string& value) {
        std::fill(model.begin(),model.end(),static_cast<unsigned char>(0));std::memcpy(model.data()+4,value.c_str(),value.size()+1);
    };
    constexpr std::uint32_t modelId=2169478528u;
    std::uint32_t currentId=modelId;
    void* wrapper=header.data();void* modelPointer=model.data();void* wrapperPointer=&wrapper;
    put(entity,0x60,modelPointer);put(entity,0x934,wrapperPointer);
    put(header,0,std::uint32_t(0x54534449));put(header,4,std::uint32_t(49));put(header,8,std::uint32_t(1234));
    put(header,76,int(header.size()));put(header,0x190,int(0x198));
    header[0x198+0x50+1]=1;put(header,0x198+0x50+8,modelId);
    path("models/l4n/s/My Pack..v2/character.mdl");
    ModelPortraitRegistryHeader registry{0x3f800000,address(sentinel.data()),1,address(buckets.data()),
        address(buckets.data())+sizeof(buckets),address(buckets.data())+sizeof(buckets),3,4};
    const auto saveRegistry=[&]() {put(neko,0x20c810,registry);};
    const auto setNode=[&](std::uint32_t key,int normal,int incap) {
        buckets.fill(address(sentinel.data()));
        const auto bucket=(ModelPortraitHash(key)&3u)*2;
        buckets[bucket]=buckets[bucket+1]=address(node.data());
        put(node,0,address(sentinel.data()));put(node,4,address(sentinel.data()));put(node,8,key);
        put(node,0x38,normal);put(node,0x3c,incap);saveRegistry();
    };
    setNode(modelId,1443,1914);
    g.neko=reinterpret_cast<HMODULE>(neko.data());g.client=reinterpret_cast<HMODULE>(1);g.surface=reinterpret_cast<void*>(2);
    int dummy=0;ClientClass klass{nullptr,nullptr,"CTerrorPlayer",&dummy,nullptr,1};
    PlayerFields fields;fields.character={0,4,0};fields.team={4,4,0};
    const auto readPass=[&]() {
        BeginModelPortraitPass();auto portrait=ReadModelPortrait(entity.data(),klass,fields);
        FinishModelPortraitPass();return portrait;
    };
    const auto first=readPass();
    Check(first.revision && first.modelId==modelId && first.normalId==1443 && first.incapId==1914,
          "applied model metadata selects its own cached normal/incap portraits with the HUD cvar left zero");
    Check(first.normalMaterial=="vgui/l4n_s/my pack..v2" && first.incapMaterial=="vgui/l4n_s/my pack..v2_incap",
          "portrait paths use the model pack segment and preserve legitimate spaces and dots");
    Check((ModelPortraitHash(modelId)&255u)==254 && g_runtimeCounters.modelPortraitInspections==1 &&
          g_runtimeCounters.modelPortraitLookups==1,"actual model key uses the verified four-byte FNV bucket lookup once");
    BeginModelPortraitPass();const auto again=ReadModelPortrait(entity.data(),klass,fields);
    auto botClass=klass;botClass.name="SurvivorBot";
    const auto shared=ReadModelPortrait(entity.data(),botClass,fields);FinishModelPortraitPass();
    Check(again.revision==first.revision && shared.revision==first.revision &&
          g_runtimeCounters.modelPortraitInspections==1 && g_runtimeCounters.modelPortraitLookups==1 &&
          g_runtimeCounters.modelPortraitCacheHits==2,
          "warm reads and another survivor using the same model share one inspection and registry lookup");
    put(node,0x38,int(1555));
    Check(readPass().normalId==1443,"a loaded model reuses copied cache IDs instead of rescanning the private registry each frame");
    header[0x198+0x50+1]=0;
    Check(readPass().revision==0,"returning to metadata without a Neko model clears the override immediately");
    const auto negatives=g_runtimeCounters.modelPortraitInspections;
    Check(readPass().revision==0 && g_runtimeCounters.modelPortraitInspections==negatives,
          "stable stock models retain a negative cache without repeated cold inspection");
    header[0x198+0x50+1]=1;
    const auto reapplied=readPass();
    Check(reapplied.revision>first.revision && reapplied.normalId==1555,
          "reapplying a model starts a fresh portrait lifetime and reads its current cache");
    put(header,8,std::uint32_t(5678));
    const auto changed=readPass();
    Check(changed.revision>reapplied.revision,"a changed loaded model checksum invalidates the previous association");
    path("models/l4n/s/another_pack/survivor.mdl");
    const auto changedPack=readPass();
    Check(changedPack.revision>changed.revision && changedPack.normalMaterial=="vgui/l4n_s/another_pack" &&
          changedPack.incapMaterial=="vgui/l4n_s/another_pack_incap",
          "model/header address and checksum reuse cannot retain another pack's portrait paths");
    g.surface=reinterpret_cast<void*>(3);
    const auto newSurface=readPass();
    Check(newSurface.revision>changedPack.revision,"surface lifetime changes invalidate model portrait IDs");
    ResetPanelCaches();
    const auto newMap=readPass();
    Check(newMap.revision>newSurface.revision,"map cache resets issue new renderer revision tokens");
    g.modelPortraits={};
    Check(readPass().revision>newMap.revision,"resetting private state cannot recycle a previous model revision");
    const auto inspections=g_runtimeCounters.modelPortraitInspections;
    auto unsupported=klass;unsupported.name="CInfected";
    BeginModelPortraitPass();
    Check(ReadModelPortrait(entity.data(),unsupported,fields).revision==0 &&
          ReadModelPortrait(entity.data(),klass,PlayerFields{}).revision==0 &&
          g_runtimeCounters.modelPortraitInspections==inspections,
          "unsupported client classes and missing survivor fields never inspect model metadata");
    FinishModelPortraitPass();
    put(header,0,std::uint32_t(0));
    Check(readPass().revision==0,"a non-IDST header cannot produce a portrait association");
    put(header,0,std::uint32_t(0x54534449));put(header,4,std::uint32_t(48));
    Check(readPass().revision==0,"an unsupported studio header version fails closed");
    put(header,4,std::uint32_t(49));put(header,0x190,int((std::numeric_limits<int>::max)()));
    Check(readPass().revision==0,"an overflowing metadata extension is rejected before pointer arithmetic");
    put(header,0x190,int(0x198));put(header,76,int(0x198+0x50+11));
    Check(readPass().revision==0,"all twelve metadata bytes must lie inside the current studio header");
    put(header,76,int(header.size()));
    void* invalid=reinterpret_cast<void*>(1);put(entity,0x934,invalid);
    Check(readPass().revision==0,"an unreadable studio wrapper cannot be dereferenced or cached");
    put(entity,0x934,wrapperPointer);put(entity,0x60,invalid);
    Check(readPass().revision==0,"an invalid model path fails safely even with plausible header metadata");
    Check(ReadStockSurvivorCharacter(entity.data(),klass)==-1,"stock character lookup rejects an unreadable model pointer");
    put(entity,0x60,modelPointer);
    put(entity,0x934,invalid);
    path("models/survivors/survivor_mechanic.mdl");
    Check(ReadStockSurvivorCharacter(entity.data(),klass)==3&&ReadStockSurvivorCharacter(entity.data(),botClass)==3,
          "a stock Ellis model supplies its own character independently of Neko studio metadata");
    path("models/survivors/survivor_biker.mdl");
    Check(ReadStockSurvivorCharacter(entity.data(),klass)==6&&ReadStockSurvivorCharacter(entity.data(),unsupported)==-1,
          "a stock Francis model is distinct from Ellis and only valid on survivor entity classes");
    path("models/l4n/s/pack/survivor_biker.mdl");
    Check(ReadStockSurvivorCharacter(entity.data(),klass)==-1,"a Neko model filename cannot reinterpret the survivor identity");
    put(entity,0x934,wrapperPointer);
    std::string normal,incap;
    for (const auto& bad:std::array<std::string,6>{{"models/l4n/s/../x.mdl","models/l4n/s/./x.mdl",
             "models/l4n/s/a:b/x.mdl","models/l4n/s//x.mdl","models/other/pack/x.mdl","models/l4n/s/pack/x.txt"}}) {
        path(bad);Check(!ModelPortraitMaterials(model.data(),normal,incap),"unsafe or unrelated model paths cannot request a portrait material");
    }
    path("models/l4n/s/\xed\x8c\xa9 v2.1/character.mdl");
    Check(ModelPortraitMaterials(model.data(),normal,incap) && normal=="vgui/l4n_s/\xed\x8c\xa9 v2.1",
          "UTF-8 model pack names remain available through the game search path");
    path("models/l4n/s/My Pack..v2/character.mdl");
    int normalId=0,incapId=0;
    registry.size=4097;saveRegistry();
    Check(!LookupModelPortrait(modelId,normalId,incapId),"implausibly large private registries are never traversed");
    registry.size=1;registry.mask=2;registry.bucketCount=3;saveRegistry();
    Check(!LookupModelPortrait(modelId,normalId,incapId),"non-power-of-two registry bucket metadata fails closed");
    registry.mask=3;registry.bucketCount=4;registry.end=registry.buckets+8;saveRegistry();
    Check(!LookupModelPortrait(modelId,normalId,incapId),"registry vector bounds must agree with the bucket mask");
    registry.end=registry.capacity;saveRegistry();setNode(modelId+1,999,998);
    g.modelPortraits={};
    const auto noEntry=readPass();
    Check(noEntry.revision && noEntry.normalId==0 && noEntry.incapId==0 && !noEntry.normalMaterial.empty(),
          "a registry miss retains only the validated pack material for one game-search-path load");
    setNode(modelId,0,-1);g.modelPortraits={};
    const auto missingImages=readPass();
    Check(missingImages.revision && missingImages.normalId==0 && missingImages.incapId==0,
          "unloaded and missing Neko cache IDs remain path requests rather than borrowed invalid textures");
    put(header,0x198+0x50+8,std::uint32_t(0));currentId=0;setNode(currentId,1777,1888);g.modelPortraits={};
    Check(readPass().normalId==1777,"a valid applied model with metadata ID zero is not arbitrarily excluded");
    put(header,0x198+0x50+8,modelId);setNode(modelId,1443,1914);
    auto* guard=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_NOACCESS);
    Check(guard!=nullptr,"unreadable registry node fixture allocated");
    const auto bucket=(ModelPortraitHash(modelId)&3u)*2;
    buckets[bucket]=buckets[bucket+1]=address(guard);
    Check(!LookupModelPortrait(modelId,normalId,incapId) && !normalId && !incapId,
          "unreadable private registry nodes cannot leak stale positive texture IDs");
    VirtualFree(guard,0,MEM_RELEASE);setNode(modelId+1,999,998);
    buckets[(ModelPortraitHash(modelId)&3u)*2]=address(sentinel.data())+4;
    buckets[(ModelPortraitHash(modelId)&3u)*2+1]=address(node.data());put(node,4,address(node.data()));
    Check(!LookupModelPortrait(modelId,normalId,incapId),"cyclic private bucket links terminate within a fixed bound");
    g=State();g_runtimeCounters={};AbortMemoryQueries();
}
void CheckMemoryQueryScope() {
    using namespace neh;
    SYSTEM_INFO info{};GetSystemInfo(&info);
    const auto pageSize=static_cast<std::size_t>(info.dwPageSize);
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,pageSize*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(memory!=nullptr,"memory-query fixture owns two committed pages");
    int expected=1234,value=0;
    std::memcpy(memory,&expected,sizeof(expected));
    g_runtimeCounters={};
    Check(Read(memory,value)&&Read(memory,value)&&g_runtimeCounters.virtualQueries>=2,
          "reads outside a scope retain independent VirtualQuery validation");
    g_runtimeCounters={};
    {
        MemoryQueryScope scope;
        Check(Read(memory,value)&&value==1234,"scope validates its first read");
        const auto first=g_runtimeCounters.virtualQueries;
        expected=5678;std::memcpy(memory,&expected,sizeof(expected));
        Check(Read(memory,value)&&value==5678&&g_runtimeCounters.virtualQueries==first,
              "scope reuses region metadata but always reads fresh value bytes");
        {
            MemoryQueryScope nested;
            Check(Read(memory,value)&&value==5678&&g_runtimeCounters.virtualQueries==first,
                  "nested scopes reuse the current pass without requerying a region");
        }
        Check(Read(memory,value)&&g_runtimeCounters.virtualQueries==first,
              "nested destruction preserves the outer pass cache");
        Check(!Executable(memory),"readable data is still rejected as an executable function");
    }
    const auto afterScope=g_runtimeCounters.virtualQueries;
    Check(Read(memory,value)&&g_runtimeCounters.virtualQueries>afterScope,
          "outer scope destruction discards region metadata");
    g_runtimeCounters={};
    {
        MemoryQueryScope scope;
        Check(Read(memory,value)&&g_runtimeCounters.virtualQueries==1,
              "a new scope starts with a fresh region query");
    }

    DWORD previous=0;
    {
        MemoryQueryScope scope;
        Check(Read(memory,value),"permission-change fixture warms readable metadata");
        Check(VirtualProtect(memory,pageSize,PAGE_NOACCESS,&previous)!=FALSE,
              "fixture changes its own page protection to no access");
        Check(!Read(memory,value),"stale readable metadata cannot bypass SEH after protection changes");
        const auto before=g_runtimeCounters.virtualQueries;
        MEMORY_BASIC_INFORMATION current{};
        Check(QueryMemoryRegion(memory,current)&&current.Protect==PAGE_NOACCESS&&
              g_runtimeCounters.virtualQueries>before,
              "failed guarded read clears metadata before the next region query");
        DWORD ignored=0;
        Check(VirtualProtect(memory,pageSize,PAGE_READWRITE,&ignored)!=FALSE,
              "fixture restores writable protection");
        InvalidateMemoryQueries();
        Check(Read(memory,value)&&value==5678,
              "explicit invalidation observes a known permission restoration within the same scope");
    }
    {
        MemoryQueryScope scope;Check(Read(memory,value),"fresh scope primes metadata for boundary test");
    }
    Check(VirtualProtect(memory,pageSize,PAGE_NOACCESS,&previous)!=FALSE,
          "fixture changes protection between independent scopes");
    g_runtimeCounters={};
    {
        MemoryQueryScope scope;
        Check(!Read(memory,value)&&g_runtimeCounters.virtualQueries>0,
              "next scope rechecks a previously readable region after VirtualProtect");
    }
    DWORD ignored=0;VirtualProtect(memory,pageSize,PAGE_READWRITE,&ignored);

    {
        MemoryQueryScope scope;
        void* firstTable[1]={reinterpret_cast<void*>(&MemoryValueA)};
        void* secondTable[1]={reinterpret_cast<void*>(&MemoryValueB)};
        struct Object {void** table;} object{firstTable};
        Check(Call(&object,0,value)&&value==11,"call fixture invokes a validated executable method");
        object.table=secondTable;
        Check(Call(&object,0,value)&&value==22,"cached region metadata does not freeze an object vtable pointer");
        secondTable[0]=reinterpret_cast<void*>(&MemoryValueA);
        Check(Call(&object,0,value)&&value==11,"method slots are reread after their function pointer changes");
        secondTable[0]=memory;
        Check(!Call(&object,0,value),"non-executable method pointer remains rejected within a warm scope");
        secondTable[0]=nullptr;
        Check(!Call(&object,0,value),"null replacement method remains rejected");
        secondTable[0]=reinterpret_cast<void*>(&MemoryFault);
        Read(memory,value);const auto before=g_runtimeCounters.virtualQueries;
        Check(!Call(&object,0,value),"native call faults remain guarded inside a query scope");
        const auto afterFault=g_runtimeCounters.virtualQueries;
        Check(Read(memory,value)&&g_runtimeCounters.virtualQueries>afterFault&&afterFault>=before,
              "guarded call fault discards metadata before a subsequent read");
        object.table=nullptr;
        Check(!Call(&object,0,value),"fresh null object vtable cannot reuse an earlier valid method");
    }
    memory[pageSize-2]='A';memory[pageSize-1]='B';
    Check(VirtualProtect(memory+pageSize,pageSize,PAGE_NOACCESS,&previous)!=FALSE,
          "fixture protects the second page for cross-region validation");
    {
        MemoryQueryScope scope;
        Check(!Readable(memory+pageSize-2,sizeof(int))&&!Read(memory+pageSize-2,value),
              "a range crossing into an inaccessible page is rejected");
        char text[8]{};Read(memory,value);
        Check(!CopyString(reinterpret_cast<char*>(memory+pageSize-2),text,sizeof(text))&&text[0]==0,
              "string reads crossing an inaccessible boundary remain guarded");
        const auto afterFault=g_runtimeCounters.virtualQueries;
        Check(Read(memory,value)&&g_runtimeCounters.virtualQueries>afterFault,
              "guarded string fault invalidates prior region metadata");
    }
    VirtualProtect(memory+pageSize,pageSize,PAGE_READWRITE,&ignored);
    Check(VirtualProtect(memory,pageSize,PAGE_READWRITE|PAGE_GUARD,&previous)!=FALSE,
          "fixture applies a guard page");
    {
        MemoryQueryScope scope;
        Check(!Read(memory,value)&&!Executable(memory),"guard pages are refused without touching their contents");
    }
    VirtualProtect(memory,pageSize,PAGE_READWRITE,&ignored);
    Check(!Readable(nullptr,4)&&!Readable(memory,0)&&
          !Readable(reinterpret_cast<void*>(~std::uintptr_t(0)-1),16),
          "null, empty, and overflowing memory ranges remain rejected");

    auto* abandoned=new MemoryQueryScope;
    Check(Read(memory,value),"aborted outer-scope fixture warms its region");
    AbortMemoryQueries();g_runtimeCounters={};
    Check(Read(memory,value)&&Read(memory,value)&&g_runtimeCounters.virtualQueries>=2,
          "abort clears scope depth as well as metadata");
    {
        MemoryQueryScope fresh;
        Read(memory,value);const auto before=g_runtimeCounters.virtualQueries;
        delete abandoned;abandoned=nullptr;
        Check(Read(memory,value)&&g_runtimeCounters.virtualQueries==before,
              "old-generation destructor cannot end a new pass after abort");
    }
    {
        MemoryQueryScope outer;Read(memory,value);
        const auto before=g_runtimeCounters.virtualQueries;
        {
            MemoryQueryScope forced(true);
            Check(Read(memory,value)&&g_runtimeCounters.virtualQueries>before,
                  "forced runtime entry starts a fresh pass despite an outstanding older scope");
        }
    }

    constexpr std::size_t regions=130;
    auto* many=static_cast<unsigned char*>(VirtualAlloc(nullptr,pageSize*regions,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(many!=nullptr,"cache-capacity fixture owns more than 128 regions");
    bool protectionOk=true;
    for(std::size_t i=0;i<regions;++i) {
        const int marker=static_cast<int>(i)+7000;
        std::memcpy(many+i*pageSize,&marker,sizeof(marker));
        if(i%2)protectionOk=protectionOk&&VirtualProtect(many+i*pageSize,pageSize,PAGE_READONLY,&ignored)!=FALSE;
    }
    Check(protectionOk,"alternating protections create distinct metadata regions");
    {
        MemoryQueryScope scope;bool all=true;
        for(std::size_t i=0;i<regions;++i)all=all&&Read(many+i*pageSize,value)&&value==static_cast<int>(i)+7000;
        Check(all&&Read(many,value)&&value==7000,
              "cache capacity exhaustion preserves correct reads instead of overrunning fixed storage");
    }
    Check(VirtualFree(many,0,MEM_RELEASE)!=FALSE&&VirtualFree(memory,0,MEM_RELEASE)!=FALSE,
          "memory-query fixtures release their own allocations");
    AbortMemoryQueries();g_runtimeCounters={};
}
} // namespace
int main() {
    using namespace neh;
    RecvProp numeric[2]{};
    numeric[0].name="000"; numeric[0].offset=0;
    numeric[1].name="001"; numeric[1].offset=1;
    RecvTable boolArray{numeric,2,nullptr,"bools"};
    RecvProp props[2]{};
    props[0].name="m_bConnected"; props[0].offset=32; props[0].dataTable=&boolArray;
    props[1].name="m_iTeam"; props[1].offset=160; props[1].stride=4; props[1].elements=33;
    RecvTable base{props,2,nullptr,"DT_PlayerResource"};
    RecvProp derivedProp{};
    derivedProp.name="baseclass"; derivedProp.offset=64; derivedProp.dataTable=&base;
    RecvTable derived{&derivedProp,1,nullptr,"DT_TerrorPlayerResource"};
    const auto direct=FindField(&base,"m_iTeam");
    Check(direct.offset==160&&direct.stride==4,"direct array metadata");
    const auto inherited=FindField(&derived,"m_iTeam");
    Check(inherited.offset==224,"baseclass offset accumulation");
    const auto small=FindField(&base,"m_bConnected");
    Check(small.offset==32&&small.stride==1&&small.elements==2,"bool stride from numeric children");
    Check(!FindField(&derived,"missing").Valid(),"missing properties remain invalid");
    RecvTable bad{props,5000,nullptr,"invalid"};
    Check(!FindField(&bad,"m_iTeam").Valid(),"malformed property count rejected");
    RecvProp cycle{}; cycle.name="baseclass"; cycle.dataTable=&derived;
    RecvTable cyclic{&cycle,1,nullptr,"cyclic"};
    cycle.dataTable=&cyclic;
    Check(!FindField(&cyclic,"missing").Valid(),"cyclic table traversal bounded");
    unsigned char data[512]{};
    int expected=2;
    std::memcpy(data+160+3*4,&expected,sizeof(expected));
    int team=0;
    Check(Array(data,direct,3,team)&&team==2,"array index uses discovered stride");
    Check(!Array(data,direct,33,team),"array element bounds enforced");
    Check(!Array(data,Field{160,1,33},3,team),"incompatible stride rejected");
    int unreadable=0;
    Check(!Read(reinterpret_cast<void*>(1),unreadable),"invalid address rejected");
    char output[8]{};
    Check(CopyString("Nick",output,sizeof(output))&&std::string(output)=="Nick","bounded string copy");
    Check(!CopyString("A name too long",output,sizeof(output)),"truncated source names signaled");
    void* nullVtable=nullptr;
    int result=0;
    Check(!Call(&nullVtable,0,result),"invalid virtual call rejected");
    CheckSurvivorCharacterContext();
    CheckResourceAdapter();
    CheckSurvivorHealthAdapter();
    CheckHudBoundsAdapter();
    CheckPanelDiscoveryCache();
    CheckNativeHudPaintState();
    CheckIncrementalSelfDiscovery();
    CheckSafePanelLifetimes();
    CheckGameHudItemIconLookup();
    CheckGameHudThemeCapture();
    CheckModelPortraitAdapter();
    CheckMemoryQueryScope();
    std::cout<<"Runtime adapter: "<<assertions<<" checks passed\n";
    return 0;
}
