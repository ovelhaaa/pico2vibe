#include <cstdint>
#include <iostream>
#include <string>

#include "dsp/vibe_core.hpp"

namespace {

std::string csv_escape(const char* text) {
    std::string s = text ? text : "";
    bool needs_quotes = false;
    for (char c : s) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') {
            needs_quotes = true;
            break;
        }
    }
    if (!needs_quotes) return s;

    std::string out;
    out.reserve(s.size() + 2);
    out.push_back('"');
    for (char c : s) {
        if (c == '"') out.push_back('"');
        out.push_back(c);
    }
    out.push_back('"');
    return out;
}

std::string flags_to_string(uint8_t flags) {
    std::string out;
    auto add = [&out](const char* name) {
        if (!out.empty()) out.push_back('|');
        out += name;
    };
    if (flags & VibeParamFlagBoolean) add("boolean");
    if (flags & VibeParamFlagLogScale) add("log_scale");
    if (flags & VibeParamFlagTempo) add("tempo");
    if (flags & VibeParamFlagOutput) add("output");
    if (out.empty()) out = "continuous";
    return out;
}

}  // namespace

int main() {
    std::cout << "id,stable_name,label,unit,min,max,default,flags\n";
    for (uint32_t i = 0; i < vibe_param_count(); ++i) {
        const auto id = static_cast<VibeParamId>(i);
        const VibeParamMetadata meta = vibe_param_metadata(id);
        const VibeParamSpec spec = vibe_param_spec(id);
        const std::string flag_text = flags_to_string(meta.flags);
        std::cout << i << ','
                  << csv_escape(meta.stable_name) << ','
                  << csv_escape(meta.label) << ','
                  << csv_escape(meta.unit) << ','
                  << spec.min_value << ','
                  << spec.max_value << ','
                  << spec.default_value << ','
                  << csv_escape(flag_text.c_str()) << '\n';
    }
    return 0;
}