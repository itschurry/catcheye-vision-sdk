#include "test_support.hpp"

#include "catcheye/roi/roi_repository.hpp"

using namespace catcheye::roi;

TEST_CASE(repository_parses_and_serializes_json)
{
    const std::string json_text = R"json(
{
  "camera_id": "cam_01",
  "image_width": 1920,
  "image_height": 1080,
  "allowed_zones": [
    {
      "id": "zone_1",
      "name": "main_safe_zone",
      "enabled": true,
      "points": [[320.0, 180.0], [1580.0, 200.0], [1500.0, 900.0], [360.0, 920.0]]
    }
  ]
}
)json";

    const auto parsed = RoiRepository::from_json_string(json_text);
    test_support::assert_true(parsed.success, "expected parse success");
    test_support::assert_true(parsed.config.allowed_zones.size() == 1, "zone count mismatch");

    const auto serialized = RoiRepository::to_json_string(parsed.config);
    const auto round_trip = RoiRepository::from_json_string(serialized);
    test_support::assert_true(round_trip.success, "round-trip parse failed");
    test_support::assert_true(round_trip.config.camera_id == "cam_01", "camera id mismatch");
}

TEST_CASE(repository_reports_structured_parse_errors)
{
    const std::string bad_json = R"json({"camera_id":123,"allowed_zones":[]})json";
    const auto parsed = RoiRepository::from_json_string(bad_json);

    test_support::assert_true(!parsed.success, "expected parse failure");
    test_support::assert_true(!parsed.errors.empty(), "expected errors");
}
