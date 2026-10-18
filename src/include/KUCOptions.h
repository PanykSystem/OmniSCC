#pragma once

// Loader/bare-SCC mailbox; bit 0 explicitly opts into an unknown flash profile.
#define CMD_SET_KUC_OPTIONS 0x09
#define KUC_OPTION_ALLOW_UNKNOWN_FLASH 0x01
