#include "processor.hpp"
#include "wav_io.hpp"

#include <cctype>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

enum class WavFormat {
    pcm16,
    float32,
};

void print_usage() {
    std::cout
        << "Uso:\n"
        << "  univibe_cli --input <arquivo> --output <arquivo.wav> [opcoes]\n\n"
        << "Opcoes:\n"
        << "  --mode <chorus|vibrato>   (padrao: chorus)\n"
        << "  --rate <hz>               (padrao: 1.2)\n"
        << "  --depth <0..1>            (padrao: 0.6)\n"
        << "  --width <0..1>            (padrao: 0.8)\n"
        << "  --feedback <0..0.99>      (padrao: 0.45)\n"
        << "  --mix <0..1>              (padrao: 1.0)\n"
        << "  --seed <int>              (padrao: 1)\n"
        << "  --wav-format <pcm16|float32> (padrao: pcm16)\n"
        << "  --help\n";
}

std::string quote(const std::string& s) {
    return "\"" + s + "\"";
}

bool is_wav_path(const fs::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".wav";
}

std::string ensure_wav_input(const std::string& input_path, fs::path& temp_wav) {
    const fs::path in(input_path);
    if (is_wav_path(in)) {
        return input_path;
    }

    temp_wav = fs::temp_directory_path() / ("univibe_in_" + std::to_string(std::time(nullptr)) + ".wav");
    const std::string cmd =
        "ffmpeg -y -hide_banner -loglevel error -i " + quote(input_path) +
        " -ar 44100 -ac 2 " + quote(temp_wav.string());

    if (std::system(cmd.c_str()) != 0) {
        throw std::runtime_error("Falha convertendo entrada para WAV com ffmpeg");
    }
    return temp_wav.string();
}

void maybe_convert_to_non_wav(const fs::path& out_path, const fs::path& wav_path) {
    if (is_wav_path(out_path)) {
        return;
    }

    const std::string cmd =
        "ffmpeg -y -hide_banner -loglevel error -i " + quote(wav_path.string()) + " " + quote(out_path.string());

    if (std::system(cmd.c_str()) != 0) {
        throw std::runtime_error("Falha convertendo saida WAV para formato final com ffmpeg");
    }
}

}  // namespace

int main(int argc, char** argv) {
    fs::path temp_in_wav;
    fs::path temp_out_wav;

    try {
        if (argc <= 1) {
            print_usage();
            return 1;
        }

        std::string input;
        std::string output;
        UnivibeParams params;
        WavFormat wav_format = WavFormat::pcm16;

        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            auto next = [&]() -> std::string {
                if (i + 1 >= argc) {
                    throw std::runtime_error("Faltou valor para argumento: " + arg);
                }
                return std::string(argv[++i]);
            };

            if (arg == "--help") {
                print_usage();
                return 0;
            }
            if (arg == "--input") {
                input = next();
            } else if (arg == "--output") {
                output = next();
            } else if (arg == "--mode") {
                const std::string mode = next();
                if (mode == "chorus") params.mode_chorus = true;
                else if (mode == "vibrato") params.mode_chorus = false;
                else throw std::runtime_error("Mode invalido: use chorus ou vibrato");
            } else if (arg == "--rate") {
                params.rate_hz = std::stof(next());
            } else if (arg == "--depth") {
                params.depth = std::stof(next());
            } else if (arg == "--width") {
                params.width = std::stof(next());
            } else if (arg == "--feedback") {
                params.feedback = std::stof(next());
            } else if (arg == "--mix") {
                params.mix = std::stof(next());
            } else if (arg == "--seed") {
                params.seed = static_cast<uint32_t>(std::stoul(next()));
            } else if (arg == "--wav-format") {
                const std::string fmt = next();
                if (fmt == "pcm16") wav_format = WavFormat::pcm16;
                else if (fmt == "float32") wav_format = WavFormat::float32;
                else throw std::runtime_error("wav-format invalido: use pcm16 ou float32");
            } else {
                throw std::runtime_error("Argumento desconhecido: " + arg);
            }
        }

        if (input.empty() || output.empty()) {
            throw std::runtime_error("--input e --output sao obrigatorios");
        }

        const std::string actual_input_wav = ensure_wav_input(input, temp_in_wav);

        StereoBuffer audio = read_wav_file(actual_input_wav);
        if (audio.sample_rate != 44100u) {
            throw std::runtime_error("Amostragem suportada para equivalencia com Pico: 44100 Hz");
        }

        DesktopUnivibeProcessor processor(params);
        processor.process_in_place(audio.left, audio.right);

        const fs::path out_path(output);
        fs::path out_wav_path = out_path;
        if (!is_wav_path(out_path)) {
            temp_out_wav = fs::temp_directory_path() / ("univibe_out_" + std::to_string(std::time(nullptr)) + ".wav");
            out_wav_path = temp_out_wav;
        }

        if (wav_format == WavFormat::pcm16) {
            write_wav_file_pcm16(out_wav_path.string(), audio);
        } else {
            write_wav_file_float32(out_wav_path.string(), audio);
        }

        maybe_convert_to_non_wav(out_path, out_wav_path);

        std::cout << "Processamento concluido: " << output << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Erro: " << e.what() << "\n";
        if (!temp_in_wav.empty()) {
            std::error_code ec;
            fs::remove(temp_in_wav, ec);
        }
        if (!temp_out_wav.empty()) {
            std::error_code ec;
            fs::remove(temp_out_wav, ec);
        }
        return 2;
    }

    if (!temp_in_wav.empty()) {
        std::error_code ec;
        fs::remove(temp_in_wav, ec);
    }
    if (!temp_out_wav.empty()) {
        std::error_code ec;
        fs::remove(temp_out_wav, ec);
    }

    return 0;
}