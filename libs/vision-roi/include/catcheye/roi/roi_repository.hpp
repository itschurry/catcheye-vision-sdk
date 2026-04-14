#pragma once

#include <string>
#include <vector>

#include "catcheye/roi/camera_roi_config.hpp"

namespace catcheye::roi {

struct RoiConfigParseResult {
    bool success {false};
    CameraRoiConfig config;
    std::vector<std::string> errors;
};

class RoiRepository {
public:
    static RoiConfigParseResult from_json_string(const std::string& json_text);
    static RoiConfigParseResult load_from_file(const std::string& path);

    static std::string to_json_string(const CameraRoiConfig& config, int indent = 2);
    static bool save_to_file(const CameraRoiConfig& config, const std::string& path);
};

} // namespace catcheye::roi
