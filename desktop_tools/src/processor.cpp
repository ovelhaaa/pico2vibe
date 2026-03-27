#include "processor.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

#define private public
#include "vibe_core.generated.hpp"
#undef private

namespace {

float clamp_finite(float v, float lo, float hi, float fallback = 0.0f) {
    if (!std::isfinite(v)) {
        return fallback;
    }
    return std::max(lo, std::min(hi, v));
}

void sanitize_vibe_state(Vibe* vibe) {
    vibe->fbl = clamp_finite(vibe->fbl, -0.92f, 0.92f, 0.0f);
    vibe->fbr = clamp_finite(vibe->fbr, -0.92f, 0.92f, 0.0f);

    vibe->lamp_state_l = clamp_finite(vibe->lamp_state_l, 0.0f, 1.0f, 0.0f);
    vibe->lamp_state_r = clamp_finite(vibe->lamp_state_r, 0.0f, 1.0f, 0.0f);

    for (int i = 0; i < 8; ++i) {
        if (!std::isfinite(vibe->stage[i].oldcvolt)) vibe->stage[i].oldcvolt = 0.0f;

        if (!std::isfinite(vibe->stage[i].vc.x1)) vibe->stage[i].vc.x1 = 0.0f;
        if (!std::isfinite(vibe->stage[i].vc.y1)) vibe->stage[i].vc.y1 = 0.0f;
        if (!std::isfinite(vibe->stage[i].vcvo.x1)) vibe->stage[i].vcvo.x1 = 0.0f;
        if (!std::isfinite(vibe->stage[i].vcvo.y1)) vibe->stage[i].vcvo.y1 = 0.0f;
        if (!std::isfinite(vibe->stage[i].ecvc.x1)) vibe->stage[i].ecvc.x1 = 0.0f;
        if (!std::isfinite(vibe->stage[i].ecvc.y1)) vibe->stage[i].ecvc.y1 = 0.0f;
        if (!std::isfinite(vibe->stage[i].vevo.x1)) vibe->stage[i].vevo.x1 = 0.0f;
        if (!std::isfinite(vibe->stage[i].vevo.y1)) vibe->stage[i].vevo.y1 = 0.0f;
    }
}

}  // namespace

struct DesktopUnivibeProcessor::Impl {
    std::array<float, PERIOD> in_l{};
    std::array<float, PERIOD> in_r{};
    std::array<float, PERIOD> out_l{};
    std::array<float, PERIOD> out_r{};
    Vibe* vibe = nullptr;
    float mix = 1.0f;
    bool user_chorus = true;

    explicit Impl(const UnivibeParams& p) {
        std::srand(p.seed);
        vibe = new Vibe(out_l.data(), out_r.data());

        // We always run the core in vibrato mode (wet only) and do the dry/wet
        // blend in the host wrapper. This avoids double dry-path mixing.
        vibe->mode_chorus = false;
        vibe->fdepth = p.depth;
        vibe->fwidth = p.width;
        vibe->fb = p.feedback;
        vibe->lfo.setFreq(p.rate_hz);
        mix = p.mix;
        user_chorus = p.mode_chorus;
    }

    ~Impl() {
        delete vibe;
        vibe = nullptr;
    }
};

DesktopUnivibeProcessor::DesktopUnivibeProcessor(const UnivibeParams& params) {
    if (params.rate_hz < 0.01f || params.rate_hz > 20.0f) {
        throw std::runtime_error("rate_hz fora do intervalo recomendado (0.01..20)");
    }
    if (params.depth < 0.0f || params.depth > 1.0f) {
        throw std::runtime_error("depth fora do intervalo [0..1]");
    }
    if (params.width < 0.0f || params.width > 1.0f) {
        throw std::runtime_error("width fora do intervalo [0..1]");
    }
    if (params.feedback < 0.0f || params.feedback > 0.99f) {
        throw std::runtime_error("feedback fora do intervalo [0..0.99]");
    }
    if (params.mix < 0.0f || params.mix > 1.0f) {
        throw std::runtime_error("mix fora do intervalo [0..1]");
    }

    impl_ = new Impl(params);
}

DesktopUnivibeProcessor::~DesktopUnivibeProcessor() {
    delete impl_;
    impl_ = nullptr;
}

void DesktopUnivibeProcessor::process_in_place(std::vector<float>& left, std::vector<float>& right) {
    if (left.size() != right.size()) {
        throw std::runtime_error("left/right com tamanhos diferentes");
    }

    const size_t n = left.size();
    size_t pos = 0;

    while (pos < n) {
        const size_t remain = n - pos;
        const size_t take = std::min(remain, static_cast<size_t>(PERIOD));

        for (size_t i = 0; i < static_cast<size_t>(PERIOD); ++i) {
            impl_->in_l[i] = 0.0f;
            impl_->in_r[i] = 0.0f;
        }

        for (size_t i = 0; i < take; ++i) {
            impl_->in_l[i] = left[pos + i];
            impl_->in_r[i] = right[pos + i];
        }

        impl_->vibe->out(impl_->in_l.data(), impl_->in_r.data());
        sanitize_vibe_state(impl_->vibe);

        const float dry = impl_->user_chorus ? (1.0f - impl_->mix) : 0.0f;
        const float wet = impl_->user_chorus ? impl_->mix : 1.0f;

        for (size_t i = 0; i < take; ++i) {
            float wl = impl_->out_l[i];
            float wr = impl_->out_r[i];
            if (!std::isfinite(wl)) wl = 0.0f;
            if (!std::isfinite(wr)) wr = 0.0f;

            float out_l = impl_->in_l[i] * dry + wl * wet;
            float out_r = impl_->in_r[i] * dry + wr * wet;
            if (!std::isfinite(out_l)) out_l = 0.0f;
            if (!std::isfinite(out_r)) out_r = 0.0f;

            left[pos + i] = out_l;
            right[pos + i] = out_r;
        }

        pos += take;
    }
}
