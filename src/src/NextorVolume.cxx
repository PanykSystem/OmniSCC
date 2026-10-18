// Read-only validation of the same logical device exposed over USB.
#include "NextorVolume.hxx"
#include <string.h>
namespace {
uint16_t u16(const uint8_t* p) { return p[0] | (uint16_t(p[1]) << 8); }
uint32_t u32(const uint8_t* p) { return u16(p) | (uint32_t(u16(p+2)) << 16); }
bool signature(const uint8_t* b) { return b[510]==0x55 && b[511]==0xAA; }
bool extended(uint8_t t) { return t==5 || t==0x0F || t==0x85; }
bool supported(uint8_t t) { return t==1 || t==4 || t==6 || t==0x0E; }
NextorVolumeResult bpb(const uint8_t* b, uint32_t limit) {
 if(!signature(b)) return NextorVolumeResult::Invalid;
 if(!memcmp(b+3,"EXFAT   ",8)) return NextorVolumeResult::Unsupported;
 if(u16(b+11)!=512) return NextorVolumeResult::Invalid;
 const uint32_t spc=b[13], reserved=u16(b+14), fats=b[16], root=u16(b+17), fat=u16(b+22);
 if(!fat && !root && u32(b+36)) return NextorVolumeResult::Unsupported;
 const uint32_t total=u16(b+19) ? u16(b+19) : u32(b+32);
 if(!spc || (spc&(spc-1)) || spc>128 || !reserved || !fats || fats>2 || !root || !fat || !total || total>limit) return NextorVolumeResult::Invalid;
 const uint32_t overhead=reserved+fats*fat+(root*32+511)/512;
 if(overhead>=total) return NextorVolumeResult::Invalid;
 const uint32_t clusters=(total-overhead)/spc;
 if(!clusters) return NextorVolumeResult::Invalid;
 if(clusters>=65525) return NextorVolumeResult::Unsupported;
 const uint32_t needed=clusters<4085 ? ((clusters+2)*3+1)/2 : (clusters+2)*2;
 return needed<=fat*512 ? NextorVolumeResult::Compatible : NextorVolumeResult::Invalid;
}
}
NextorVolumeResult ValidateNextorVolume(uint32_t capacity,
 bool (*readSector)(uint32_t,uint8_t*,void*),void* ctx) {
 if(!capacity || !readSector) return NextorVolumeResult::Invalid;
 uint8_t b[512];
 if(!readSector(0,b,ctx)) return NextorVolumeResult::ReadError;
 if(!signature(b)) return NextorVolumeResult::Invalid;
 if(!memcmp(b+3,"EXFAT   ",8)) return NextorVolumeResult::Unsupported;
 NextorVolumeResult candidate=NextorVolumeResult::Invalid;
 if(u16(b+11)==512) {
  candidate=bpb(b,capacity);
  if(candidate==NextorVolumeResult::Compatible) return candidate;
 }
 // MBR bootstrap bytes may resemble an invalid/unsupported BPB. Examine
 // its partition table before rejecting such a candidate.
 // Keep the four entries before reusing the sector scratch for VBR/EBR.
 uint8_t entries[64]; memcpy(entries,b+446,64);
 bool found=false;
 for(unsigned i=0;i<4;++i) {
  const uint8_t* e=entries+i*16; uint8_t type=e[4];
  if(!type) continue;
  uint32_t start=u32(e+8), size=u32(e+12);
  if(!start || !size || start>=capacity || size>capacity-start) return NextorVolumeResult::Invalid;
  if(supported(type)) {
   if(!readSector(start,b,ctx)) return NextorVolumeResult::ReadError;
   auto r=bpb(b,size); if(r!=NextorVolumeResult::Compatible) return r; found=true;
  } else if(extended(type)) {
   uint32_t current=start; bool ended=false;
   for(unsigned hop=0;hop<64;++hop) {
    if(current<start || current-start>=size) return NextorVolumeResult::Invalid;
    if(!readSector(current,b,ctx)) return NextorVolumeResult::ReadError;
    if(!signature(b)) return NextorVolumeResult::Invalid;
    uint8_t first[16],next[16]; memcpy(first,b+446,16);memcpy(next,b+462,16);
    const uint32_t rel=u32(first+8), n=u32(first+12);
    if(!supported(first[4])) return NextorVolumeResult::Unsupported;
    if(!rel || !n || rel>=size-(current-start) || n>size-(current-start)-rel) return NextorVolumeResult::Invalid;
    if(!readSector(current+rel,b,ctx)) return NextorVolumeResult::ReadError;
    auto r=bpb(b,n); if(r!=NextorVolumeResult::Compatible) return r; found=true;
    if(!next[4]) { ended=true;break; }
    const uint32_t offset=u32(next+8), nextSize=u32(next+12);
    if(!extended(next[4]) || !offset || offset>=size || !nextSize || nextSize>size-offset) return NextorVolumeResult::Invalid;
    current=start+offset;
   }
   if(!ended) return NextorVolumeResult::Invalid;
  } else return NextorVolumeResult::Unsupported;
 }
 return found ? NextorVolumeResult::Compatible : candidate;
}
