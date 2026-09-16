/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 */

// OmniSCC.h

// setting first overrides the value in the default header

//#define PICO_FLASH_SPI_CLKDIV 2     // use for winbond flash
#define PICO_FLASH_SPI_CLKDIV 4     // use for slower flash (e.g. zbit)

// pick up the rest of the settings
#include "boards/pico.h"
