#include "test_support.hpp"

#include "catcheye/roi/roi_evaluator.hpp"

using namespace catcheye::roi;

namespace {

CameraRoiConfig config_with_two_zones()
{
    CameraRoiConfig config;
    config.camera_id = "cam_eval";
    config.image_width = 1000;
    config.image_height = 800;
    config.allowed_zones = {
        RoiPolygon {"zone_enabled", "enabled", true, {{100, 100}, {400, 100}, {400, 400}, {100, 400}}},
        RoiPolygon {"zone_disabled", "disabled", false, {{500, 500}, {700, 500}, {700, 700}, {500, 700}}}
    };
    return config;
}

} // namespace

TEST_CASE(evaluator_returns_allowed_for_inside_point)
{
    const auto result = evaluate_reference_point({200.0, 300.0}, config_with_two_zones());
    test_support::assert_true(result.status == EvaluationStatus::Allowed, "expected Allowed");
}

TEST_CASE(evaluator_skips_disabled_zones)
{
    const auto result = evaluate_reference_point({600.0, 600.0}, config_with_two_zones());
    test_support::assert_true(result.status == EvaluationStatus::Restricted, "expected Restricted");
}

TEST_CASE(evaluator_bbox_fully_inside_returns_allowed_when_entire_box_is_inside)
{
    const auto result = evaluate_bbox_fully_inside(120.0, 120.0, 100.0, 200.0, config_with_two_zones());
    test_support::assert_true(result.status == EvaluationStatus::Allowed, "expected Allowed for fully-contained bbox");
}

TEST_CASE(evaluator_bbox_fully_inside_returns_restricted_when_box_crosses_zone_boundary)
{
    const auto result = evaluate_bbox_fully_inside(120.0, 50.0, 100.0, 200.0, config_with_two_zones());
    test_support::assert_true(result.status == EvaluationStatus::Restricted, "expected Restricted for partially outside bbox");
}

TEST_CASE(evaluator_reports_invalid_config)
{
    auto config = config_with_two_zones();
    config.image_height = 0;

    const auto result = evaluate_reference_point({100.0, 100.0}, config);
    test_support::assert_true(result.status == EvaluationStatus::Invalid, "expected Invalid");
}
