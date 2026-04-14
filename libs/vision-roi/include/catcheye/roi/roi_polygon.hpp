#pragma once

#include <string>
#include <vector>

#include "catcheye/roi/point.hpp"

namespace catcheye::roi {

struct RoiPolygon {
    std::string id;
    std::string name;
    bool enabled {true};
    std::vector<Point> points;
};

} // namespace catcheye::roi
