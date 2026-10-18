#pragma once

#include <cstddef>
#include <cstdint>

// Portable streaming SHA-256 (FIPS 180-4). Used only at cartridge preparation,
// never from the MSX bus loop; firmware and simulator share the same identity.
class CSHA256 {
public:
    void Update(const uint8_t* data, size_t size);
    void Final(uint8_t digest[32]);

private:
    void Compress();
    uint32_t state_[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    uint8_t block_[64] = {};
    uint64_t bytes_ = 0;
    size_t used_ = 0;
};
