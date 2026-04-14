#include "catcheye/roi/roi_validation.hpp"

#include <cmath>
#include <utility>

#include "catcheye/roi/roi_geometry.hpp"

namespace catcheye::roi {
namespace {

bool point_in_image_bounds(const Point& point, int width, int height)
{
    const double max_x = static_cast<double>(width);
    const double max_y = static_cast<double>(height);
    return point.x >= 0.0 && point.x <= max_x && point.y >= 0.0 && point.y <= max_y;
}

void append_issue(
    ValidationResult& result,
    ValidationIssueCode code,
    std::string message,
    std::size_t zone_index,
    std::size_t point_index = 0
)
{
    result.valid = false;
    result.issues.push_back(ValidationIssue {code, std::move(message), zone_index, point_index});
}

} // namespace

ValidationResult validate_camera_roi_config(const CameraRoiConfig& config, const ValidationOptions& options)
{
    ValidationResult result;

    if (config.image_width <= 0) {
        append_issue(result, ValidationIssueCode::InvalidImageWidth, "image_width must be > 0", 0U);
    }

    if (config.image_height <= 0) {
        append_issue(result, ValidationIssueCode::InvalidImageHeight, "image_height must be > 0", 0U);
    }

    for (std::size_t zone_index = 0; zone_index < config.allowed_zones.size(); ++zone_index) {
        const auto& zone = config.allowed_zones[zone_index];

        if (zone.points.empty()) {
            append_issue(result, ValidationIssueCode::EmptyPolygon, "zone has no points", zone_index);
            continue;
        }

        if (zone.points.size() < 3) {
            append_issue(result, ValidationIssueCode::TooFewPolygonPoints, "zone requires at least 3 points", zone_index);
        }

        for (std::size_t point_index = 1; point_index < zone.points.size(); ++point_index) {
            if (zone.points[point_index] == zone.points[point_index - 1]) {
                append_issue(
                    result,
                    ValidationIssueCode::DuplicateConsecutivePoints,
                    "zone has duplicate consecutive points",
                    zone_index,
                    point_index
                );
            }
        }

        for (std::size_t point_index = 0; point_index < zone.points.size(); ++point_index) {
            if (!point_in_image_bounds(zone.points[point_index], config.image_width, config.image_height)) {
                append_issue(
                    result,
                    ValidationIssueCode::PointOutOfBounds,
                    "point is outside image bounds",
                    zone_index,
                    point_index
                );
            }
        }

        if (options.detect_self_intersection && has_self_intersections(zone.points)) {
            append_issue(
                result,
                ValidationIssueCode::SelfIntersectingPolygon,
                "polygon self-intersection detected",
                zone_index
            );
        }

        if (options.reject_near_zero_area) {
            const double area = polygon_area(zone.points);
            if (area < options.minimum_area_threshold || std::fabs(area) < 1e-12) {
                append_issue(
                    result,
                    ValidationIssueCode::NearZeroAreaPolygon,
                    "polygon area is near zero",
                    zone_index
                );
            }
        }
    }

    return result;
}

} // namespace catcheye::roi
