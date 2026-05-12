#pragma once

#include <functional>
#include <string>

#include "catcheye/http/http_server.hpp"
#include "catcheye/roi/camera_roi_config.hpp"

namespace catcheye::http {

enum class RoiConfigKind {
    Person,
    Pallet,
};

struct RoiApiConfig {
    std::string person_roi_path;
    std::string pallet_roi_path;
    std::function<bool(RoiConfigKind, const catcheye::roi::CameraRoiConfig&)> apply;
};

void register_roi_routes(HttpServer& server, RoiApiConfig config);

} // namespace catcheye::http
