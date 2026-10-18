#pragma once

#include <stdint.h>
#include <atomic>
#include "SCCWriteQueue.hxx"

/**
 * Konami SCC audio emulation.
 *
 * The SCC provides 5 wavetable channels. Register writes are enqueued from
 * the cartridge hot path (Core 0) and consumed on Core 1, which renders
 * PDM (pulse-density modulated) audio and feeds it to PIO/DMA on GPIO 26.
 */
class CSCCSound {
public:
    static constexpr uint32_t AUDIO_GPIO = 26;

    void init();
    void reset();

    // Core 0 publishes one aligned gain; Core 1 reads it without a lock.
    // Q16 keeps division out of the SRAM audio loop. reset() preserves it.
    bool SetMasterVolume(uint8_t percent) {
        if (percent > 100) return false;
        masterGain_.store((uint32_t(percent) * 65536u) / 100u,
                          std::memory_order_relaxed);
        return true;
    }

    //void writeRegister(uint8_t address, uint8_t value);
    uint8_t readRegister(uint8_t address);
    uint8_t readKUCRegister(uint8_t address, bool plus);

    // Encode the register layout on each event. A subsequent mapper mode change
    // cannot reinterpret writes already waiting for the audio core.
    inline bool enqueueKUCWrite(uint8_t address, uint8_t value, bool plus)
    {
        if (plus) {
            if (address < 0xa0) waveShadow_[address] = value;
        } else if (address < 0x80) {
            waveShadow_[address] = value;
            if (address >= 0x60) waveShadow_[address + 0x20] = value;
        }
        return g_sccWriteQueue.push(uint16_t(address) | (plus ? 0x1000u : 0), value);
    }

    inline bool enqueueDAC(uint8_t value)
    {
        return g_sccWriteQueue.push(0x2000, value);
    }

    /**
     * Render/audio pump. Must be called frequently on Core 1.
     * It is very cheap: it produces at most one 32-bit PDM word per call.
     */
    void service();

    // Self-contained sine-wave test; useful for verifying the audio path.
    void startupTestToneSine();

    // Direct GPIO toggle test on AUDIO_GPIO (bypasses PIO/DMA).
    void initDmaChainTest();

    // Entry point launched on Core 1 via multicore_launch_core1.
    static void core1Entry();

    // Called from the cartridge Execute() loop on Core 0.
    // Returns false if the queue is full (write is dropped).
    //
    // Wave RAM writes also land in waveShadow_ right here, on Core 0, in the
    // same bus cycle. readRegister serves wave RAM from that shadow, so a Z80
    // that writes a wave byte and reads it straight back -- which is how SCC
    // detection routines work, 7 T-states apart -- always sees its own write,
    // regardless of when Core 1 gets round to draining the queue.
    inline bool enqueueWrite(uint8_t address, uint8_t value)
    {
        if (address < 0x80)
            waveShadow_[address] = value;
        return g_sccWriteQueue.push(address, value);
    }

private:
    void startOnCore1();
    void renderOneWord(uint32_t& dstWord);
    int16_t nextSample();
    void rebuildVolAdjustedWave(uint32_t ch);

    static void dmaIrqHandler();
    void drainWriteQueue();

    std::atomic<uint32_t> masterGain_{65536u}; // 100% at power-on

    // Wavetable RAM: 5 channels x 32 samples, signed 8-bit.
    int8_t wave_[5][32];

    // Core-0 copy of the 5 x 32 bytes of wave RAM (the fifth requires SCC+)
    // (0x9800-0x987F), as last written. Owned entirely by Core 0: written in
    // enqueueWrite, read in readRegister, cleared in reset. See enqueueWrite.
    uint8_t waveShadow_[160];

    uint8_t dac_ = 128; // Unsigned DAC midpoint is silence, independent of SCC gain.

    // Volume-adjusted copy of wave_ for fast mixing.
    int16_t volAdjustedWave_[5][32];

    // 12-bit period split into two bytes for lock-free Core0-write / Core1-read.
    // A transient lo/hi mismatch only causes a short pitch glitch, not a crash.
    uint8_t periodLo_[5];
    uint8_t periodHi_[5];

    // Channel volume (4 bits) and on/off mask (5 bits).
    uint8_t volume_[5];
    uint8_t enableMask_;

    // Deformation/mode register (0xE0 on SCC).
    uint8_t deform_;

    // Per-channel phase accumulator and current wavetable position.
    uint16_t phaseCount_[5];
    uint8_t wavePos_[5];

    // Most recently computed sample for each channel.
    int16_t out_[5];

    // 1-bit delta-sigma modulator state.
    int32_t sigmaError_;
    uint32_t sccAccumulator_;
    int16_t currentSample_;

    // Double-buffered audio generation state.
    bool buildActive_;
    uint8_t buildBuf_;
    uint32_t buildIndex_;

    static CSCCSound* instance_;
};
