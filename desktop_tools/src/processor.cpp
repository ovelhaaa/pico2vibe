#include "processor.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <stdexcept>

#define private public
#include "vibe_core.generated.hpp"
#undef private

namespace {

#if !defined(NDEBUG)
inline void debug_assert_finite(float v, const char* label) {
    (void)label;
    assert(std::isfinite(v) && "Vibe state NaN/Inf detected");
}

void debug_assert_vibe_state_finite(const Vibe* vibe) {
    debug_assert_finite(vibe->fbl, "fbl");
    debug_assert_finite(vibe->fbr, "fbr");
    debug_assert_finite(vibe->lamp_state_l, "lamp_state_l");
    debug_assert_finite(vibe->lamp_state_r, "lamp_state_r");
    for (int i = 0; i < 8; ++i) {
        debug_assert_finite(vibe->stage[i].oldcvolt, "stage.oldcvolt");
        debug_assert_finite(vibe->stage[i].vc.x1, "stage.vc.x1");
        debug_assert_finite(vibe->stage[i].vc.y1, "stage.vc.y1");
        debug_assert_finite(vibe->stage[i].vcvo.x1, "stage.vcvo.x1");
        debug_assert_finite(vibe->stage[i].vcvo.y1, "stage.vcvo.y1");
        debug_assert_finite(vibe->stage[i].ecvc.x1, "stage.ecvc.x1");
        debug_assert_finite(vibe->stage[i].ecvc.y1, "stage.ecvc.y1");
        debug_assert_finite(vibe->stage[i].vevo.x1, "stage.vevo.x1");
        debug_assert_finite(vibe->stage[i].vevo.y1, "stage.vevo.y1");
    }
}
#else
inline void debug_assert_vibe_state_finite(const Vibe*) {}
#endif

float clamp_finite(float v, float lo, float hi, float fallback = 0.0f) {
    if (!std::isfinite(v)) {
        return fallback;
    }
    return std::max(lo, std::min(hi, v));
}

AudioMetrics compute_metrics(const std::vector<float>& left, const std::vector<float>& right) {
    AudioMetrics m{};
    if (left.empty()) {
        return m;
    }

    long double sum_sq = 0.0;
    long double sum = 0.0;
    for (size_t i = 0; i < left.size(); ++i) {
        const float mono = 0.5f * (left[i] + right[i]);
        const float abs_mono = std::fabs(mono);
        if (abs_mono > m.peak) m.peak = abs_mono;
        if (abs_mono >= 0.999f) m.clipping_count++;
        sum_sq += static_cast<long double>(mono) * static_cast<long double>(mono);
        sum += mono;
    }

    m.rms = static_cast<float>(std::sqrt(sum_sq / static_cast<long double>(left.size())));
    m.dc_offset = static_cast<float>(sum / static_cast<long double>(left.size()));
    return m;
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

VibeVoicing map_preset(UnivibeParams::Preset p) {
    switch (p) {
        case UnivibeParams::Preset::classic_vibrato: return VibeVoicing::ClassicVibrato;
        case UnivibeParams::Preset::deep_throb: return VibeVoicing::DeepThrob;
        case UnivibeParams::Preset::modern_wide: return VibeVoicing::ModernWide;
        case UnivibeParams::Preset::classic_chorus:
        default: return VibeVoicing::ClassicChorus;
    }
}

}  // namespace

struct DesktopUnivibeProcessor::Impl {
    std::array<float, PERIOD> in_l{};
    std::array<float, PERIOD> in_r{};
    std::array<float, PERIOD> out_l{};
    std::array<float, PERIOD> out_r{};
    std::array<float, PERIOD> diff_l{};
    std::array<float, PERIOD> diff_r{};
    Vibe* improved = nullptr;
    Vibe* legacy = nullptr;
    UnivibeParams user{};
    AudioMetrics metrics{};

    explicit Impl(const UnivibeParams& p) : user(p) {
        std::srand(p.seed);
        improved = new Vibe(out_l.data(), out_r.data());
        improved->reseed(p.seed);
        improved->set_voicing(map_preset(p.preset));
        improved->set_param(VibeParamId::Depth, p.depth);
        improved->set_param(VibeParamId::Feedback, p.feedback);
        improved->set_param(VibeParamId::Mix, p.mix);
        improved->set_param(VibeParamId::LfoRateHz, p.rate_hz);
        improved->set_param(VibeParamId::InputDrive, p.input_drive);
        improved->set_param(VibeParamId::OutputGain, p.output_gain);
        improved->set_param(VibeParamId::ToneTilt, p.tone_tilt);
        improved->set_param(VibeParamId::PreHpfHz, p.pre_hpf_hz);
        improved->set_param(VibeParamId::LfoSkew, p.lfo_skew);
        improved->set_param(VibeParamId::SatAsymmetry, p.sat_asymmetry);
        improved->set_param(VibeParamId::SatOutTrim, p.sat_out_trim);
        improved->mode_chorus = p.mode_chorus;

        if (p.engine_mode == UnivibeParams::EngineMode::legacy || p.compare_mode == UnivibeParams::CompareMode::difference) {
            legacy = new Vibe(diff_l.data(), diff_r.data());
            legacy->reseed(p.seed);
            legacy->set_voicing(VibeVoicing::ClassicChorus);
            legacy->params.lfo_shape = LfoShape::Legacy;
            legacy->params.feedback_color = FeedbackColor::Flat;
            legacy->params.legacy_saturation = true;
            legacy->set_param(VibeParamId::Depth, p.depth);
            legacy->set_param(VibeParamId::Feedback, p.feedback);
            legacy->set_param(VibeParamId::Mix, p.mix);
            legacy->set_param(VibeParamId::LfoRateHz, p.rate_hz);
            legacy->set_param(VibeParamId::ToneTilt, 0.0f);
            legacy->set_param(VibeParamId::PreHpfHz, 8.0f);
            legacy->set_param(VibeParamId::SatAsymmetry, 0.0f);
            legacy->set_param(VibeParamId::SatOutTrim, 1.0f);
            legacy->mode_chorus = p.mode_chorus;
        }
    }

    ~Impl() {
        delete improved;
        delete legacy;
    }
};

DesktopUnivibeProcessor::DesktopUnivibeProcessor(const UnivibeParams& params) {
    if (params.rate_hz < 0.01f || params.rate_hz > 20.0f) {
        throw std::runtime_error("rate_hz fora do intervalo recomendado (0.01..20)");
    }
    if (params.depth < 0.0f || params.depth > 1.0f) {
        throw std::runtime_error("depth fora do intervalo [0..1]");
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

        std::fill(impl_->in_l.begin(), impl_->in_l.end(), 0.0f);
        std::fill(impl_->in_r.begin(), impl_->in_r.end(), 0.0f);

        for (size_t i = 0; i < take; ++i) {
            impl_->in_l[i] = left[pos + i];
            impl_->in_r[i] = right[pos + i];
        }

        if (impl_->user.engine_mode == UnivibeParams::EngineMode::legacy && impl_->legacy) {
            impl_->legacy->out(impl_->in_l.data(), impl_->in_r.data());
            debug_assert_vibe_state_finite(impl_->legacy);
            sanitize_vibe_state(impl_->legacy);
            for (size_t i = 0; i < take; ++i) {
                left[pos + i] = impl_->diff_l[i];
                right[pos + i] = impl_->diff_r[i];
            }
        } else {
            impl_->improved->out(impl_->in_l.data(), impl_->in_r.data());
            debug_assert_vibe_state_finite(impl_->improved);
            sanitize_vibe_state(impl_->improved);

            if (impl_->user.compare_mode == UnivibeParams::CompareMode::difference && impl_->legacy) {
                impl_->legacy->out(impl_->in_l.data(), impl_->in_r.data());
                debug_assert_vibe_state_finite(impl_->legacy);
                sanitize_vibe_state(impl_->legacy);
                for (size_t i = 0; i < take; ++i) {
                    left[pos + i] = impl_->out_l[i] - impl_->diff_l[i];
                    right[pos + i] = impl_->out_r[i] - impl_->diff_r[i];
                }
            } else {
                for (size_t i = 0; i < take; ++i) {
                    left[pos + i] = impl_->out_l[i];
                    right[pos + i] = impl_->out_r[i];
                }
            }
        }

        pos += take;
    }

    impl_->metrics = compute_metrics(left, right);
}

AudioMetrics DesktopUnivibeProcessor::last_metrics() const {
    return impl_->metrics;
}
