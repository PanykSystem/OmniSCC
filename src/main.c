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
#include "Board.h"


int main(void)
{
   //_fs_create();
   if(!_fs_mount())
      _fs_create();

   //msx_cart_main();

    // check to see if we are plugged into MSX
    // by checking for high on SLTSL gpio for 100ms

    gpio_init(SLTSL_GPIO_PIN);
    gpio_set_dir(SLTSL_GPIO_PIN, GPIO_IN);
    gpio_pull_down(SLTSL_GPIO_PIN);
    while (to_ms_since_boot(get_absolute_time()) < 100)
    {
       if (gpio_get(SLTSL_GPIO_PIN))
              msx_cart_main();
    }

  // we are presumably powered from USB
  // enter USB mass storage mode

  // init device stack on configured roothub port
  tud_init(BOARD_TUD_RHPORT);
  stdio_init_all();   // for serial output, via printf()

  printf("Start up\n");  
  
  while (1)
  {
    tud_task();
  }

  return 0;
}
