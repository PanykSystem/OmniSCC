#include "SHA1.hxx"
#include <cstring>

namespace {
uint32_t SHA1Rotate(uint32_t value, unsigned bits) { return (value << bits) | (value >> (32 - bits)); }
}

void CSHA1::Compress()
{
    uint32_t words[16];
    for (unsigned i = 0; i < 16; ++i) {
        const auto* p = block_ + i * 4;
        words[i] = uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
    }
    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3], e = state_[4];
    for (unsigned i = 0; i < 80; ++i) {
        const unsigned n = i & 15;
        if (i >= 16) words[n] = SHA1Rotate(words[(i - 3) & 15] ^ words[(i - 8) & 15]
                                      ^ words[(i - 14) & 15] ^ words[n], 1);
        uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5a827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ed9eba1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8f1bbcdc; }
        else { f = b ^ c ^ d; k = 0xca62c1d6; }
        const uint32_t next = SHA1Rotate(a, 5) + f + e + k + words[n];
        e = d; d = c; c = SHA1Rotate(b, 30); b = a; a = next;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d; state_[4] += e;
}

void CSHA1::Update(const uint8_t* data, size_t size)
{
    bytes_ += size;
    while (size) {
        size_t count = sizeof(block_) - used_;
        if (count > size) count = size;
        std::memcpy(block_ + used_, data, count);
        used_ += count; data += count; size -= count;
        if (used_ == sizeof(block_)) { Compress(); used_ = 0; }
    }
}

void CSHA1::Final(uint8_t digest[20])
{
    const uint64_t bits = bytes_ * 8;
    block_[used_++] = 0x80;
    if (used_ > 56) {
        while (used_ < 64) block_[used_++] = 0;
        Compress(); used_ = 0;
    }
    while (used_ < 56) block_[used_++] = 0;
    for (unsigned i = 0; i < 8; ++i) block_[56 + i] = uint8_t(bits >> (56 - i * 8));
    Compress();
    for (unsigned i = 0; i < 20; ++i) digest[i] = uint8_t(state_[i / 4] >> (24 - (i & 3) * 8));
}
