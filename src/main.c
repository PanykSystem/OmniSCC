/**
 * MSX RP2040 pico cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 *
 * Alexey Spirkov 2025 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 * Pick the board header in include/boards/ to match the flash on the pico
 * clone (PICO_FLASH_SPI_CLKDIV) and the presence of level shifters:
 * -DPICO_BOARD=omniscc or -DPICO_BOARD=omniscc_plus
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/time.h"
#include "tusb.h"

#include "FSAdapter.h"
#include "VolumeAccess.h"
#include "Board.h"


int main(void)
{
    const bool filesystem_ready = _fs_initialize();

    // Only start cartridge mode with a mounted FAT. Otherwise keep USB
    // available even on MSX power, so the volume can be repaired from a PC.
    if (filesystem_ready)
    {
        // Sample SLTSL for 100 ms after storage initialization has finished.
        gpio_init(SLTSL_GPIO_PIN);
        gpio_set_dir(SLTSL_GPIO_PIN, GPIO_IN);
        gpio_pull_down(SLTSL_GPIO_PIN);
        const uint64_t detect_start_ms = to_ms_since_boot(get_absolute_time());
        while (to_ms_since_boot(get_absolute_time()) - detect_start_ms < 100)
        {
            if (gpio_get(SLTSL_GPIO_PIN))
                msx_cart_main();
        }
    }

    const bool usb_storage_ready = volume_handover(VOLUME_MENU, VOLUME_USB);
    // Enumerate USB even after a physical flash/format failure. Ownership
    // keeps MSC not-ready and rejects I/O if a usable device could not be made.

    // Init device stack on the configured roothub port.
    tud_init(BOARD_TUD_RHPORT);
    stdio_init_all();
    printf("Start up\n");
    if (!filesystem_ready && usb_storage_ready)
        printf("FAT mount failed: USB recovery disk available; repair or format FAT12/FAT16, then restart.\n");
    else if (!usb_storage_ready)
        printf("Flash storage unavailable: USB device active, medium not ready.\n");

    while (1)
        tud_task();

    return 0;
}
