#pragma once

#include <string>

#include "catcheye/roi/camera_roi_config.hpp"
#include "catcheye/roi/point.hpp"

namespace catcheye::roi {

enum class EvaluationStatus {
    Allowed,
    Restricted,
    Invalid
};

struct EvaluationResult {
    EvaluationStatus status {EvaluationStatus::Invalid};
    std::string reason;
};

EvaluationResult evaluate_reference_point(
    const Point& reference_point,
    const CameraRoiConfig& config
);

EvaluationResult evaluate_bbox_bottom_center(
    double x,
    double y,
    double width,
    double height,
    const CameraRoiConfig& config
);

EvaluationResult evaluate_bbox_fully_inside(
    double x,
    double y,
    double width,
    double height,
    const CameraRoiConfig& config
);

} // namespace catcheye::roi
