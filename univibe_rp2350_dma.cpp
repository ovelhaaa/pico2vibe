#include <stdio.h>
#include <math.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/sync.h"

#include "i2s_bidir_dma.pio.h"

// ============================================================================
// Hardware / áudio
// ============================================================================

#define PIN_MCLK   21
#define PIN_BCLK   16
#define PIN_LRCLK  17
#define PIN_DOUT   18
#define PIN_DIN    19

#ifndef USER_INTERFACE
#define USER_INTERFACE 1
#endif

#if USER_INTERFACE
#define UI_PIN_ENC_A        2
#define UI_PIN_ENC_B        3
#define UI_PIN_ENC_SW       4
#define UI_PIN_LED_PARAM    6
#define UI_PIN_LED_MODE     7
#define UI_ENCODER_REVERSE  0
#endif

#include "src/dsp/vibe_core.hpp"

// ============================================================================
// Conversão PCM <-> float
// ============================================================================

static inline float pcm24_to_float(int32_t v) {
    v >>= 8;
    return (float)v * (1.0f / 8388608.0f);
}

static inline int32_t float_to_pcm24(float v, uint32_t &rng_state) {
#if ENABLE_TPDF_DITHER
    // TPDF dither at roughly +/-1 LSB before quantization reduces low-level truncation grit.
    const float tpdf = (noise_bipolar(rng_state) + noise_bipolar(rng_state)) * (0.5f / 8388607.0f);
    v += tpdf;
#else
    (void)rng_state;
#endif
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return ((int32_t)(v * 8388607.0f)) << 8;
}

struct OutputDcBlocker {
    float x1 = 0.0f;
    float y1 = 0.0f;
};

struct OutputConditioner {
    OutputDcBlocker dc_l;
    OutputDcBlocker dc_r;
    float env = 0.0f;
    float trim = 1.0f;
    uint32_t dither_rng = 0xC001C0DEu;
};

static OutputConditioner output_conditioner;

static inline float dc_block_sample(OutputDcBlocker &s, float x) {
#if ENABLE_OUTPUT_DC_BLOCKER
    // Very low cutoff (~8 Hz) removes slow bias drift while preserving guitar lows.
    constexpr float kDcR = 0.99886f;
    const float y = (x - s.x1) + kDcR * s.y1;
    s.x1 = x;
    s.y1 = y;
    return y;
#else
    (void)s;
    return x;
#endif
}

static inline float condition_output_sample(OutputConditioner &st, float x, OutputDcBlocker &dc_state) {
    float y = dc_block_sample(dc_state, x);

#if ENABLE_OUTPUT_AUTO_HEADROOM
    const float abs_y = fabsf(y);
    const float env_attack = 0.14f;
    const float env_release = 0.003f;
    st.env += (abs_y > st.env ? env_attack : env_release) * (abs_y - st.env);
    const float target = (st.env > 0.92f) ? (0.92f / (st.env + 1e-12f)) : 1.0f;
    const float trim_attack = 0.20f;
    const float trim_release = 0.0015f;
    st.trim += (target < st.trim ? trim_attack : trim_release) * (target - st.trim);
    y *= st.trim;
#endif

#if ENABLE_OUTPUT_SOFT_LIMITER
    // Safety limiter: soft knee near full-scale avoids edgy hard clipping.
    const float limit_drive = 1.25f;
    y = soft_clip_cubic(y * limit_drive) * (1.0f / limit_drive);
#endif
    return y;
}

// ============================================================================
// Buffers
// ============================================================================

static float dsp_in_l[PERIOD];
static float dsp_in_r[PERIOD];
static float dsp_out_l[PERIOD];
static float dsp_out_r[PERIOD];

static Vibe univibe(dsp_out_l, dsp_out_r);
#if USER_INTERFACE
static VibeUi vibe_ui;
#endif

alignas(4) static int32_t rx_dma_buf[2][DMA_WORDS_PER_BLOCK];
alignas(4) static int32_t tx_dma_buf[2][DMA_WORDS_PER_BLOCK];
alignas(4) static int32_t tx_silence_buf[DMA_WORDS_PER_BLOCK] = {0};

static volatile bool rx_block_ready[2] = {false, false};
static volatile bool tx_block_ready[2] = {false, false};

static volatile uint32_t rx_overruns  = 0;
static volatile uint32_t tx_underruns = 0;

static volatile int rx_fill_index = 0;
static volatile int tx_expected_index = 0;
static volatile int tx_bootstrap_remaining = 2;

// ============================================================================
// PIO / DMA state
// ============================================================================

static PIO pio = pio0;
static uint sm_clk = 0;
static uint sm_tx  = 1;
static uint sm_rx  = 2;

static int dma_rx_chan = -1;
static int dma_tx_chan = -1;

// ============================================================================
// Clock setup
// ============================================================================

static bool setup_audio_sys_clock(void) {
    return set_sys_clock_hz(AUDIO_SYS_CLOCK_HZ, false);
}

static void setup_mclk(uint gpio_pin) {
    gpio_set_function(gpio_pin, GPIO_FUNC_GPCK);

    clock_gpio_init_int_frac8(
        gpio_pin,
        CLOCKS_CLK_GPOUT0_CTRL_AUXSRC_VALUE_CLK_SYS,
        12,
        0
    );
}

// ============================================================================
// I2S PIO
// ============================================================================

static void i2s_init_pio(void) {
    uint off_clk = pio_add_program(pio, &i2s_master_clk_program);
    uint off_tx  = pio_add_program(pio, &i2s_tx_slave_program);
    uint off_rx  = pio_add_program(pio, &i2s_rx_slave_program);

    {
        pio_sm_config c = i2s_master_clk_program_get_default_config(off_clk);
        sm_config_set_clkdiv(&c, 24.0f);
        sm_config_set_sideset_pins(&c, PIN_BCLK); // GPIO16/17

        pio_gpio_init(pio, PIN_BCLK);
        pio_gpio_init(pio, PIN_LRCLK);
        pio_sm_set_consecutive_pindirs(pio, sm_clk, PIN_BCLK, 2, true);
        pio_sm_init(pio, sm_clk, off_clk, &c);
    }

    {
        pio_sm_config c = i2s_tx_slave_program_get_default_config(off_tx);
        sm_config_set_clkdiv(&c, 1.0f);
        sm_config_set_out_pins(&c, PIN_DOUT, 1);
        sm_config_set_out_shift(&c, false, true, 32);
        sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

        pio_gpio_init(pio, PIN_DOUT);
        pio_sm_set_consecutive_pindirs(pio, sm_tx, PIN_DOUT, 1, true);
        pio_sm_init(pio, sm_tx, off_tx, &c);
    }

    {
        pio_sm_config c = i2s_rx_slave_program_get_default_config(off_rx);
        sm_config_set_clkdiv(&c, 1.0f);
        sm_config_set_in_pins(&c, PIN_DIN);
        sm_config_set_in_shift(&c, false, true, 32);
        sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);

        pio_gpio_init(pio, PIN_DIN);
        pio_sm_set_consecutive_pindirs(pio, sm_rx, PIN_DIN, 1, false);
        pio_sm_init(pio, sm_rx, off_rx, &c);
    }
}

static void i2s_start_pio(void) {
    pio_sm_set_enabled(pio, sm_tx, true);
    pio_sm_set_enabled(pio, sm_rx, true);
    pio_sm_set_enabled(pio, sm_clk, true);
}

// ============================================================================
// DMA
// ============================================================================

static void start_rx_dma_to_buffer(int index) {
    dma_channel_transfer_to_buffer_now(
        dma_rx_chan,
        rx_dma_buf[index],
        DMA_WORDS_PER_BLOCK
    );
}

static void start_tx_dma_from_buffer(const int32_t *buffer) {
    dma_channel_transfer_from_buffer_now(
        dma_tx_chan,
        buffer,
        DMA_WORDS_PER_BLOCK
    );
}

static void __isr dma_irq0_handler(void) {
    if (dma_channel_get_irq0_status(dma_rx_chan)) {
        dma_channel_acknowledge_irq0(dma_rx_chan);

        const int finished = rx_fill_index;
        const int next     = finished ^ 1;

        if (rx_block_ready[finished]) {
            rx_overruns++;
        }

        __dmb();
        rx_block_ready[finished] = true;
        rx_fill_index = next;

        start_rx_dma_to_buffer(next);
    }

    if (dma_channel_get_irq0_status(dma_tx_chan)) {
        dma_channel_acknowledge_irq0(dma_tx_chan);

        const int32_t *next_buf = tx_silence_buf;

        if (tx_bootstrap_remaining > 0) {
            tx_bootstrap_remaining--;
        } else {
            const int idx = tx_expected_index;

            if (tx_block_ready[idx]) {
                __dmb();
                tx_block_ready[idx] = false;
                next_buf = tx_dma_buf[idx];
                tx_expected_index ^= 1;
            } else {
                tx_underruns++;
                next_buf = tx_silence_buf;
            }
        }

        start_tx_dma_from_buffer(next_buf);
    }
}

static void dma_audio_init(void) {
    dma_rx_chan = dma_claim_unused_channel(true);
    dma_tx_chan = dma_claim_unused_channel(true);

    {
        dma_channel_config c = dma_channel_get_default_config(dma_rx_chan);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, false);
        channel_config_set_write_increment(&c, true);
        channel_config_set_dreq(&c, pio_get_dreq(pio, sm_rx, false));
        channel_config_set_high_priority(&c, true);

        dma_channel_configure(
            dma_rx_chan,
            &c,
            rx_dma_buf[0],
            &pio->rxf[sm_rx],
            DMA_WORDS_PER_BLOCK,
            false
        );

        dma_channel_set_irq0_enabled(dma_rx_chan, true);
    }

    {
        dma_channel_config c = dma_channel_get_default_config(dma_tx_chan);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
        channel_config_set_read_increment(&c, true);
        channel_config_set_write_increment(&c, false);
        channel_config_set_dreq(&c, pio_get_dreq(pio, sm_tx, true));
        channel_config_set_high_priority(&c, true);

        dma_channel_configure(
            dma_tx_chan,
            &c,
            &pio->txf[sm_tx],
            tx_silence_buf,
            DMA_WORDS_PER_BLOCK,
            false
        );

        dma_channel_set_irq0_enabled(dma_tx_chan, true);
    }

    irq_set_exclusive_handler(DMA_IRQ_0, dma_irq0_handler);
    irq_set_enabled(DMA_IRQ_0, true);
}

static void audio_start(void) {
    rx_fill_index = 0;
    tx_expected_index = 0;
    tx_bootstrap_remaining = 2;
    rx_block_ready[0] = false;
    rx_block_ready[1] = false;
    tx_block_ready[0] = false;
    tx_block_ready[1] = false;
    output_conditioner = {};
    output_conditioner.trim = 1.0f;
    output_conditioner.dither_rng = time_us_32() ^ 0xC001C0DEu;

    pio_sm_set_enabled(pio, sm_clk, false);
    pio_sm_set_enabled(pio, sm_tx, false);
    pio_sm_set_enabled(pio, sm_rx, false);

    pio_sm_clear_fifos(pio, sm_tx);
    pio_sm_clear_fifos(pio, sm_rx);

    pio_sm_restart(pio, sm_clk);
    pio_sm_restart(pio, sm_tx);
    pio_sm_restart(pio, sm_rx);

    start_rx_dma_to_buffer(0);
    start_tx_dma_from_buffer(tx_silence_buf);
    i2s_start_pio();
}

// ============================================================================
// DSP por bloco
// ============================================================================

static void process_block(int block_index) {
    for (int i = 0; i < PERIOD; i++) {
        dsp_in_l[i] = pcm24_to_float(rx_dma_buf[block_index][2 * i + 0]);
        dsp_in_r[i] = pcm24_to_float(rx_dma_buf[block_index][2 * i + 1]);
    }

    univibe.out(dsp_in_l, dsp_in_r);

    for (int i = 0; i < PERIOD; i++) {
        const float out_l = condition_output_sample(output_conditioner, dsp_out_l[i], output_conditioner.dc_l);
        const float out_r = condition_output_sample(output_conditioner, dsp_out_r[i], output_conditioner.dc_r);
        tx_dma_buf[block_index][2 * i + 0] = float_to_pcm24(out_l, output_conditioner.dither_rng);
        tx_dma_buf[block_index][2 * i + 1] = float_to_pcm24(out_r, output_conditioner.dither_rng);
    }

    __dmb();
    tx_block_ready[block_index] = true;
}

// ============================================================================
// main
// ============================================================================

int main() {
    if (!setup_audio_sys_clock()) {
        while (true) {
            tight_loop_contents();
        }
    }

    stdio_init_all();
    sleep_ms(80);

    univibe.reseed(time_us_32());

    setup_mclk(PIN_MCLK);
    sleep_ms(20);

    i2s_init_pio();
    dma_audio_init();

    univibe.mode_chorus = true;
#if USER_INTERFACE
    vibe_ui.init(&univibe);
#endif

    audio_start();

    absolute_time_t last_report = get_absolute_time();

    while (true) {
#if USER_INTERFACE
        vibe_ui.poll();
#endif

        if (rx_block_ready[0]) {
            __dmb();
            rx_block_ready[0] = false;
            process_block(0);
        } else if (rx_block_ready[1]) {
            __dmb();
            rx_block_ready[1] = false;
            process_block(1);
        } else {
            tight_loop_contents();
        }

        if (absolute_time_diff_us(last_report, get_absolute_time()) > 1000000) {
            last_report = get_absolute_time();
            printf("RX overruns=%lu  TX underruns=%lu\n",
                   (unsigned long)rx_overruns,
                   (unsigned long)tx_underruns);
        }
    }
}
