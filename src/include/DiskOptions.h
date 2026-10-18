#pragma once

// Available in loader and Disk mode. Request: exchange byte 0, no reply payload.
#define CMD_SET_DISK_OPTIONS 0x06
#define DISK_OPTION_MULTIDISK 0x01
#define DISK_OPTIONS_DEFAULT DISK_OPTION_MULTIDISK
