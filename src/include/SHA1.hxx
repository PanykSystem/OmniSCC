#pragma once
#include <cstddef>
#include <cstdint>

// Streaming SHA-1 for the openMSX database's ROM identities (menu time only).
class CSHA1 {
public:
    void Update(const uint8_t* data, size_t size);
    void Final(uint8_t digest[20]);
private:
    void Compress();
    uint32_t state_[5] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476, 0xc3d2e1f0};
    uint8_t block_[64] = {};
    uint64_t bytes_ = 0;
    size_t used_ = 0;
};
