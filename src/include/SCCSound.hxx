#pragma once

#include <stdint.h>
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

    //void writeRegister(uint8_t address, uint8_t value);
    uint8_t readRegister(uint8_t address);

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

    // Wavetable RAM: 5 channels x 32 samples, signed 8-bit.
    int8_t wave_[5][32];

    // Core-0 copy of the 4 x 32 bytes of wave RAM the Z80 can address
    // (0x9800-0x987F), as last written. Owned entirely by Core 0: written in
    // enqueueWrite, read in readRegister, cleared in reset. See enqueueWrite.
    uint8_t waveShadow_[128];

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
