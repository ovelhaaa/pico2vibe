// Auto-generated from univibe_rp2350_dma.cpp. Do not edit manually.
#pragma once

#include <cmath>
#include <cstdlib>

#define SAMPLE_RATE     44100.0f
#define SAMPLE_RATE_HZ  44100u
#define SLOT_BITS       32u
#define CHANNELS        2u
#define PERIOD          32
#define DMA_WORDS_PER_BLOCK   (PERIOD * CHANNELS)

#define DENORMAL_GUARD  1e-18f
#define cSAMPLE_RATE    (1.0f / SAMPLE_RATE)
#define fSAMPLE_RATE    SAMPLE_RATE
#define fPERIOD         ((float)PERIOD)

#define AUDIO_SYS_CLOCK_HZ 135475200u

static inline float fast_soft_clip(float x) {
    return x / (1.0f + fabsf(x));
}

// ============================================================================
// LFO
// ============================================================================

class EffectLFO {
private:
    float phase = 0.0f;
    float freq = 1.2f;
    float state_l = 0.0f;
    float state_r = 0.0f;
    float smoothing = 0.2f;

public:
    void setFreq(float f) { freq = f; }

    void processBlock(float *l, float *r) {
        float drift = 1.0f + 0.015f * (((float)rand() / (float)RAND_MAX) - 0.5f);
        phase += freq * drift * cSAMPLE_RATE * fPERIOD;
        if (phase >= 1.0f) phase -= 1.0f;

        float tri_l = (phase < 0.4f) ? (phase * 2.5f) : (1.0f - (phase - 0.4f) * 1.6666f);

        float p_r = phase + 0.25f;
        if (p_r >= 1.0f) p_r -= 1.0f;
        float tri_r = (p_r < 0.4f) ? (p_r * 2.5f) : (1.0f - (p_r - 0.4f) * 1.6666f);

        state_l += smoothing * (tri_l - state_l);
        state_r += smoothing * (tri_r - state_r);

        *l = state_l;
        *r = state_r;
    }
};

struct fparams {
    float x1 = 0.0f, y1 = 0.0f, n0 = 0.0f, n1 = 0.0f, d0 = 0.0f, d1 = 0.0f;
};

struct PhaseStage {
    fparams vc, vcvo, ecvc, vevo;
    float oldcvolt = 0.0f;
    float ldr_mismatch = 1.0f;
};

// ============================================================================
// Univibe
// ============================================================================

class Vibe {
public:
    Vibe(float *efxoutl_, float *efxoutr_);
    void out(float *smpsl, float *smpsr);
    void init_vibes();

    bool mode_chorus = true;

private:
    float *efxoutl;
    float *efxoutr;
    float fwidth = 0.8f;
    float fdepth = 0.6f;
    float lpanning = 1.0f, rpanning = 1.0f;
    float fb = 0.45f;

    EffectLFO lfo;

    float lamp_state_l = 0.0f, lamp_state_r = 0.0f;
    float lamp_attack, lamp_release;

    PhaseStage stage[8];

    float fbr = 0.0f, fbl = 0.0f;
    float gain_bjt = 0.0f, k = 0.0f, R1 = 0.0f, C2 = 0.0f, C1[8] = {0}, beta = 0.0f;
    float cn0[8], cn1[8], cd0[8], cd1[8];
    float en0[8], en1[8], ed0[8], ed1[8];
    float ecn0[8], ecn1[8], ecd0[8], ecd1[8];
    float on0[8], on1[8], od0[8], od1[8];

    float vibefilter(float data, fparams *ftype);
    void modulate(float res_l, float res_r);
    float bjt_shape(float data);
};

Vibe::Vibe(float *efxoutl_, float *efxoutr_) : efxoutl(efxoutl_), efxoutr(efxoutr_) {
    float block_time = fPERIOD / fSAMPLE_RATE;
    lamp_attack = 1.0f - expf(-block_time / 0.010f);
    lamp_release = 1.0f - expf(-block_time / 0.040f);
    init_vibes();
}

float Vibe::vibefilter(float data, fparams *ftype) {
    float y0 = data * ftype->n0 + ftype->x1 * ftype->n1 - ftype->y1 * ftype->d1;
    ftype->y1 = y0 + DENORMAL_GUARD;
    ftype->x1 = data;
    return y0;
}

float Vibe::bjt_shape(float data) {
    float drive = 3.5f;
    float bjt_gain = 0.35f;
    return fast_soft_clip(data * drive) * bjt_gain;
}

void Vibe::init_vibes() {
    k = 2.0f * fSAMPLE_RATE;
    R1 = 4700.0f;
    C2 = 1e-6f;
    beta = 150.0f;
    gain_bjt = -beta / (beta + 1.0f);

    float base_C1[8] = {
        0.015e-6f, 0.22e-6f, 470e-12f, 0.0047e-6f,
        0.015e-6f, 0.22e-6f, 470e-12f, 0.0047e-6f
    };

    for (int i = 0; i < 8; i++) {
        float cap_var = 0.9f + 0.2f * ((float)rand() / (float)RAND_MAX);
        C1[i] = base_C1[i] * cap_var;
        stage[i].ldr_mismatch = 0.95f + 0.1f * ((float)rand() / (float)RAND_MAX);
        stage[i].oldcvolt = 0.0f;
        en1[i] = k * R1 * C1[i];
        en0[i] = 1.0f;
    }
}

void Vibe::modulate(float res_l, float res_r) {
    for (int i = 0; i < 8; i++) {
        float base_res = (i < 4) ? res_l : res_r;
        float currentRv = 4700.0f + (base_res * stage[i].ldr_mismatch);

        float R1pRv = R1 + currentRv;
        float C2pC1 = C2 + C1[i];

        float cd1_val  = k * R1pRv * C1[i];
        float cn1_val  = k * gain_bjt * currentRv * C1[i];
        float ecd1_val = k * cd1_val * C2 / C2pC1;
        float ecn1_val = k * gain_bjt * R1 * cd1_val * C2 / (currentRv * C2pC1);
        float on1_val  = k * currentRv * C2;

        float cd0_val  = 1.0f + C1[i] / C2;
        float ecd0_val = 1.0f;
        float od0_val  = 1.0f + C2 / C1[i];
        float ed0_val  = 1.0f + C1[i] / C2;
        float cn0_val  = gain_bjt * (1.0f + C1[i] / C2);
        float on0_val  = 1.0f;
        float ecn0_val = 0.0f;

        float tmp = 1.0f / (cd1_val + cd0_val);
        stage[i].vc.n1 = tmp * (cn0_val - cn1_val);
        stage[i].vc.n0 = tmp * (cn1_val + cn0_val);
        stage[i].vc.d1 = tmp * (cd0_val - cd1_val);

        tmp = 1.0f / (ecd1_val + ecd0_val);
        stage[i].ecvc.n1 = tmp * (ecn0_val - ecn1_val);
        stage[i].ecvc.n0 = tmp * (ecn1_val + ecn0_val);
        stage[i].ecvc.d1 = tmp * (ecd0_val - ecd1_val);

        tmp = 1.0f / (on1_val + od0_val);
        stage[i].vcvo.n1 = tmp * (on0_val - on1_val);
        stage[i].vcvo.n0 = tmp * (on1_val + on0_val);
        stage[i].vcvo.d1 = tmp * (od0_val - on1_val);

        float ed1_val = k * R1pRv * C1[i];
        tmp = 1.0f / (ed1_val + ed0_val);
        stage[i].vevo.n1 = tmp * (en0[i] - en1[i]);
        stage[i].vevo.n0 = tmp * (en1[i] + en0[i]);
        stage[i].vevo.d1 = tmp * (ed0_val - ed1_val);
    }
}

void Vibe::out(float *smpsl, float *smpsr) {
    float lfol, lfor;
    lfo.processBlock(&lfol, &lfor);

    float target_l = fminf(1.0f, fmaxf(0.0f, fdepth + lfol * fwidth));
    float target_r = fminf(1.0f, fmaxf(0.0f, fdepth + lfor * fwidth));

    if (target_l > lamp_state_l) lamp_state_l += lamp_attack * (target_l - lamp_state_l);
    else                         lamp_state_l += lamp_release * (target_l - lamp_state_l);

    if (target_r > lamp_state_r) lamp_state_r += lamp_attack * (target_r - lamp_state_r);
    else                         lamp_state_r += lamp_release * (target_r - lamp_state_r);

    float bright_l = lamp_state_l * sqrtf(lamp_state_l);
    float bright_r = lamp_state_r * sqrtf(lamp_state_r);

    float R_dark = 1000000.0f;
    float k_ldr  = 7.6009f;

    float res_l = R_dark * expf(-k_ldr * bright_l);
    float res_r = R_dark * expf(-k_ldr * bright_r);

    modulate(res_l, res_r);

    float emitterfb_l = 25.0f / (res_l / 500.0f);
    float emitterfb_r = 25.0f / (res_r / 500.0f);

    for (int i = 0; i < PERIOD; i++) {
        float dry_l = smpsl[i];
        float input = bjt_shape(fbl + dry_l);

        for (int j = 0; j < 4; j++) {
            float cvolt = vibefilter(input, &stage[j].ecvc) +
                          vibefilter(input + emitterfb_l * stage[j].oldcvolt, &stage[j].vc);
            float ocvolt = vibefilter(cvolt, &stage[j].vcvo);
            stage[j].oldcvolt = ocvolt;
            input = bjt_shape(ocvolt + vibefilter(input, &stage[j].vevo));
        }

        fbl = fast_soft_clip(stage[3].oldcvolt * fb);
        efxoutl[i] = mode_chorus ? lpanning * (dry_l + input) * 0.5f : lpanning * input;

        float dry_r = smpsr[i];
        input = bjt_shape(fbr + dry_r);

        for (int j = 4; j < 8; j++) {
            float cvolt = vibefilter(input, &stage[j].ecvc) +
                          vibefilter(input + emitterfb_r * stage[j].oldcvolt, &stage[j].vc);
            float ocvolt = vibefilter(cvolt, &stage[j].vcvo);
            stage[j].oldcvolt = ocvolt;
            input = bjt_shape(ocvolt + vibefilter(input, &stage[j].vevo));
        }

        fbr = fast_soft_clip(stage[7].oldcvolt * fb);
        efxoutr[i] = mode_chorus ? rpanning * (dry_r + input) * 0.5f : rpanning * input;
    }
}
