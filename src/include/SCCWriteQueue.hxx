#pragma once
#include <cstdint>
#include "hardware/sync.h" // __dmb()

/**
 * Single-producer single-consumer lock-free ring buffer.
 *
 * Used to pass SCC register writes from Core 0 (cartridge bus loop) to
 * Core 1 (audio rendering). CAPACITY must be a power of two.
 */
template <uint32_t CAPACITY>
class CSCCWriteQueue
{
    static_assert((CAPACITY & (CAPACITY - 1)) == 0, "CAPACITY must be power of two");

public:
    struct Entry {
        uint16_t address; // Low byte is the register; high bits tag KUC audio events.
        uint8_t value;
    };

    CSCCWriteQueue() : head_(0), tail_(0) {}

    // Producer side: called from Core 0 inside the RAM-resident Execute() loop.
    // Must be inlined to avoid flash latency on the hot path.
    __attribute__((always_inline))
    inline bool push(uint16_t address, uint8_t value)
    {
        const uint32_t head = head_;
        const uint32_t nextHead = (head + 1) & (CAPACITY - 1);

        if (nextHead == tail_) {
            return false;
        }

        buf_[head].address = address;
        buf_[head].value   = value;

        // Memory barrier: make sure the entry data is committed before Core 1
        // can observe the updated head index.
        __dmb();
        head_ = nextHead;
        return true;
    }

    // Consumer side: called from Core 1.
    __attribute__((always_inline))
    inline bool pop(Entry& out)
    {
        const uint32_t tail = tail_;

        // Barrier: ensure we read the tail index before checking head,
        // otherwise we might observe a stale empty state.
        __dmb();
        if (tail == head_) {
            return false;
        }

        out = buf_[tail];

        // Barrier: make sure the entry read completes before Core 0 can reuse
        // this slot.
        __dmb();
        tail_ = (tail + 1) & (CAPACITY - 1);
        return true;
    }

private:
    Entry buf_[CAPACITY];
    volatile uint32_t head_; // written by Core 0, read by Core 1
    volatile uint32_t tail_; // written by Core 1, read by Core 0
};

extern CSCCWriteQueue<64> g_sccWriteQueue;
