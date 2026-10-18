#pragma once
#include <stdint.h>
#include <string.h>
#include <array>

// Nextor stores FAT length in one byte. Present only the used FAT16 prefix,
// moving the surplus into the virtual reserved area without moving root/data.
// Physical USB boot-sector geometry and cluster numbering remain unchanged.
class NextorVolumeView {
public:
 void Reset() { _ready=false;_count=0; }
 template<class Reader> bool Init(Reader read,uint8_t* scratch,uint32_t capacity) {
  if(_ready)return true;
  _count=0;
  if(!read(0,scratch))return false;
  if(Word(scratch+11)==512 && scratch[13] && !(scratch[13]&(scratch[13]-1))) {
   Add(0,capacity,scratch);_ready=true;return true;
  }
  if(scratch[510]==0x55 && scratch[511]==0xAA) {
   uint32_t starts[4]={},sizes[4]={};
   for(unsigned i=0;i<4;++i) {
    const auto* p=scratch+446+16*i;
    if(p[4]!=1 && p[4]!=4 && p[4]!=6 && p[4]!=14)continue;
    const auto start=Dword(p+8),size=Dword(p+12);
    if(start && start<capacity && size && size<=capacity-start){starts[i]=start;sizes[i]=size;}
   }
   for(unsigned i=0;i<4;++i)if(sizes[i]) {
    if(!read(starts[i],scratch)){_count=0;return false;}
    Add(starts[i],sizes[i],scratch);
   }
  }
  _ready=true;return true;
 }
 uint32_t Physical(uint32_t lba) const {
  for(unsigned i=0;i<_count;++i) {
   const auto& v=_views[i];const auto first=v.boot+Reserved(v);
   if(lba>=first && lba-first<v.shortFat)return v.boot+v.reserved+lba-first;
  }
  return lba;
 }
 void ReadBoot(uint32_t lba,uint8_t* p) const {
  for(unsigned i=0;i<_count;++i)if(lba==_views[i].boot) {
   PutWord(p+14,Reserved(_views[i]));PutWord(p+22,_views[i].shortFat);return;
  }
 }
 bool PrepareWrite(uint32_t lba,uint8_t* p) const {
  // Partition geometry must stay fixed while the derived view is active.
  if(lba==0 && _count && _views[0].boot!=0)return false;
  for(unsigned i=0;i<_count;++i) {
   const auto& v=_views[i];
   if(lba==v.boot) {
    if(p[510]!=0x55 || p[511]!=0xAA)return false;
    for(unsigned j=11;j<36;++j) {
     uint8_t expected=v.geometry[j-11];
     if(j==14)expected=Reserved(v);
     else if(j==15)expected=Reserved(v)>>8;
     else if(j==22)expected=v.shortFat;
     else if(j==23)expected=v.shortFat>>8;
     if(p[j]!=expected)return false;
    }
    PutWord(p+14,v.reserved);PutWord(p+22,v.fat);return true;
   }
   // The apparent reserved gap aliases the physical FAT: do not let raw
   // Nextor writes overwrite it through a second logical address.
   if(lba>v.boot && lba-v.boot<Reserved(v))return false;
  }
  return true;
 }
 template<class Archive> void serialize(Archive& ar, unsigned) {
  ar.serialize("ready",_ready,"count",_count);
  for(unsigned i=0;i<4;++i) {
   ar.serialize("boot",_views[i].boot,"reserved",_views[i].reserved,
                "fat",_views[i].fat,"shortFat",_views[i].shortFat);
   std::array<uint8_t,25> geometry{};
   memcpy(geometry.data(),_views[i].geometry,25);
   ar.serialize("geometry",geometry);
   if constexpr(Archive::IS_LOADER)memcpy(_views[i].geometry,geometry.data(),25);
  }
 }
private:
 struct View {uint32_t boot;uint16_t reserved,fat,shortFat;uint8_t geometry[25];};
 static uint16_t Word(const uint8_t* p){return uint16_t(p[0])|uint16_t(p[1])<<8;}
 static uint32_t Dword(const uint8_t* p){return uint32_t(Word(p))|uint32_t(Word(p+2))<<16;}
 static void PutWord(uint8_t* p,uint16_t n){p[0]=n;p[1]=n>>8;}
 static uint16_t Reserved(const View& v){return v.reserved+v.fat-v.shortFat;}
 void Add(uint32_t start,uint32_t limit,const uint8_t* p) {
  if(p[510]!=0x55 || p[511]!=0xAA || Word(p+11)!=512 || p[16]!=1)return;
  const uint32_t spc=p[13],reserved=Word(p+14),fat=Word(p+22),root=Word(p+17);
  if(!spc || (spc&(spc-1)) || !reserved || !root || fat<=256)return;
  const uint32_t total=Word(p+19)?Word(p+19):Dword(p+32);
  const uint32_t overhead=reserved+fat+(root+15)/16;
  if(total>limit || total<=overhead)return;
  const auto clusters=(total-overhead)/spc;
  if(clusters<4085 || clusters>=65525)return;
  const auto needed=((clusters+2)*2+511)/512;
  if(!needed || needed>256 || needed>fat || reserved+fat-needed>65535)return;
  auto& v=_views[_count++];v.boot=start;v.reserved=reserved;v.fat=fat;v.shortFat=needed;
  memcpy(v.geometry,p+11,sizeof v.geometry);
 }
 View _views[4]{};
 uint8_t _count=0;
 bool _ready=false;
};
