#include "catcheye/roi/roi_evaluator.hpp"

#include <cmath>

#include "catcheye/roi/roi_geometry.hpp"
#include "catcheye/roi/roi_validation.hpp"

namespace catcheye::roi {
namespace {

bool is_finite(double value)
{
    return std::isfinite(value);
}

} // namespace

EvaluationResult evaluate_reference_point(const Point& reference_point, const CameraRoiConfig& config)
{
    if (!is_finite(reference_point.x) || !is_finite(reference_point.y)) {
        return {EvaluationStatus::Invalid, "reference point coordinates must be finite"};
    }

    const ValidationResult validation = validate_camera_roi_config(config);
    if (!validation.valid) {
        return {EvaluationStatus::Invalid, "ROI config failed validation"};
    }

    if (is_point_inside_any_allowed_zone(reference_point, config.allowed_zones)) {
        return {EvaluationStatus::Allowed, "reference point is inside an enabled zone"};
    }

    return {EvaluationStatus::Restricted, "reference point is outside all enabled zones"};
}

EvaluationResult evaluate_bbox_bottom_center(
    double x,
    double y,
    double width,
    double height,
    const CameraRoiConfig& config
)
{
    if (!is_finite(x) || !is_finite(y) || !is_finite(width) || !is_finite(height)) {
        return {EvaluationStatus::Invalid, "bbox inputs must be finite"};
    }

    if (width <= 0.0 || height <= 0.0) {
        return {EvaluationStatus::Invalid, "bbox width and height must be > 0"};
    }

    Point bottom_center {x + (width * 0.5), y + height};
    return evaluate_reference_point(bottom_center, config);
}

EvaluationResult evaluate_bbox_fully_inside(
    double x,
    double y,
    double width,
    double height,
    const CameraRoiConfig& config
)
{
    if (!is_finite(x) || !is_finite(y) || !is_finite(width) || !is_finite(height)) {
        return {EvaluationStatus::Invalid, "bbox inputs must be finite"};
    }

    if (width <= 0.0 || height <= 0.0) {
        return {EvaluationStatus::Invalid, "bbox width and height must be > 0"};
    }

    const ValidationResult validation = validate_camera_roi_config(config);
    if (!validation.valid) {
        return {EvaluationStatus::Invalid, "ROI config failed validation"};
    }

    if (is_bounding_box_inside_any_allowed_zone(x, y, width, height, config.allowed_zones)) {
        return {EvaluationStatus::Allowed, "bounding box is fully inside an enabled zone"};
    }

    return {EvaluationStatus::Restricted, "bounding box is not fully contained in any enabled zone"};
}

} // namespace catcheye::roi
