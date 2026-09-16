/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 */

#include "tusb.h"
#include "FlashAdapter.h"
#include "FSAdapter.h"

extern const struct lfs_config lfs_pico_flash_config;
static bool ejected = false;
static bool is_initialized = false;

// Invoked when received SCSI_CMD_INQUIRY
// Application fill vendor id, product id and revision with string up to 8, 16, 4 characters respectively
void tud_msc_inquiry_cb(uint8_t lun, uint8_t vendor_id[8], uint8_t product_id[16], uint8_t product_rev[4])
{
  (void) lun;

  const char vid[] = "OmniSCC";
  const char pid[] = "Mass Storage";
  const char rev[] = "1.0";

  memcpy(vendor_id  , vid, strlen(vid));
  memcpy(product_id , pid, strlen(pid));
  memcpy(product_rev, rev, strlen(rev));
}

bool tud_msc_test_unit_ready_cb(uint8_t lun) {
    (void)lun;
    return true;
}

void tud_msc_capacity_cb(uint8_t lun, uint32_t* block_count, uint16_t* block_size) {
    (void)lun;
    
    //mimic_fat_init(&lfs_pico_flash_config);
    *block_count = FAT_NUM_SECTORS;
    *block_size  = FAT_SECTOR_SIZE;
}

bool tud_msc_start_stop_cb(uint8_t lun, uint8_t power_condition, bool start, bool load_eject) {
    (void)lun;
    (void)power_condition;

    if (load_eject) {
        if (start) {
            // load disk storage
        } else {
            // unload disk storage
            _fs_sync();
        }
    }
    return true;
}

// Sector scratch, lent out of car_cache by OmniSCC.cxx: USB mass storage and
// the cartridge never run together (main() picks one and msx_cart_main()
// never returns), and a 512-byte array of our own does not fit in .bss.
extern uint8_t* const msc_sector_scratch;

int32_t tud_msc_read10_cb(uint8_t lun, uint32_t lba, uint32_t offset, void* buffer, uint32_t bufsize) {
    if (lba >= FAT_NUM_SECTORS) return -1;
    if (offset + bufsize > FAT_SECTOR_SIZE) return -1;

    if (_fs_read(msc_sector_scratch, lba, 1) != 0) return -1;

    memcpy(buffer, msc_sector_scratch + offset, bufsize);
    return (int32_t) bufsize;
}

bool tud_msc_is_writable_cb (uint8_t lun) {
    (void) lun;
    return true;
}

// Callback invoked when received WRITE10 command.
// Process data in buffer to disk's storage and return number of written bytes
static alarm_id_t alarm_id = -1;

static int64_t sync_callback(alarm_id_t id, void *user_data)
{
  (void) id;
  (void) user_data;
  _fs_sync();
  alarm_id = -1;
  return 0;
}


int32_t tud_msc_write10_cb(uint8_t lun, uint32_t lba, uint32_t offset, uint8_t* buffer, uint32_t bufsize) {
    (void)offset;

    if (lba >= FAT_NUM_SECTORS) return -1;
    if (offset + bufsize > FAT_SECTOR_SIZE) return -1;

    // read-modify-write for partial transfers
    if (offset != 0 || bufsize != FAT_SECTOR_SIZE) {
        if (_fs_read(msc_sector_scratch, lba, 1) != 0) return -1;
        memcpy(msc_sector_scratch + offset, buffer, bufsize);
        if (_fs_write(msc_sector_scratch, lba, 1) != 0) return -1;
    } else {
        if (_fs_write(buffer, lba, 1) != 0) return -1;
    }

    // deferred sync
    if (alarm_id >= 0) cancel_alarm(alarm_id);
    alarm_id = add_alarm_in_ms(250, sync_callback, NULL, false);

    return (int32_t) bufsize;
}

int32_t tud_msc_scsi_cb(uint8_t lun, uint8_t const scsi_cmd[16], void *buffer, uint16_t bufsize) {
    void const *response = NULL;
    int32_t resplen = 0;

    // most scsi handled is input
    bool in_xfer = true;

    switch (scsi_cmd[0]) {
    case 0x35: // SYNCHRONIZE_CACHE_10
      _fs_sync();
      return 0;

    default:
        // Set Sense = Invalid Command Operation
        tud_msc_set_sense(lun, SCSI_SENSE_ILLEGAL_REQUEST, 0x20, 0x00);
        // negative means error -> tinyusb could stall and/or response with failed status
        resplen = -1;
        break;
    }

    // return resplen must not larger than bufsize
    if (resplen > bufsize) resplen = bufsize;

    if (response && (resplen > 0)) {
        if(in_xfer) {
            memcpy(buffer, response, (size_t) resplen);
        } else {
            ; // SCSI output
        }
    }
    return (int32_t)resplen;
}
