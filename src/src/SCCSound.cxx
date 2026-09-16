#include "SCCSound.hxx"

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/sync.h"

#include "scc_dac.pio.h"


CSCCSound* CSCCSound::instance_ = nullptr;

namespace {

static PIO const s_audioPio = pio1;
static int s_audioSm = -1;
static int s_audioDma = -1;

static constexpr uint32_t OUTPUT_GPIO = 26;

// PIO output rate: each PIO instruction shifts one bit to the GPIO,
// so this sets the PDM bit-clock.
static constexpr uint32_t OUTPUT_BIT_HZ = 4'000'000;

// Original SCC master clock and the frequency at which the wavetable position
// advances by one step.
static constexpr uint32_t SCC_CLOCK_HZ = 3579545;
static constexpr uint32_t SCC_STEP_HZ = SCC_CLOCK_HZ / 32;

// Each DMA transfer is one 32-bit PDM word. 128 words @ 4 Mbit/s = ~1.024 ms
// of audio per buffer.
static constexpr uint32_t AUDIO_WORDS = 128;

// Delta-sigma modulator full-scale value. The mixed sample is clamped to
// +/-DAC_SCALE before feeding the 1-bit modulator.
static constexpr int32_t DAC_SCALE = 640;

// Two audio buffers; Core 1 renders into the free one while DMA plays the other.
static uint32_t s_audioBuf0[AUDIO_WORDS];
static uint32_t s_audioBuf1[AUDIO_WORDS];

// Index of the buffer currently being played by DMA.
static volatile uint8_t s_dmaPlayingBuf = 0;

// Index of the buffer that Core 1 may render into next, or 0xFF if none is free.
static volatile uint8_t s_freeBuf = 0xFF;

// True when a buffer contains freshly rendered audio ready for DMA.
static volatile bool s_bufReady[2] = {false, false};

// Apply a 4-bit channel volume to a signed 8-bit waveform sample.
static inline int16_t adjustSample(int8_t sample, uint8_t volume)
{
    return (int16_t)(((int32_t)sample * (int32_t)(volume & 0x0F)) >> 4);
}

// Compiler memory barrier: prevents reordering of volatile accesses around it.
static inline void memBarrier()
{
    __asm volatile ("" ::: "memory");
}

} // namespace


/**
 * Reset the SCC emulation state to power-on defaults.
 *
 * All wavetable slots are filled with -1, volumes are set to maximum, and all
 * channels are disabled. Called once when an SCC-capable cartridge is
 * constructed. It must NOT be called from the mode-select register: on the
 * Konami mapper that address is also the page-2 bank register, so resetting
 * there wipes the chip every time a game maps the SCC window back in.
 */
void __not_in_flash_func(CSCCSound::reset())
{
    for (uint32_t ch = 0; ch < 5; ++ch) {
        for (uint32_t i = 0; i < 32; ++i) {
            wave_[ch][i] = -1;
            volAdjustedWave_[ch][i] = adjustSample(-1, 15);
            if (ch < 4)
                waveShadow_[ch * 32 + i] = 0xFF;    // matches wave_ above
        }

        periodLo_[ch] = 0;
        periodHi_[ch] = 0;
        volume_[ch] = 15;
        phaseCount_[ch] = 0;
        wavePos_[ch] = 0;
        out_[ch] = volAdjustedWave_[ch][0];
    }

    enableMask_ = 0;
    deform_ = 0;

    sigmaError_ = 0;
    sccAccumulator_ = 0;
    currentSample_ = 0;

    buildActive_ = false;
    buildBuf_ = 0;
    buildIndex_ = 0;
}


/**
 * Recompute the volume-scaled wavetable for one channel.
 *
 * Called after a volume register write so the mixer can use pre-scaled samples
 * instead of doing the multiply every sample.
 */
void __not_in_flash_func(CSCCSound::rebuildVolAdjustedWave)(uint32_t ch)
{
    const uint8_t vol = volume_[ch] & 0x0F;

    for (uint32_t i = 0; i < 32; ++i) {
        volAdjustedWave_[ch][i] = adjustSample(wave_[ch][i], vol);
    }

    out_[ch] = volAdjustedWave_[ch][wavePos_[ch] & 31u];
}


/**
 * Program a simple sine wave into channel 0 and enable it.
 *
 * Useful as a quick smoke test of the SCC->PDM->PIO->DMA audio chain without
 * needing a real SCC-playing ROM.
 */
void CSCCSound::startupTestToneSine()
{
    reset();

    static const int8_t sine32[32] = {
          0,   25,   49,   71,   90,  106,  117,  125,
        127,  125,  117,  106,   90,   71,   49,   25,
          0,  -25,  -49,  -71,  -90, -106, -117, -125,
       -127, -125, -117, -106,  -90,  -71,  -49,  -25
    };

    for (uint8_t i = 0; i < 32; ++i) {
        enqueueWrite(i, (uint8_t)sine32[i]);
    }

    const uint16_t period = 0x180;

    enqueueWrite(0x80, (uint8_t)(period & 0xFF));
    enqueueWrite(0x81, (uint8_t)((period >> 8) & 0x0F));

    enqueueWrite(0x8A, 0x08);
    enqueueWrite(0x8B, 0x00);
    enqueueWrite(0x8C, 0x00);
    enqueueWrite(0x8D, 0x00);
    enqueueWrite(0x8E, 0x00);

    enqueueWrite(0x8F, 0x01);
    enqueueWrite(0xE0, 0x00);
}


/**
 * DMA interrupt handler: implements ping-pong audio buffering.
 *
 * Called on Core 1 when a buffer finishes playing. It schedules the next ready
 * buffer (or repeats the current one if nothing is ready yet) and marks the
 * just-played buffer as free for rendering.
 */
void __not_in_flash_func(CSCCSound::dmaIrqHandler)()
{
    if (s_audioDma < 0) {
        return;
    }

    const uint32_t mask = 1u << (uint32_t)s_audioDma;

    if (!(dma_hw->ints0 & mask)) {
        return;
    }

    dma_hw->ints0 = mask;

    const uint8_t justPlayed = s_dmaPlayingBuf;
    const uint8_t nextBuf = justPlayed ^ 1u;

    // The buffer that just finished is now available for rendering.
    s_freeBuf = justPlayed;

    // Prefer the buffer that has already been rendered; otherwise keep playing
    // the last buffer to avoid an audible gap.
    const uint8_t startBuf = s_bufReady[nextBuf] ? nextBuf : justPlayed;
    s_dmaPlayingBuf = startBuf;

    uint32_t* src = (startBuf == 0) ? s_audioBuf0 : s_audioBuf1;

    if (s_bufReady[startBuf]) {
        s_bufReady[startBuf] = false;
    }

    dma_channel_set_read_addr((uint32_t)s_audioDma, src, true);
}


/**
 * Initialize the PIO/DMA audio output path.
 *
 * This is called once from the main firmware startup. It pre-renders two
 * silent buffers, claims a PIO state machine and a DMA channel, and configures
 * the SCC DAC PIO program. The actual DMA IRQ handler is installed later on
 * Core 1 so that audio interrupts never preempt cartridge emulation.
 */
void CSCCSound::init()
{
    static bool initialized = false;
    if (initialized) {
        return;
    }
    initialized = true;

    instance_ = this;
    reset();

    // On normal boot the SCC is silent until a ROM writes to its registers.
    // For a standalone audio test, call startupTestToneSine() after init().

    // Pre-fill both buffers with silence so DMA has valid data immediately.
    for (uint32_t i = 0; i < AUDIO_WORDS; ++i) {
        renderOneWord(s_audioBuf0[i]);
    }
    for (uint32_t i = 0; i < AUDIO_WORDS; ++i) {
        renderOneWord(s_audioBuf1[i]);
    }

    s_bufReady[0] = true;
    s_bufReady[1] = true;

    s_audioSm = pio_claim_unused_sm(s_audioPio, false);
    if (s_audioSm < 0) {
        gpio_set_function(OUTPUT_GPIO, GPIO_FUNC_SIO);
        gpio_set_dir(OUTPUT_GPIO, GPIO_OUT);
        gpio_put(OUTPUT_GPIO, 0);
        return;
    }

    if (!pio_can_add_program(s_audioPio, &scc_dac_program)) {
        gpio_set_function(OUTPUT_GPIO, GPIO_FUNC_SIO);
        gpio_set_dir(OUTPUT_GPIO, GPIO_OUT);
        gpio_put(OUTPUT_GPIO, 0);
        return;
    }

    const uint32_t programOffset = pio_add_program(s_audioPio, &scc_dac_program);

    pio_gpio_init(s_audioPio, OUTPUT_GPIO);

    pio_sm_config pioCfg = scc_dac_program_get_default_config(programOffset);

    sm_config_set_out_pins(&pioCfg, OUTPUT_GPIO, 1);
    sm_config_set_out_shift(&pioCfg, true, true, 32);
    sm_config_set_fifo_join(&pioCfg, PIO_FIFO_JOIN_TX);

    sm_config_set_clkdiv(
        &pioCfg,
        (float)clock_get_hz(clk_sys) / (float)OUTPUT_BIT_HZ
    );

    pio_sm_set_consecutive_pindirs(
        s_audioPio,
        (uint32_t)s_audioSm,
        OUTPUT_GPIO,
        1,
        true
    );

    pio_sm_init(
        s_audioPio,
        (uint32_t)s_audioSm,
        programOffset,
        &pioCfg
    );

    s_audioDma = dma_claim_unused_channel(false);
    if (s_audioDma < 0) {
        pio_sm_set_enabled(s_audioPio, (uint32_t)s_audioSm, false);
        gpio_set_function(OUTPUT_GPIO, GPIO_FUNC_SIO);
        gpio_set_dir(OUTPUT_GPIO, GPIO_OUT);
        gpio_put(OUTPUT_GPIO, 0);
        return;
    }

    dma_channel_config dmaCfg =
        dma_channel_get_default_config((uint32_t)s_audioDma);

    channel_config_set_transfer_data_size(&dmaCfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dmaCfg, true);
    channel_config_set_write_increment(&dmaCfg, false);
    channel_config_set_dreq(
        &dmaCfg,
        pio_get_dreq(s_audioPio, (uint32_t)s_audioSm, true)
    );

    dma_channel_configure(
        (uint32_t)s_audioDma,
        &dmaCfg,
        &s_audioPio->txf[s_audioSm],
        s_audioBuf0,
        AUDIO_WORDS,
        false
    );

    // The DMA IRQ handler is installed and enabled on Core 1, keeping all
    // audio work off Core 0 where cartridge emulation runs.
    pio_sm_set_enabled(s_audioPio, (uint32_t)s_audioSm, true);
}


/**
 * Core 1 audio loop.
 *
 * Installs the DMA IRQ handler, starts the first DMA transfer, and then
 * repeatedly calls service() to render new buffers.
 */
void __not_in_flash_func(CSCCSound::startOnCore1())
{
    irq_set_exclusive_handler(DMA_IRQ_0, dmaIrqHandler);
    dma_channel_set_irq0_enabled((uint32_t)s_audioDma, true);
    irq_set_enabled(DMA_IRQ_0, true);

    s_dmaPlayingBuf = 0;
    s_freeBuf = 0xFF;
    s_bufReady[0] = false;
    s_bufReady[1] = true;

    // Start DMA after the IRQ handler is active so the first interrupt is caught.
    dma_channel_start((uint32_t)s_audioDma);

    while (true) {
        service();
    }
}


void __not_in_flash_func(CSCCSound::core1Entry)()
{
    if (instance_) {
        instance_->startOnCore1();
    }
}

/**
 * Render one 32-bit PDM word.
 *
 * Each bit is produced by a first-order delta-sigma modulator driven by the
 * current mixed SCC sample. The SCC sample itself is only recomputed when the
 * SCC accumulator crosses OUTPUT_BIT_HZ, emulating the original SCC clock.
 */
void __not_in_flash_func(CSCCSound::renderOneWord)(uint32_t& dstWord)
{
    uint32_t outWord = 0;

    for (uint32_t bit = 0; bit < 32; ++bit) {
        sccAccumulator_ += SCC_STEP_HZ;

        if (sccAccumulator_ >= OUTPUT_BIT_HZ) {
            do {
                sccAccumulator_ -= OUTPUT_BIT_HZ;
                currentSample_ = nextSample();
            } while (sccAccumulator_ >= OUTPUT_BIT_HZ);
        }

        sigmaError_ += currentSample_;

        if (sigmaError_ >= 0) {
            outWord |= (1u << bit);
            sigmaError_ -= DAC_SCALE;
        } else {
            sigmaError_ += DAC_SCALE;
        }
    }

    dstWord = outWord;
}


/**
 * Compute the next mixed SCC sample.
 *
 * Advances each enabled channel's wavetable position according to its 12-bit
 * period register, then sums the volume-adjusted samples and clamps to the
 * delta-sigma full-scale range.
 */
int16_t __not_in_flash_func(CSCCSound::nextSample)()
{
    int32_t mix = 0;
    const uint8_t deform = deform_;
    uint8_t enabled = enableMask_;

    for (uint32_t ch = 0; ch < 5; ++ch, enabled >>= 1) {
        uint16_t p =
            (uint16_t)periodLo_[ch] |
            ((uint16_t)periodHi_[ch] << 8);

        // SCC deformation bits alter how the period is interpreted.
        if (deform & 0x02) {
            p &= 0x00FF;
        } else if (deform & 0x01) {
            p >>= 8;
        }

        if (p > 8) {
            uint32_t next = (uint32_t)phaseCount_[ch] + 32u;
            const uint32_t divider = (uint32_t)p + 1u;

            while (next >= divider) {
                next -= divider;
                wavePos_[ch] = (wavePos_[ch] + 1u) & 31u;
                out_[ch] = volAdjustedWave_[ch][wavePos_[ch]];
            }

            phaseCount_[ch] = (uint16_t)next;
        }

        if (enabled & 1u) {
            mix += out_[ch];
        }
    }

    if (mix > DAC_SCALE) {
        mix = DAC_SCALE;
    } else if (mix < -DAC_SCALE) {
        mix = -DAC_SCALE;
    }

    return (int16_t)mix;
}

volatile bool sccInitialized_ = false;

/**
 * Apply all pending SCC register writes queued by Core 0.
 *
 * Runs on Core 1 inside service(). The queue decouples the tight Z80 bus loop
 * from audio rendering: if the queue overflows, the oldest writes are dropped.
 */
void __not_in_flash_func(CSCCSound::drainWriteQueue())
{
    CSCCWriteQueue<64>::Entry e;
    while (g_sccWriteQueue.pop(e)) {
        uint8_t address = e.address;
        uint8_t value = e.value;

        address &= 0xFF;

        // Wavetable RAM: 5 channels x 32 samples.
        if (address < 0x80) {
            const uint32_t ch = address >> 5;
            const uint32_t index = address & 0x1F;

            if (ch >= 5) return; 

            if (ch < 4) {
                wave_[ch][index] = (int8_t)value;
                volAdjustedWave_[ch][index] =
                    adjustSample(wave_[ch][index], volume_[ch]);

                // Channel 4 mirrors channel 3's waveform on the real SCC.
                if (ch == 3) {
                    wave_[4][index] = wave_[3][index];
                    volAdjustedWave_[4][index] =
                        adjustSample(wave_[4][index], volume_[4]);
                }

                out_[ch] = volAdjustedWave_[ch][wavePos_[ch] & 31u];
                if (ch == 3) {
                    out_[4] = volAdjustedWave_[4][wavePos_[4] & 31u];
                }
            }
            sccInitialized_ = true;
            return;
        }

        // Frequency, volume, and enable registers.
        if (address < 0xA0) {
            const uint32_t reg = address & 0x0F;

            if (reg < 0x0A) {
                const uint32_t ch = reg >> 1;

                if (reg & 1u) {
                    periodHi_[ch] = value & 0x0F;
                } else {
                    periodLo_[ch] = value;
                }

                phaseCount_[ch] = 0;

                // Some deformation modes reset the wavetable position.
                if (deform_ & 0x20u) {
                    wavePos_[ch] = 0;
                }

                out_[ch] = volAdjustedWave_[ch][wavePos_[ch] & 31u];
                return;
            }

            if (reg < 0x0F) {
                const uint32_t ch = reg - 0x0Au;
                volume_[ch] = value & 0x0Fu;
                rebuildVolAdjustedWave(ch);
                return;
            }

            enableMask_ = value & 0x1Fu;
            return;
        }

        // Deformation / mode register.
        if (address >= 0xE0) {
            deform_ = value;
        }

    }
}

/**
 * Audio pump: process queued SCC writes and render one PDM buffer when free.
 *
 * Must be called repeatedly from Core 1. It is designed to be very fast:
 * it only drains the write queue and renders at most one 32-bit word per call,
 * so it can be interleaved with other work if needed.
 */
void __not_in_flash_func(CSCCSound::service())
{
    // Apply all register writes from Core 0 first. This is cheap and avoids
    // audio artifacts from stale register state.
    drainWriteQueue();

    if (!buildActive_) {
        const uint32_t irqState = save_and_disable_interrupts();
        uint8_t freeBuf = s_freeBuf;

        if (freeBuf != 0xFF) {
            s_freeBuf = 0xFF;
        }

        restore_interrupts(irqState);

        if (freeBuf == 0xFF) {
            return;
        }

        buildBuf_ = freeBuf;
        buildIndex_ = 0;
        buildActive_ = true;
    }

    uint32_t* dst = (buildBuf_ == 0) ? s_audioBuf0 : s_audioBuf1;

    renderOneWord(dst[buildIndex_]);
    ++buildIndex_;

    if (buildIndex_ >= AUDIO_WORDS) {
        s_bufReady[buildBuf_] = true;
        buildActive_ = false;
    }
}


/**
 * Read an SCC register from the Z80 side.
 *
 * Wavetable RAM reads back what was last written to it, from power-on. The
 * 0xE0 deformation register is implemented as write-only on read (returns 0xFF
 * but is considered "initialized").
 */
uint8_t __not_in_flash_func(CSCCSound::readRegister)(uint8_t address)
{
    address &= 0xFF;

    if (address < 0x80) {
        // Wave RAM comes from the Core-0 shadow, never from wave_. wave_ is
        // only updated when Core 1 drains the write queue, and a Z80 that
        // writes a wave byte and reads it back 7 T-states later -- which is how
        // SCC detection routines work, SM25PLAY.COD on SCCMUZAX.DSK among them
        // -- would see the old value whenever Core 1 had not reached the write
        // yet: a coin toss per run on hardware, where an emulated SCC answers
        // synchronously and always passes. The shadow is written on Core 0 in
        // enqueueWrite, in the same bus cycle as the write, so the read-back is
        // consistent no matter when Core 1 catches up.
        return waveShadow_[address];
    }

    if (address >= 0xE0) {
        deform_ = 0xFF;
        memBarrier();
    }

    return 0xFF;
}


/**
 * Direct GPIO toggle test on AUDIO_GPIO.
 *
 * Disables PIO and DMA, then toggles GPIO 26 at a fixed software rate.
 * Useful as a hardware sanity check; not used in normal operation.
 */
void CSCCSound::initDmaChainTest()
{
    for (uint32_t sm = 0; sm < 4; ++sm) {
        pio_sm_set_enabled(pio1, sm, false);
    }

    for (uint32_t ch = 0; ch < 12; ++ch) {
        dma_channel_abort(ch);
    }

    gpio_init(26);
    gpio_set_function(26, GPIO_FUNC_SIO);
    gpio_set_dir(26, GPIO_OUT);

    // Disable interrupts permanently for this test; the return value is unused.
    save_and_disable_interrupts();

    while (true) {
        sio_hw->gpio_set = 1u << 26;
        busy_wait_at_least_cycles(50);

        sio_hw->gpio_clr = 1u << 26;
        busy_wait_at_least_cycles(50);
    }
}

__attribute__((section(".data")))
CSCCWriteQueue<64> g_sccWriteQueue;
