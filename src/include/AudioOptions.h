#pragma once

// Loader/bare-SCC command. Byte 0 = linear master amplitude, 0..100 percent.
// No reply payload; invalid values return FE without changing the old setting.
#define CMD_SET_SCC_VOLUME 0x07
#define SCC_VOLUME_DEFAULT 100
