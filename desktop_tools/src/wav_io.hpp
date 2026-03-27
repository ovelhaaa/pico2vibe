#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct StereoBuffer {
    uint32_t sample_rate = 0;
    std::vector<float> left;
    std::vector<float> right;
};

StereoBuffer read_wav_file(const std::string& path);
void write_wav_file_pcm16(const std::string& path, const StereoBuffer& buffer);
void write_wav_file_float32(const std::string& path, const StereoBuffer& buffer);