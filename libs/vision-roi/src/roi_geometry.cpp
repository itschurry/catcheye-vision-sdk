#include "catcheye/roi/roi_geometry.hpp"

#include <algorithm>
#include <cmath>

namespace catcheye::roi {
namespace {

constexpr double kEpsilon = 1e-9;

bool is_point_on_segment(const Point& point, const Point& a, const Point& b)
{
    const double cross = ((point.y - a.y) * (b.x - a.x)) - ((point.x - a.x) * (b.y - a.y));
    if (std::fabs(cross) > kEpsilon) {
        return false;
    }

    const double dot = ((point.x - a.x) * (b.x - a.x)) + ((point.y - a.y) * (b.y - a.y));
    if (dot < -kEpsilon) {
        return false;
    }

    const double squared_length = ((b.x - a.x) * (b.x - a.x)) + ((b.y - a.y) * (b.y - a.y));
    return dot <= squared_length + kEpsilon;
}

int orientation(const Point& a, const Point& b, const Point& c)
{
    const double value = ((b.y - a.y) * (c.x - b.x)) - ((b.x - a.x) * (c.y - b.y));
    if (std::fabs(value) < kEpsilon) {
        return 0;
    }

    return (value > 0.0) ? 1 : 2;
}

bool segments_intersect(const Point& p1, const Point& q1, const Point& p2, const Point& q2)
{
    const int o1 = orientation(p1, q1, p2);
    const int o2 = orientation(p1, q1, q2);
    const int o3 = orientation(p2, q2, p1);
    const int o4 = orientation(p2, q2, q1);

    if ((o1 != o2) && (o3 != o4)) {
        return true;
    }

    if ((o1 == 0) && is_point_on_segment(p2, p1, q1)) {
        return true;
    }
    if ((o2 == 0) && is_point_on_segment(q2, p1, q1)) {
        return true;
    }
    if ((o3 == 0) && is_point_on_segment(p1, p2, q2)) {
        return true;
    }
    if ((o4 == 0) && is_point_on_segment(q1, p2, q2)) {
        return true;
    }

    return false;
}

bool segments_properly_intersect(const Point& p1, const Point& q1, const Point& p2, const Point& q2)
{
    const int o1 = orientation(p1, q1, p2);
    const int o2 = orientation(p1, q1, q2);
    const int o3 = orientation(p2, q2, p1);
    const int o4 = orientation(p2, q2, q1);

    return (o1 != 0) && (o2 != 0) && (o3 != 0) && (o4 != 0) && (o1 != o2) && (o3 != o4);
}

std::vector<Point> bounding_box_corners(double x, double y, double width, double height)
{
    return {
        {x, y},
        {x + width, y},
        {x + width, y + height},
        {x, y + height}
    };
}

} // namespace

bool point_in_polygon(const Point& point, const std::vector<Point>& polygon_points)
{
    if (polygon_points.size() < 3) {
        return false;
    }

    bool inside = false;
    std::size_t j = polygon_points.size() - 1;

    for (std::size_t i = 0; i < polygon_points.size(); ++i) {
        const auto& pi = polygon_points[i];
        const auto& pj = polygon_points[j];

        if (is_point_on_segment(point, pi, pj)) {
            return true;
        }

        const bool intersects = ((pi.y > point.y) != (pj.y > point.y))
            && (point.x < ((pj.x - pi.x) * (point.y - pi.y) / ((pj.y - pi.y) + kEpsilon)) + pi.x);

        if (intersects) {
            inside = !inside;
        }

        j = i;
    }

    return inside;
}

bool point_in_polygon(const Point& point, const RoiPolygon& polygon)
{
    return point_in_polygon(point, polygon.points);
}

double polygon_signed_area(const std::vector<Point>& polygon_points)
{
    if (polygon_points.size() < 3) {
        return 0.0;
    }

    double area = 0.0;
    for (std::size_t i = 0; i < polygon_points.size(); ++i) {
        const std::size_t j = (i + 1) % polygon_points.size();
        area += (polygon_points[i].x * polygon_points[j].y) - (polygon_points[j].x * polygon_points[i].y);
    }

    return area * 0.5;
}

double polygon_area(const std::vector<Point>& polygon_points)
{
    return std::fabs(polygon_signed_area(polygon_points));
}

double polygon_area(const RoiPolygon& polygon)
{
    return polygon_area(polygon.points);
}

PolygonBounds polygon_bounds(const std::vector<Point>& polygon_points)
{
    PolygonBounds bounds;
    if (polygon_points.empty()) {
        return bounds;
    }

    auto [min_x_it, max_x_it] = std::minmax_element(
        polygon_points.begin(),
        polygon_points.end(),
        [](const Point& lhs, const Point& rhs) { return lhs.x < rhs.x; }
    );

    auto [min_y_it, max_y_it] = std::minmax_element(
        polygon_points.begin(),
        polygon_points.end(),
        [](const Point& lhs, const Point& rhs) { return lhs.y < rhs.y; }
    );

    bounds.min_x = min_x_it->x;
    bounds.max_x = max_x_it->x;
    bounds.min_y = min_y_it->y;
    bounds.max_y = max_y_it->y;
    bounds.valid = true;
    return bounds;
}

PolygonBounds polygon_bounds(const RoiPolygon& polygon)
{
    return polygon_bounds(polygon.points);
}

bool has_self_intersections(const std::vector<Point>& polygon_points)
{
    const std::size_t n = polygon_points.size();
    if (n < 4) {
        return false;
    }

    for (std::size_t i = 0; i < n; ++i) {
        const Point& a1 = polygon_points[i];
        const Point& a2 = polygon_points[(i + 1) % n];

        for (std::size_t j = i + 1; j < n; ++j) {
            if (j == i || ((j + 1) % n) == i || (i + 1) % n == j) {
                continue;
            }

            const Point& b1 = polygon_points[j];
            const Point& b2 = polygon_points[(j + 1) % n];
            if (segments_intersect(a1, a2, b1, b2)) {
                return true;
            }
        }
    }

    return false;
}

bool is_point_inside_any_allowed_zone(const Point& point, const std::vector<RoiPolygon>& allowed_zones)
{
    for (const auto& zone : allowed_zones) {
        if (!zone.enabled) {
            continue;
        }

        if (point_in_polygon(point, zone.points)) {
            return true;
        }
    }

    return false;
}

bool is_bounding_box_inside_polygon(
    double x,
    double y,
    double width,
    double height,
    const RoiPolygon& polygon
)
{
    if (polygon.points.size() < 3) {
        return false;
    }

    const std::vector<Point> corners = bounding_box_corners(x, y, width, height);
    for (const auto& corner : corners) {
        if (!point_in_polygon(corner, polygon.points)) {
            return false;
        }
    }

    for (std::size_t i = 0; i < corners.size(); ++i) {
        const Point& rect_a = corners[i];
        const Point& rect_b = corners[(i + 1) % corners.size()];

        for (std::size_t j = 0; j < polygon.points.size(); ++j) {
            const Point& poly_a = polygon.points[j];
            const Point& poly_b = polygon.points[(j + 1) % polygon.points.size()];
            if (segments_properly_intersect(rect_a, rect_b, poly_a, poly_b)) {
                return false;
            }
        }
    }

    return true;
}

bool is_bounding_box_inside_any_allowed_zone(
    double x,
    double y,
    double width,
    double height,
    const std::vector<RoiPolygon>& allowed_zones
)
{
    for (const auto& zone : allowed_zones) {
        if (!zone.enabled) {
            continue;
        }

        if (is_bounding_box_inside_polygon(x, y, width, height, zone)) {
            return true;
        }
    }

    return false;
}

} // namespace catcheye::roi
