#pragma once

#include <cstdint>
#include <vector>

struct UnivibeParams {
    bool mode_chorus = true;
    float rate_hz = 1.2f;
    float depth = 0.6f;
    float width = 0.8f;
    float feedback = 0.45f;
    float mix = 1.0f;
    uint32_t seed = 1;
};

class DesktopUnivibeProcessor {
public:
    explicit DesktopUnivibeProcessor(const UnivibeParams& params);
    ~DesktopUnivibeProcessor();

    DesktopUnivibeProcessor(const DesktopUnivibeProcessor&) = delete;
    DesktopUnivibeProcessor& operator=(const DesktopUnivibeProcessor&) = delete;

    void process_in_place(std::vector<float>& left, std::vector<float>& right);

private:
    struct Impl;
    Impl* impl_ = nullptr;
};