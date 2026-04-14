#pragma once

#include <string>
#include <vector>

#include "catcheye/roi/roi_polygon.hpp"

namespace catcheye::roi {

struct CameraRoiConfig {
    std::string camera_id;
    int image_width {0};
    int image_height {0};
    std::vector<RoiPolygon> allowed_zones;
};

} // namespace catcheye::roi
