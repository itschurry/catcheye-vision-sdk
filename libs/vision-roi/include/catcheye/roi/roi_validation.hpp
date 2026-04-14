#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "catcheye/roi/camera_roi_config.hpp"

namespace catcheye::roi {

enum class ValidationIssueCode {
    InvalidImageWidth,
    InvalidImageHeight,
    EmptyPolygon,
    TooFewPolygonPoints,
    PointOutOfBounds,
    DuplicateConsecutivePoints,
    SelfIntersectingPolygon,
    NearZeroAreaPolygon
};

struct ValidationIssue {
    ValidationIssueCode code;
    std::string message;
    std::size_t zone_index {0};
    std::size_t point_index {0};
};

struct ValidationOptions {
    bool detect_self_intersection {true};
    bool reject_near_zero_area {true};
    double minimum_area_threshold {1.0};
};

struct ValidationResult {
    bool valid {true};
    std::vector<ValidationIssue> issues;
};

ValidationResult validate_camera_roi_config(
    const CameraRoiConfig& config,
    const ValidationOptions& options = {}
);

} // namespace catcheye::roi
