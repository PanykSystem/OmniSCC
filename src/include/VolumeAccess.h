#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef enum { VOLUME_MENU, VOLUME_NEXTOR, VOLUME_USB, VOLUME_ROM } VolumeOwner;
typedef enum { VWRITE_OK, VWRITE_ERROR, VWRITE_VERIFY, VWRITE_INVALID } VolumeWriteResult;
VolumeWriteResult volume_write_sector(VolumeOwner owner,const uint8_t* src,uint32_t lba);
bool volume_sync(VolumeOwner owner);
bool volume_handover(VolumeOwner expected, VolumeOwner next);
VolumeOwner volume_owner(void);
void volume_set_ready(bool ready);
bool volume_is_ready(void);
uint32_t volume_read(VolumeOwner owner,uint8_t* dst,uint32_t lba,uint32_t count);
uint32_t volume_write(VolumeOwner owner,const uint8_t* src,uint32_t lba,uint32_t count);
#ifdef __cplusplus
}
#endif
