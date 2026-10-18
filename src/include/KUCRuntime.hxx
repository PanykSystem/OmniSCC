#pragma once

// KUC code overlays bytes 104..120 KiB of the cartridge workspace, preserving
// the top 8 KiB for the live menu. Other types retain all 128 KiB, including
// Nextor. The menu copies this image before constructing any KUC helper.
#ifdef PICO_ON_DEVICE
#include "pico.h"
#define KUC_RAM(name) __attribute__((noinline, section(".kuc_runtime.text"))) name
#else
#define KUC_RAM(name) name
#endif
