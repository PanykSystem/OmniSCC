#include "NextorMailbox.hxx"
#include "NextorProtocol.hxx"
#include "VolumeAccess.h"
extern "C" {
#include "FlashAdapter.h"
#include "ff.h"
#include "diskio.h"
}
#include <string.h>
void CNextorMailbox::Reset() {
 _view.Reset();
 memset(_bank+NP_LBA,0,16);
 for(unsigned i=0;i<4;++i) _bank[NP_CAPACITY+i]=FAT_NUM_SECTORS>>(8*i);
 _bank[NP_SECTOR_SIZE+1]=2;
 _bank[NP_VERSION]=1;
 _bank[NP_READY]=NEXTOR_READY;
}
void CNextorMailbox::Execute(uint8_t command) {
 _bank[NP_READY]=0;_bank[NP_DONE]=0;
 uint8_t status=NS_OK;
 if(volume_owner()!=VOLUME_NEXTOR) status=NS_NOT_READY;
 else if(command!=NC_INFO && command!=NC_READ && command!=NC_WRITE) status=NS_BAD_COMMAND;
 else if(!_view.Init([](uint32_t l,uint8_t* p){return volume_read(VOLUME_NEXTOR,p,l,1)==RES_OK;},_bank+NP_SCRATCH,FAT_NUM_SECTORS)) status=NS_NOT_READY;
 else if(command!=NC_INFO) {
  uint32_t lba=0;for(unsigned i=0;i<4;++i)lba|=uint32_t(_bank[NP_LBA+i])<<(8*i);
  const uint8_t count=_bank[NP_COUNT];
  if(!count || count>NEXTOR_MAX_SECTORS) status=NS_BAD_PARAM;
  else if(lba>=FAT_NUM_SECTORS || count>FAT_NUM_SECTORS-lba) status=NS_RANGE;
  else {
   for(uint8_t i=0;i<count;++i) {
    uint8_t* p=_bank+NP_BUFFER+unsigned(i)*512;
    if(command==NC_READ) {
     if(volume_read(VOLUME_NEXTOR,p,_view.Physical(lba+i),1)!=RES_OK) { status=NS_NOT_READY;break; }
     _view.ReadBoot(lba+i,p);
    } else {
     if(!_view.PrepareWrite(lba+i,p)){status=NS_WRITE_ERROR;break;}
     const auto result=volume_write_sector(VOLUME_NEXTOR,p,_view.Physical(lba+i));
     if(result!=VWRITE_OK) {status=result==VWRITE_VERIFY ? NS_VERIFY_ERROR : NS_WRITE_ERROR;break;}
    }
    ++_bank[NP_DONE];
   }
   if(command==NC_WRITE && !volume_sync(VOLUME_NEXTOR)) { status=NS_WRITE_ERROR;_bank[NP_DONE]=0; }
  }
 }
 _bank[NP_STATUS]=status;
 _bank[NP_READY]=NEXTOR_READY;
}
