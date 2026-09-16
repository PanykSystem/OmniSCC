/**
 * MSX cartridge for Raspberry Pi Pico (16MB clone with all GPIO)
 * Alexey Spirkov 2026 (dev _at_ alsp.net)
 *
 * Needs to be a release NOT debug build for the cartridge emulation to work
 * 
 */

#pragma once

// ****************************************************************************
// Cartridge options
// ****************************************************************************

// force compilation of variant with level shifters
//#define WITH_SHIFTERS

// permananent storage size - assumed that FW size is less than 1Mb, Cartridge size is less then 1MB

#define PERM_BASE    (1 * 1024 * 1024u)   // 1MB firmware size (if changed - linker script modification is needed)
#define PERM_BYTES   (1 * 1024 * 1024u)   // 1MB max cartridge size (can be freely changed - affectr size of FAT partition)


// ****************************************************************************
// DO NOT EDIT BELLOW THIS LINE
// ****************************************************************************


#ifdef __cplusplus
extern "C" {
#endif

#include "pico/stdlib.h"
#include "hardware/sync.h"

void msx_cart_main();

#ifdef __cplusplus
}
#endif

#define FS_BASE (PERM_BASE + PERM_BYTES)
#define FS_SIZE (PICO_FLASH_SIZE_BYTES - FS_BASE)

#define ALL_GPIO_MASK   	0x3FFFFFFF
#define ADDR_GPIO_MASK  	0x00FFFF00
#define ADDR_GPIO_SHIFT  	8
#define DATA_GPIO_MASK  	0x000000FF
#define RD_GPIO_MASK  	    0x01000000
#define WR_GPIO_MASK  	    0x02000000
#define IORQ_GPIO_MASK  	0x04000000
// GP27 carries /BUSDIR on the base board and the level-shifter direction on
// OmniSCC+; GP28 is /WAIT on both. Keep every mask in step with its pin number.
#ifdef WITH_SHIFTERS
#define DATADIR_GPIO_MASK  	0x08000000
#define DATADIR_PIN         27
#define WAIT_GPIO_MASK  	0x10000000
#define WAIT_PIN            28
#else
#define BUSDIR_GPIO_MASK  	0x08000000
#define WAIT_GPIO_MASK  	0x10000000
#define WAIT_PIN            28
#endif
#define SLTSL_GPIO_MASK  	0x20000000
#define SLTSL_GPIO_PIN      29
#define SET_DATA_MODE_OUT   gpio_set_dir_out_masked(DATA_GPIO_MASK)
#define SET_DATA_MODE_IN    gpio_set_dir_in_masked(DATA_GPIO_MASK)

#define OVERCLOCK_SPEED 250000

static inline void forceWait()
{
    gpio_set_dir(WAIT_PIN, GPIO_OUT);
    gpio_put(WAIT_PIN, 0);
}

static inline void releaseWait()
{
    gpio_set_dir(WAIT_PIN, GPIO_IN);
}


static_assert((DATA_GPIO_MASK & (1u << 26)) == 0,
              "GPIO26 is used by MSX data bus");

static_assert((ADDR_GPIO_MASK & (1u << 26)) == 0,
              "GPIO26 is used by MSX address bus");

static_assert((SLTSL_GPIO_MASK & (1u << 26)) == 0,
              "GPIO26 is used by /SLTSL");

static_assert((RD_GPIO_MASK & (1u << 26)) == 0,
              "GPIO26 is used by /RD");

static_assert((WR_GPIO_MASK & (1u << 26)) == 0,
              "GPIO26 is used by /WR");

// forceWait()/releaseWait() drive WAIT_PIN while the bus loops decode
// WAIT_GPIO_MASK, so the two must describe the same pin. Same for DATADIR.
static_assert(WAIT_GPIO_MASK == (1u << WAIT_PIN),
              "WAIT_GPIO_MASK does not match WAIT_PIN");

static_assert(SLTSL_GPIO_MASK == (1u << SLTSL_GPIO_PIN),
              "SLTSL_GPIO_MASK does not match SLTSL_GPIO_PIN");

#ifdef WITH_SHIFTERS
static_assert(DATADIR_GPIO_MASK == (1u << DATADIR_PIN),
              "DATADIR_GPIO_MASK does not match DATADIR_PIN");
#endif
