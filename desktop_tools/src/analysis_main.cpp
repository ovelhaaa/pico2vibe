#include "processor.hpp"
#include "wav_io.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr uint32_t kSampleRate = 44100;
constexpr double kPi = 3.14159265358979323846;

struct RunConfig {
    fs::path out_dir = "analysis_out";
    std::vector<std::string> presets{"classic"};
    std::vector<float> sine_levels_db{-24.0f, -18.0f, -12.0f, -6.0f, 0.0f};
    float sweep_seconds = 8.0f;
    float guitar_seconds = 4.0f;
    float sine_seconds = 2.0f;
    float impulse_seconds = 1.0f;
    fs::path compare_to;
};

struct FreqPoint {
    float time_s = 0.0f;
    float freq_hz = 0.0f;
    float gain_db = 0.0f;
};

void usage() {
    std::cout
        << "Uso:\n"
        << "  dsp_validate [opcoes]\n\n"
        << "Opcoes:\n"
        << "  --out-dir <pasta>              Pasta de saida (padrao: analysis_out)\n"
        << "  --preset <nome>                Pode repetir. Presets: classic, subtle, deep, vibrato\n"
        << "  --levels-db <lista>            Ex: -24,-18,-12,-6,0\n"
        << "  --sweep-seconds <seg>          Duracao do sweep (padrao: 8)\n"
        << "  --compare-to <pasta>           Pasta de baseline para gerar diff de summary\n"
        << "  --help\n\n"
        << "Exemplo:\n"
        << "  dsp_validate --out-dir out/new --preset classic --preset deep\\n"
        << "               --compare-to out/old\n";
}

std::vector<float> parse_list(const std::string& csv) {
    std::vector<float> out;
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (!item.empty()) out.push_back(std::stof(item));
    }
    if (out.empty()) throw std::runtime_error("Lista vazia em --levels-db");
    return out;
}

UnivibeParams preset_params(const std::string& name) {
    UnivibeParams p;
    if (name == "classic") {
        p.mode_chorus = true;
        p.rate_hz = 1.2f;
        p.depth = 0.6f;
        p.width = 0.8f;
        p.feedback = 0.45f;
        p.mix = 1.0f;
    } else if (name == "subtle") {
        p.mode_chorus = true;
        p.rate_hz = 0.75f;
        p.depth = 0.35f;
        p.width = 0.55f;
        p.feedback = 0.2f;
        p.mix = 0.65f;
    } else if (name == "deep") {
        p.mode_chorus = true;
        p.rate_hz = 1.8f;
        p.depth = 0.95f;
        p.width = 1.0f;
        p.feedback = 0.6f;
        p.mix = 1.0f;
    } else if (name == "vibrato") {
        p.mode_chorus = false;
        p.rate_hz = 4.0f;
        p.depth = 0.8f;
        p.width = 0.85f;
        p.feedback = 0.3f;
        p.mix = 1.0f;
    } else {
        throw std::runtime_error("Preset desconhecido: " + name);
    }
    return p;
}

float db_to_gain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

StereoBuffer make_buffer(size_t n) {
    StereoBuffer b;
    b.sample_rate = kSampleRate;
    b.left.assign(n, 0.0f);
    b.right.assign(n, 0.0f);
    return b;
}

StereoBuffer generate_impulse(float seconds) {
    size_t n = static_cast<size_t>(std::max(0.1f, seconds) * static_cast<float>(kSampleRate));
    StereoBuffer b = make_buffer(n);
    b.left[0] = 1.0f;
    b.right[0] = 1.0f;
    return b;
}

StereoBuffer generate_sine(float freq_hz, float level_db, float seconds) {
    size_t n = static_cast<size_t>(std::max(0.1f, seconds) * static_cast<float>(kSampleRate));
    StereoBuffer b = make_buffer(n);
    const float amp = db_to_gain(level_db);
    const double w = 2.0 * kPi * static_cast<double>(freq_hz) / static_cast<double>(kSampleRate);
    for (size_t i = 0; i < n; ++i) {
        float s = amp * static_cast<float>(std::sin(w * static_cast<double>(i)));
        b.left[i] = s;
        b.right[i] = s;
    }
    return b;
}

StereoBuffer generate_log_sweep(float f0, float f1, float seconds) {
    size_t n = static_cast<size_t>(std::max(0.1f, seconds) * static_cast<float>(kSampleRate));
    StereoBuffer b = make_buffer(n);
    const double T = static_cast<double>(seconds);
    const double ratio = std::log(static_cast<double>(f1) / static_cast<double>(f0));
    const double K = (2.0 * kPi * static_cast<double>(f0) * T) / ratio;

    for (size_t i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(kSampleRate);
        const double phase = K * (std::exp((t / T) * ratio) - 1.0);
        float s = 0.8f * static_cast<float>(std::sin(phase));
        b.left[i] = s;
        b.right[i] = s;
    }
    return b;
}

StereoBuffer generate_guitar_like(float seconds) {
    size_t n = static_cast<size_t>(std::max(0.1f, seconds) * static_cast<float>(kSampleRate));
    StereoBuffer b = make_buffer(n);

    const float f0 = 110.0f;
    for (size_t i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSampleRate);
        const float atk = 1.0f - std::exp(-80.0f * t);
        const float dec = std::exp(-3.5f * t);
        const float env = atk * dec;
        const float vibrato = 1.0f + 0.0025f * std::sin(2.0 * kPi * 5.2 * static_cast<double>(t));

        float s = 0.0f;
        for (int h = 1; h <= 8; ++h) {
            const float amp = 0.7f / static_cast<float>(h);
            const float freq = f0 * static_cast<float>(h) * vibrato * (1.0f + 0.0008f * static_cast<float>(h));
            s += amp * std::sin(static_cast<float>(2.0 * kPi) * freq * t);
        }
        s += 0.05f * std::sin(static_cast<float>(2.0 * kPi) * 3421.0f * t) * std::exp(-20.0f * t);
        s *= env;
        s = std::max(-0.95f, std::min(0.95f, s));
        b.left[i] = s;
        b.right[i] = s;
    }
    return b;
}

void process(UnivibeParams p, StereoBuffer& b) {
    DesktopUnivibeProcessor proc(p);
    proc.process_in_place(b.left, b.right);
}

float rms_window(const std::vector<float>& x, int center, int radius) {
    if (x.empty()) return 0.0f;
    int start = std::max(0, center - radius);
    int end = std::min(static_cast<int>(x.size()), center + radius + 1);
    if (start >= end) return 0.0f;
    double acc = 0.0;
    for (int i = start; i < end; ++i) {
        double v = static_cast<double>(x[static_cast<size_t>(i)]);
        acc += v * v;
    }
    return static_cast<float>(std::sqrt(acc / static_cast<double>(end - start)));
}

float goertzel_mag(const std::vector<float>& x, float freq_hz) {
    const int N = static_cast<int>(x.size());
    if (N <= 0) return 0.0f;
    const float kf = 0.5f + (static_cast<float>(N) * freq_hz / static_cast<float>(kSampleRate));
    const int k = static_cast<int>(kf);
    const float omega = 2.0f * static_cast<float>(kPi) * static_cast<float>(k) / static_cast<float>(N);
    const float coeff = 2.0f * std::cos(omega);

    float q0 = 0.0f, q1 = 0.0f, q2 = 0.0f;
    for (float s : x) {
        q0 = coeff * q1 - q2 + s;
        q2 = q1;
        q1 = q0;
    }
    const float power = q1 * q1 + q2 * q2 - coeff * q1 * q2;
    return std::sqrt(std::max(0.0f, power));
}

float thd_ratio(const std::vector<float>& x, float f0, int harmonics) {
    const float fundamental = goertzel_mag(x, f0);
    if (fundamental <= 1e-9f) return 0.0f;

    double harm_sq = 0.0;
    for (int h = 2; h <= harmonics; ++h) {
        const float f = f0 * static_cast<float>(h);
        if (f >= (kSampleRate * 0.5f)) break;
        const float m = goertzel_mag(x, f);
        harm_sq += static_cast<double>(m) * static_cast<double>(m);
    }
    return static_cast<float>(std::sqrt(harm_sq) / fundamental);
}

size_t next_pow2(size_t n) {
    size_t p = 1;
    while (p < n) p <<= 1;
    return p;
}

void fft_inplace(std::vector<std::complex<float>>& a) {
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }

    for (size_t len = 2; len <= n; len <<= 1) {
        const float ang = -2.0f * static_cast<float>(kPi) / static_cast<float>(len);
        std::complex<float> wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (size_t j = 0; j < len / 2; ++j) {
                std::complex<float> u = a[i + j];
                std::complex<float> v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

float high_freq_ratio_db(const std::vector<float>& x, float split_hz) {
    const size_t N = next_pow2(std::min<size_t>(x.size(), 65536));
    if (N < 1024) return -120.0f;

    std::vector<std::complex<float>> a(N, {0.0f, 0.0f});
    for (size_t i = 0; i < N; ++i) {
        const float w = 0.5f - 0.5f * std::cos(static_cast<float>(2.0 * kPi * static_cast<double>(i) / static_cast<double>(N - 1)));
        a[i] = std::complex<float>(x[i] * w, 0.0f);
    }
    fft_inplace(a);

    const size_t split_bin = std::min(N / 2, static_cast<size_t>(std::floor(split_hz * static_cast<float>(N) / static_cast<float>(kSampleRate))));
    double low = 0.0;
    double high = 0.0;
    for (size_t k = 1; k < N / 2; ++k) {
        const double p = std::norm(a[k]);
        if (k >= split_bin) high += p;
        else low += p;
    }
    const double total = low + high;
    if (total <= 1e-20) return -120.0f;
    return static_cast<float>(10.0 * std::log10(std::max(1e-20, high / total)));
}

std::vector<FreqPoint> frequency_response_from_sweep(const std::vector<float>& in,
                                                     const std::vector<float>& out,
                                                     float sweep_seconds,
                                                     float f0,
                                                     float f1,
                                                     int points = 96) {
    std::vector<FreqPoint> res;
    res.reserve(static_cast<size_t>(points));
    const double ratio = std::log(static_cast<double>(f1) / static_cast<double>(f0));
    const int radius = 1024;

    for (int i = 0; i < points; ++i) {
        const double alpha = (points <= 1) ? 0.0 : static_cast<double>(i) / static_cast<double>(points - 1);
        const float f = f0 * std::pow(f1 / f0, static_cast<float>(alpha));
        const double t = static_cast<double>(sweep_seconds) * (std::log(static_cast<double>(f) / static_cast<double>(f0)) / ratio);
        const int center = static_cast<int>(std::round(t * static_cast<double>(kSampleRate)));

        const float rin = rms_window(in, center, radius);
        const float rout = rms_window(out, center, radius);
        const float g = 20.0f * std::log10((rout + 1e-8f) / (rin + 1e-8f));
        res.push_back(FreqPoint{static_cast<float>(t), f, g});
    }
    return res;
}

std::vector<FreqPoint> notch_tracking_from_sweep(const std::vector<FreqPoint>& fr) {
    std::vector<FreqPoint> out;
    if (fr.size() < 3) return out;
    for (size_t i = 1; i + 1 < fr.size(); ++i) {
        if (fr[i].gain_db < fr[i - 1].gain_db && fr[i].gain_db < fr[i + 1].gain_db) {
            out.push_back(fr[i]);
        }
    }
    return out;
}

void write_csv_freq(const fs::path& path, const std::vector<FreqPoint>& rows, const std::string& header3) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Falha escrevendo CSV: " + path.string());
    out << "time_s,freq_hz," << header3 << "\n";
    out << std::fixed << std::setprecision(6);
    for (const auto& r : rows) {
        out << r.time_s << ',' << r.freq_hz << ',' << r.gain_db << "\n";
    }
}

void write_summary(const fs::path& path, const std::map<std::string, float>& kv) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("Falha escrevendo CSV: " + path.string());
    out << "metric,value\n";
    out << std::fixed << std::setprecision(6);
    for (const auto& it : kv) out << it.first << ',' << it.second << "\n";
}

std::map<std::string, float> read_summary(const fs::path& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("Falha lendo summary: " + path.string());
    std::map<std::string, float> kv;
    std::string line;
    std::getline(in, line);
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto p = line.find(',');
        if (p == std::string::npos) continue;
        kv[line.substr(0, p)] = std::stof(line.substr(p + 1));
    }
    return kv;
}

void write_summary_diff(const fs::path& baseline,
                        const fs::path& candidate,
                        const fs::path& out_path) {
    if (!fs::exists(baseline) || !fs::exists(candidate)) return;
    auto a = read_summary(baseline);
    auto b = read_summary(candidate);

    std::ofstream out(out_path);
    if (!out) throw std::runtime_error("Falha escrevendo diff CSV: " + out_path.string());
    out << "metric,baseline,candidate,delta,delta_pct\n";
    out << std::fixed << std::setprecision(6);

    for (const auto& kv : b) {
        const auto it = a.find(kv.first);
        if (it == a.end()) continue;
        const float base = it->second;
        const float cand = kv.second;
        const float delta = cand - base;
        const float pct = (std::fabs(base) > 1e-9f) ? (100.0f * delta / base) : 0.0f;
        out << kv.first << ',' << base << ',' << cand << ',' << delta << ',' << pct << "\n";
    }
}

void write_signal_pair(const fs::path& dir,
                       const std::string& base_name,
                       const StereoBuffer& in,
                       const StereoBuffer& out) {
    fs::create_directories(dir);
    write_wav_file_float32((dir / (base_name + "_in.wav")).string(), in);
    write_wav_file_float32((dir / (base_name + "_out.wav")).string(), out);
}

void run_preset(const std::string& preset_name, const RunConfig& cfg) {
    const UnivibeParams params = preset_params(preset_name);
    const fs::path root = cfg.out_dir / preset_name;
    const fs::path signal_dir = root / "signals";
    const fs::path metric_dir = root / "metrics";
    fs::create_directories(signal_dir);
    fs::create_directories(metric_dir);

    std::map<std::string, float> summary;

    auto impulse_in = generate_impulse(cfg.impulse_seconds);
    auto impulse_out = impulse_in;
    process(params, impulse_out);
    write_signal_pair(signal_dir, "impulse", impulse_in, impulse_out);

    std::ofstream thd_csv(metric_dir / "thd_vs_drive.csv");
    if (!thd_csv) throw std::runtime_error("Falha abrindo thd_vs_drive.csv");
    thd_csv << "sine_level_db,thd_ratio,thd_db,alias_proxy_hf_db\n";
    thd_csv << std::fixed << std::setprecision(6);

    float worst_thd_db = -120.0f;
    for (float level_db : cfg.sine_levels_db) {
        auto sine_in = generate_sine(440.0f, level_db, cfg.sine_seconds);
        auto sine_out = sine_in;
        process(params, sine_out);

        std::ostringstream name;
        name << "sine_" << std::showpos << static_cast<int>(std::round(level_db)) << "dB";
        write_signal_pair(signal_dir, name.str(), sine_in, sine_out);

        const float thd = thd_ratio(sine_out.left, 440.0f, 8);
        const float thd_db = 20.0f * std::log10(thd + 1e-12f);
        const float hf_db = high_freq_ratio_db(sine_out.left, 12000.0f);
        thd_csv << level_db << ',' << thd << ',' << thd_db << ',' << hf_db << "\n";
        worst_thd_db = std::max(worst_thd_db, thd_db);
    }

    auto sweep_in = generate_log_sweep(20.0f, 12000.0f, cfg.sweep_seconds);
    auto sweep_out = sweep_in;
    process(params, sweep_out);
    write_signal_pair(signal_dir, "log_sweep", sweep_in, sweep_out);

    const auto fr = frequency_response_from_sweep(sweep_in.left, sweep_out.left, cfg.sweep_seconds, 20.0f, 12000.0f, 120);
    write_csv_freq(metric_dir / "frequency_response.csv", fr, "gain_db");

    const auto notch = notch_tracking_from_sweep(fr);
    write_csv_freq(metric_dir / "notch_tracking.csv", notch, "notch_depth_db");

    auto guitar_in = generate_guitar_like(cfg.guitar_seconds);
    auto guitar_out = guitar_in;
    process(params, guitar_out);
    write_signal_pair(signal_dir, "guitar_like", guitar_in, guitar_out);

    summary["worst_thd_db"] = worst_thd_db;
    summary["guitar_alias_proxy_db"] = high_freq_ratio_db(guitar_out.left, 12000.0f);
    summary["sweep_alias_proxy_db"] = high_freq_ratio_db(sweep_out.left, 12000.0f);

    float notch_min_db = 0.0f;
    if (!notch.empty()) {
        notch_min_db = notch[0].gain_db;
        for (const auto& n : notch) notch_min_db = std::min(notch_min_db, n.gain_db);
    }
    summary["deepest_tracked_notch_db"] = notch_min_db;
    summary["tracked_notch_count"] = static_cast<float>(notch.size());

    write_summary(metric_dir / "summary.csv", summary);

    if (!cfg.compare_to.empty()) {
        write_summary_diff(cfg.compare_to / preset_name / "metrics" / "summary.csv",
                           metric_dir / "summary.csv",
                           metric_dir / "summary_vs_baseline.csv");
    }
}

RunConfig parse_args(int argc, char** argv) {
    RunConfig cfg;
    cfg.presets.clear();

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) throw std::runtime_error("Faltou valor para: " + arg);
            return std::string(argv[++i]);
        };

        if (arg == "--help") {
            usage();
            std::exit(0);
        } else if (arg == "--out-dir") {
            cfg.out_dir = next();
        } else if (arg == "--preset") {
            cfg.presets.push_back(next());
        } else if (arg == "--levels-db") {
            cfg.sine_levels_db = parse_list(next());
        } else if (arg == "--sweep-seconds") {
            cfg.sweep_seconds = std::stof(next());
        } else if (arg == "--compare-to") {
            cfg.compare_to = next();
        } else {
            throw std::runtime_error("Argumento desconhecido: " + arg);
        }
    }

    if (cfg.presets.empty()) cfg.presets.push_back("classic");
    return cfg;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        RunConfig cfg = parse_args(argc, argv);
        fs::create_directories(cfg.out_dir);

        std::cout << "Rodando harness offline (sr=" << kSampleRate << ") em: " << cfg.out_dir.string() << "\n";
        for (const auto& p : cfg.presets) {
            std::cout << "  preset: " << p << "\n";
            run_preset(p, cfg);
        }
        std::cout << "Concluido. Veja WAVs e CSVs em: " << cfg.out_dir.string() << "\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Erro: " << e.what() << "\n";
        return 2;
    }
}
