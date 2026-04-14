#include "test_support.hpp"

#include "catcheye/roi/roi_validation.hpp"

using namespace catcheye::roi;

namespace {

CameraRoiConfig valid_config()
{
    CameraRoiConfig config;
    config.camera_id = "cam_01";
    config.image_width = 1920;
    config.image_height = 1080;
    config.allowed_zones = {
        RoiPolygon {
            "zone_1",
            "main",
            true,
            {{320.0, 180.0}, {1580.0, 200.0}, {1500.0, 900.0}, {360.0, 920.0}}
        }
    };
    return config;
}

} // namespace

TEST_CASE(validation_accepts_valid_config)
{
    const auto result = validate_camera_roi_config(valid_config());
    test_support::assert_true(result.valid, "expected valid config");
    test_support::assert_true(result.issues.empty(), "expected no issues");
}

TEST_CASE(validation_rejects_invalid_dimensions_and_points)
{
    auto config = valid_config();
    config.image_width = 0;
    config.allowed_zones[0].points[0] = {-1.0, 100.0};

    const auto result = validate_camera_roi_config(config);
    test_support::assert_true(!result.valid, "expected invalid config");

    bool width_issue = false;
    bool bounds_issue = false;
    for (const auto& issue : result.issues) {
        width_issue = width_issue || issue.code == ValidationIssueCode::InvalidImageWidth;
        bounds_issue = bounds_issue || issue.code == ValidationIssueCode::PointOutOfBounds;
    }

    test_support::assert_true(width_issue, "missing InvalidImageWidth issue");
    test_support::assert_true(bounds_issue, "missing PointOutOfBounds issue");
}

TEST_CASE(validation_rejects_duplicate_consecutive_points)
{
    auto config = valid_config();
    config.allowed_zones[0].points[2] = config.allowed_zones[0].points[1];

    const auto result = validate_camera_roi_config(config);
    bool duplicate_issue = false;
    for (const auto& issue : result.issues) {
        duplicate_issue = duplicate_issue || issue.code == ValidationIssueCode::DuplicateConsecutivePoints;
    }

    test_support::assert_true(duplicate_issue, "missing duplicate consecutive point issue");
}
