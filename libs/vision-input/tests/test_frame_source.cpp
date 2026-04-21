#include "test_support.hpp"

#include "catcheye/input/gstreamer_source.hpp"
#include "catcheye/input/libcamera_source.hpp"

TEST_CASE(gstreamer_source_reads_frame_from_test_pattern)
{
    catcheye::input::GStreamerSource source({
        .pipeline = catcheye::input::GStreamerSource::test_pattern_pipeline(64, 48),
    });

    test_support::assert_true(source.open(), "test pattern source should open");
    test_support::assert_true(source.is_open(), "source should report open");

    catcheye::input::Frame frame;
    const auto status = source.read(frame);
    test_support::assert_true(
        status == catcheye::input::FrameReadStatus::Ok, "first read should succeed");
    test_support::assert_true(frame.width == 64, "frame width mismatch");
    test_support::assert_true(frame.height == 48, "frame height mismatch");
    test_support::assert_true(frame.stride > 0, "stride should be positive");
    test_support::assert_true(!frame.empty(), "frame data should not be empty");
    test_support::assert_true(
        frame.format == catcheye::input::PixelFormat::NV12, "format should be NV12");

    source.close();
    test_support::assert_true(!source.is_open(), "source should report closed");
}

TEST_CASE(gstreamer_source_fails_for_invalid_pipeline)
{
    catcheye::input::GStreamerSource source({.pipeline = "not_a_real_element"});
    test_support::assert_true(!source.open(), "invalid pipeline should fail to open");
}

TEST_CASE(gstreamer_source_describe_contains_pipeline)
{
    const std::string pipeline =
        catcheye::input::GStreamerSource::test_pattern_pipeline(64, 48);
    catcheye::input::GStreamerSource source({.pipeline = pipeline});
    const std::string desc = source.describe();
    test_support::assert_true(
        desc.find("gstreamer:") != std::string::npos,
        "describe should contain gstreamer: prefix");
}

TEST_CASE(libcamera_source_describe_before_open)
{
    catcheye::input::LibCameraSource source({.camera_id = "test-id"});
    const std::string desc = source.describe();
    test_support::assert_true(
        desc.find("libcamera:") != std::string::npos,
        "describe should contain libcamera: prefix");
}

TEST_CASE(gstreamer_pipeline_helpers_are_not_empty)
{
    test_support::assert_true(
        !catcheye::input::GStreamerSource::test_pattern_pipeline().empty(),
        "test pattern pipeline should not be empty");
    test_support::assert_true(
        !catcheye::input::GStreamerSource::usb_camera_pipeline().empty(),
        "usb camera pipeline should not be empty");
    test_support::assert_true(
        !catcheye::input::GStreamerSource::video_file_pipeline("/tmp/test.mp4").empty(),
        "video file pipeline should not be empty");
}
