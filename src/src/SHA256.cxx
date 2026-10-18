#include "SHA256.hxx"

namespace {
constexpr uint32_t RoundConstants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};
uint32_t Rotate(uint32_t value, unsigned bits) { return (value >> bits) | (value << (32 - bits)); }
}

void CSHA256::Compress()
{
    uint32_t words[64];
    for (unsigned i = 0; i < 16; ++i) {
        const auto* p = block_ + 4 * i;
        words[i] = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
    }
    for (unsigned i = 16; i < 64; ++i) {
        const uint32_t x = words[i - 15], y = words[i - 2];
        words[i] = words[i - 16] + (Rotate(x, 7) ^ Rotate(x, 18) ^ (x >> 3))
                 + words[i - 7] + (Rotate(y, 17) ^ Rotate(y, 19) ^ (y >> 10));
    }
    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (unsigned i = 0; i < 64; ++i) {
        const uint32_t first = h + (Rotate(e, 6) ^ Rotate(e, 11) ^ Rotate(e, 25))
                             + ((e & f) ^ (~e & g)) + RoundConstants[i] + words[i];
        const uint32_t second = (Rotate(a, 2) ^ Rotate(a, 13) ^ Rotate(a, 22))
                              + ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + first;
        d = c; c = b; b = a; a = first + second;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

void CSHA256::Update(const uint8_t* data, size_t size)
{
    bytes_ += size;
    for (size_t i = 0; i < size; ++i) {
        block_[used_++] = data[i];
        if (used_ == sizeof(block_)) { Compress(); used_ = 0; }
    }
}

void CSHA256::Final(uint8_t digest[32])
{
    const uint64_t bits = bytes_ * 8;
    block_[used_++] = 0x80;
    if (used_ > 56) {
        while (used_ < 64) block_[used_++] = 0;
        Compress();
        used_ = 0;
    }
    while (used_ < 56) block_[used_++] = 0;
    for (unsigned i = 0; i < 8; ++i) block_[56 + i] = uint8_t(bits >> (56 - 8 * i));
    Compress();
    for (unsigned i = 0; i < 32; ++i) digest[i] = uint8_t(state_[i / 4] >> (24 - 8 * (i % 4)));
}
