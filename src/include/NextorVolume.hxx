#pragma once
#include <stdint.h>
enum class NextorVolumeResult { Compatible, Unsupported, Invalid, ReadError };
NextorVolumeResult ValidateNextorVolume(uint32_t capacity,
    bool (*readSector)(uint32_t, uint8_t*, void*), void* context);
