#pragma once

#include <vector>

#include "catcheye/roi/point.hpp"
#include "catcheye/roi/roi_polygon.hpp"

namespace catcheye::roi {

struct PolygonBounds {
    double min_x {0.0};
    double min_y {0.0};
    double max_x {0.0};
    double max_y {0.0};
    bool valid {false};
};

bool point_in_polygon(const Point& point, const std::vector<Point>& polygon_points);
bool point_in_polygon(const Point& point, const RoiPolygon& polygon);

double polygon_signed_area(const std::vector<Point>& polygon_points);
double polygon_area(const std::vector<Point>& polygon_points);
double polygon_area(const RoiPolygon& polygon);

PolygonBounds polygon_bounds(const std::vector<Point>& polygon_points);
PolygonBounds polygon_bounds(const RoiPolygon& polygon);

bool has_self_intersections(const std::vector<Point>& polygon_points);

bool is_point_inside_any_allowed_zone(
    const Point& point,
    const std::vector<RoiPolygon>& allowed_zones
);

bool is_bounding_box_inside_polygon(
    double x,
    double y,
    double width,
    double height,
    const RoiPolygon& polygon
);

bool is_bounding_box_inside_any_allowed_zone(
    double x,
    double y,
    double width,
    double height,
    const std::vector<RoiPolygon>& allowed_zones
);

} // namespace catcheye::roi
