#include "wav_io.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr uint16_t WAVE_FORMAT_PCM = 1;
constexpr uint16_t WAVE_FORMAT_IEEE_FLOAT = 3;
constexpr uint16_t WAVE_FORMAT_EXTENSIBLE = 0xFFFE;

uint16_t read_u16_le(const std::vector<uint8_t>& data, size_t pos) {
    return static_cast<uint16_t>(data[pos] | (static_cast<uint16_t>(data[pos + 1]) << 8));
}

uint32_t read_u32_le(const std::vector<uint8_t>& data, size_t pos) {
    return static_cast<uint32_t>(data[pos]) |
           (static_cast<uint32_t>(data[pos + 1]) << 8) |
           (static_cast<uint32_t>(data[pos + 2]) << 16) |
           (static_cast<uint32_t>(data[pos + 3]) << 24);
}

void write_u16_le(std::ofstream& out, uint16_t v) {
    const char bytes[2] = {
        static_cast<char>(v & 0xff),
        static_cast<char>((v >> 8) & 0xff),
    };
    out.write(bytes, 2);
}

void write_u32_le(std::ofstream& out, uint32_t v) {
    const char bytes[4] = {
        static_cast<char>(v & 0xff),
        static_cast<char>((v >> 8) & 0xff),
        static_cast<char>((v >> 16) & 0xff),
        static_cast<char>((v >> 24) & 0xff),
    };
    out.write(bytes, 4);
}

float decode_pcm_sample(const uint8_t* p, uint16_t bits_per_sample) {
    switch (bits_per_sample) {
        case 16: {
            const int16_t s = static_cast<int16_t>(p[0] | (static_cast<int16_t>(p[1]) << 8));
            return static_cast<float>(s) / 32768.0f;
        }
        case 24: {
            int32_t s = static_cast<int32_t>(p[0]) |
                        (static_cast<int32_t>(p[1]) << 8) |
                        (static_cast<int32_t>(p[2]) << 16);
            if (s & 0x800000) {
                s |= ~0xFFFFFF;
            }
            return static_cast<float>(s) / 8388608.0f;
        }
        case 32: {
            int32_t s = static_cast<int32_t>(p[0]) |
                        (static_cast<int32_t>(p[1]) << 8) |
                        (static_cast<int32_t>(p[2]) << 16) |
                        (static_cast<int32_t>(p[3]) << 24);
            return static_cast<float>(s) / 2147483648.0f;
        }
        default:
            throw std::runtime_error("PCM bits por sample nao suportado");
    }
}

float decode_float32_sample(const uint8_t* p) {
    union {
        uint32_t u;
        float f;
    } cvt{};
    cvt.u = static_cast<uint32_t>(p[0]) |
            (static_cast<uint32_t>(p[1]) << 8) |
            (static_cast<uint32_t>(p[2]) << 16) |
            (static_cast<uint32_t>(p[3]) << 24);
    return cvt.f;
}

void write_f32_le(std::ofstream& out, float v) {
    union {
        float f;
        uint32_t u;
    } cvt{};
    cvt.f = v;
    write_u32_le(out, cvt.u);
}

}  // namespace

StereoBuffer read_wav_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("Nao foi possivel abrir WAV: " + path);
    }

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 44) {
        throw std::runtime_error("Arquivo WAV invalido (muito pequeno)");
    }

    if (!(data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
          data[8] == 'W' && data[9] == 'A' && data[10] == 'V' && data[11] == 'E')) {
        throw std::runtime_error("Formato nao eh WAV RIFF valido");
    }

    size_t pos = 12;
    size_t fmt_pos = 0;
    uint32_t fmt_size = 0;
    size_t data_pos = 0;
    uint32_t data_size = 0;

    while (pos + 8 <= data.size()) {
        const uint32_t chunk_size = read_u32_le(data, pos + 4);
        const size_t chunk_data_pos = pos + 8;
        if (chunk_data_pos + chunk_size > data.size()) {
            throw std::runtime_error("Chunk WAV truncado");
        }

        if (data[pos] == 'f' && data[pos + 1] == 'm' && data[pos + 2] == 't' && data[pos + 3] == ' ') {
            fmt_pos = chunk_data_pos;
            fmt_size = chunk_size;
        } else if (data[pos] == 'd' && data[pos + 1] == 'a' && data[pos + 2] == 't' && data[pos + 3] == 'a') {
            data_pos = chunk_data_pos;
            data_size = chunk_size;
        }

        pos = chunk_data_pos + chunk_size + (chunk_size & 1U);
    }

    if (fmt_pos == 0 || data_pos == 0) {
        throw std::runtime_error("WAV sem chunk fmt/data");
    }

    if (fmt_size < 16) {
        throw std::runtime_error("Chunk fmt invalido");
    }

    uint16_t audio_format = read_u16_le(data, fmt_pos + 0);
    const uint16_t channels = read_u16_le(data, fmt_pos + 2);
    const uint32_t sample_rate = read_u32_le(data, fmt_pos + 4);
    const uint16_t block_align = read_u16_le(data, fmt_pos + 12);
    const uint16_t bits_per_sample = read_u16_le(data, fmt_pos + 14);

    if (audio_format == WAVE_FORMAT_EXTENSIBLE) {
        if (fmt_size < 40) {
            throw std::runtime_error("WAV extensivel com fmt curto demais");
        }
        audio_format = read_u16_le(data, fmt_pos + 24);
    }

    if (channels == 0 || channels > 2) {
        throw std::runtime_error("Suporta apenas WAV mono ou stereo");
    }

    if (!((audio_format == WAVE_FORMAT_PCM && (bits_per_sample == 16 || bits_per_sample == 24 || bits_per_sample == 32)) ||
          (audio_format == WAVE_FORMAT_IEEE_FLOAT && bits_per_sample == 32))) {
        throw std::runtime_error("WAV suportado: PCM 16/24/32 ou float32");
    }

    const uint16_t bytes_per_sample = static_cast<uint16_t>(bits_per_sample / 8);
    const uint16_t expected_block_align = static_cast<uint16_t>(channels * bytes_per_sample);
    if (block_align != expected_block_align) {
        throw std::runtime_error("Block align invalido no WAV");
    }

    const size_t frame_count = data_size / block_align;
    StereoBuffer out;
    out.sample_rate = sample_rate;
    out.left.resize(frame_count);
    out.right.resize(frame_count);

    for (size_t i = 0; i < frame_count; ++i) {
        const uint8_t* frame = &data[data_pos + i * block_align];

        float l = 0.0f;
        float r = 0.0f;

        if (audio_format == WAVE_FORMAT_PCM) {
            l = decode_pcm_sample(frame, bits_per_sample);
            r = (channels == 2) ? decode_pcm_sample(frame + bytes_per_sample, bits_per_sample) : l;
        } else {
            l = decode_float32_sample(frame);
            r = (channels == 2) ? decode_float32_sample(frame + bytes_per_sample) : l;
        }

        out.left[i] = std::isfinite(l) ? l : 0.0f;
        out.right[i] = std::isfinite(r) ? r : 0.0f;
    }

    return out;
}

void write_wav_file_pcm16(const std::string& path, const StereoBuffer& buffer) {
    if (buffer.left.size() != buffer.right.size()) {
        throw std::runtime_error("Buffers left/right com tamanhos diferentes");
    }
    if (buffer.sample_rate == 0) {
        throw std::runtime_error("Sample rate invalido");
    }

    constexpr uint16_t channels = 2;
    constexpr uint16_t bits_per_sample = 16;
    constexpr uint16_t bytes_per_sample = bits_per_sample / 8;
    constexpr uint16_t block_align = channels * bytes_per_sample;

    const uint32_t frame_count = static_cast<uint32_t>(buffer.left.size());
    const uint32_t data_bytes = frame_count * block_align;

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Nao foi possivel criar WAV: " + path);
    }

    out.write("RIFF", 4);
    write_u32_le(out, 36 + data_bytes);
    out.write("WAVE", 4);

    out.write("fmt ", 4);
    write_u32_le(out, 16);
    write_u16_le(out, WAVE_FORMAT_PCM);
    write_u16_le(out, channels);
    write_u32_le(out, buffer.sample_rate);
    write_u32_le(out, buffer.sample_rate * block_align);
    write_u16_le(out, block_align);
    write_u16_le(out, bits_per_sample);

    out.write("data", 4);
    write_u32_le(out, data_bytes);

    for (uint32_t i = 0; i < frame_count; ++i) {
        const float l = std::isfinite(buffer.left[i]) ? std::max(-1.0f, std::min(1.0f, buffer.left[i])) : 0.0f;
        const float r = std::isfinite(buffer.right[i]) ? std::max(-1.0f, std::min(1.0f, buffer.right[i])) : 0.0f;

        const int16_t li = static_cast<int16_t>(std::lrint(l * 32767.0f));
        const int16_t ri = static_cast<int16_t>(std::lrint(r * 32767.0f));

        write_u16_le(out, static_cast<uint16_t>(li));
        write_u16_le(out, static_cast<uint16_t>(ri));
    }
}

void write_wav_file_float32(const std::string& path, const StereoBuffer& buffer) {
    if (buffer.left.size() != buffer.right.size()) {
        throw std::runtime_error("Buffers left/right com tamanhos diferentes");
    }
    if (buffer.sample_rate == 0) {
        throw std::runtime_error("Sample rate invalido");
    }

    constexpr uint16_t channels = 2;
    constexpr uint16_t bits_per_sample = 32;
    constexpr uint16_t bytes_per_sample = bits_per_sample / 8;
    constexpr uint16_t block_align = channels * bytes_per_sample;

    const uint32_t frame_count = static_cast<uint32_t>(buffer.left.size());
    const uint32_t data_bytes = frame_count * block_align;

    std::ofstream out(path, std::ios::binary);
    if (!out) {
        throw std::runtime_error("Nao foi possivel criar WAV: " + path);
    }

    out.write("RIFF", 4);
    write_u32_le(out, 36 + data_bytes);
    out.write("WAVE", 4);

    out.write("fmt ", 4);
    write_u32_le(out, 16);
    write_u16_le(out, WAVE_FORMAT_IEEE_FLOAT);
    write_u16_le(out, channels);
    write_u32_le(out, buffer.sample_rate);
    write_u32_le(out, buffer.sample_rate * block_align);
    write_u16_le(out, block_align);
    write_u16_le(out, bits_per_sample);

    out.write("data", 4);
    write_u32_le(out, data_bytes);

    for (uint32_t i = 0; i < frame_count; ++i) {
        const float l = std::isfinite(buffer.left[i]) ? std::max(-1.0f, std::min(1.0f, buffer.left[i])) : 0.0f;
        const float r = std::isfinite(buffer.right[i]) ? std::max(-1.0f, std::min(1.0f, buffer.right[i])) : 0.0f;
        write_f32_le(out, l);
        write_f32_le(out, r);
    }
}