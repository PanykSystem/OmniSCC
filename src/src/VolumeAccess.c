// Exclusive ownership of the logical flash device, independent of FatFs.
#include "VolumeAccess.h"
#include "FSAdapter.h"
#include "FlashAdapter.h"
#include "ff.h"
#include "diskio.h"
static VolumeOwner owner=VOLUME_MENU;
static bool ready;
VolumeOwner __not_in_flash_func(volume_owner)(void) { return owner; }
void volume_set_ready(bool value) { ready=value; }
bool volume_is_ready(void) { return ready; }
bool volume_handover(VolumeOwner expected,VolumeOwner next) {
 if(owner!=expected || (next!=VOLUME_MENU && next!=VOLUME_NEXTOR && next!=VOLUME_USB && next!=VOLUME_ROM) || !ready) return false;
 if(next==owner) return true;
 if(owner==VOLUME_ROM) return false;
 if(!volume_sync(owner)) return false;
 if(owner==VOLUME_MENU && !_fs_unmount()) return false;
 if(next==VOLUME_MENU) {
  owner=VOLUME_MENU;
  if(!_fs_mount()) { owner=expected;return false; }
 } else owner=next;
 return true;
}
uint32_t volume_read(VolumeOwner requester,uint8_t* dst,uint32_t lba,uint32_t count) {
 if(!ready || requester!=owner) return RES_ERROR;
 if(!dst || !count || lba>=FAT_NUM_SECTORS || count>FAT_NUM_SECTORS-lba) return RES_PARERR;
 for(uint32_t i=0;i<count;++i) _flash_fs_read((uint16_t)(lba+i),dst+i*512);
 return RES_OK;
}
VolumeWriteResult volume_write_sector(VolumeOwner requester,const uint8_t* src,uint32_t lba) {
 if(!ready || owner==VOLUME_ROM || requester!=owner || !src || lba>=FAT_NUM_SECTORS) return VWRITE_INVALID;
 if(!_flash_fs_write((uint16_t)lba,src)) return VWRITE_ERROR;
 if(!_flash_fs_verify((uint16_t)lba,src)) return VWRITE_VERIFY;
 return VWRITE_OK;
}
bool volume_sync(VolumeOwner requester) {
 return ready && requester==owner && _flash_sync_checked();
}
uint32_t volume_write(VolumeOwner requester,const uint8_t* src,uint32_t lba,uint32_t count) {
 if(!ready || owner==VOLUME_ROM || requester!=owner) return RES_ERROR;
 if(!src || !count || lba>=FAT_NUM_SECTORS || count>FAT_NUM_SECTORS-lba) return RES_PARERR;
 for(uint32_t i=0;i<count;++i) if(volume_write_sector(requester,src+i*512,lba+i)!=VWRITE_OK) return RES_ERROR;
 return RES_OK;
}
