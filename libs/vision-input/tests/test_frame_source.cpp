#include "test_support.hpp"

#include <filesystem>

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>

#include "catcheye/input/frame_source.hpp"

namespace fs = std::filesystem;

namespace {

fs::path make_temp_png_path()
{
    return fs::temp_directory_path() / "catcheye_vision_input_test.png";
}

} // namespace

TEST_CASE(image_file_source_reads_once_then_reaches_end_of_stream)
{
    const fs::path image_path = make_temp_png_path();
    const cv::Mat image(8, 12, CV_8UC3, cv::Scalar(1, 2, 3));
    const bool write_ok = cv::imwrite(image_path.string(), image);
    test_support::assert_true(write_ok, "failed to create temporary PNG");

    catcheye::input::InputSourceConfig config;
    config.type = catcheye::input::InputSourceType::ImageFile;
    config.uri = image_path.string();
    catcheye::input::ImageFileSource source(config);

    test_support::assert_true(source.open(), "image source open should succeed");

    catcheye::input::Frame frame;
    test_support::assert_true(source.read(frame) == catcheye::input::FrameReadStatus::Ok, "first read should succeed");
    test_support::assert_true(frame.width() == image.cols, "frame width mismatch");
    test_support::assert_true(frame.height() == image.rows, "frame height mismatch");
    test_support::assert_true(source.read(frame) == catcheye::input::FrameReadStatus::EndOfStream, "second read should end");

    source.close();
    fs::remove(image_path);
}

TEST_CASE(opencv_capture_source_fails_for_missing_video_file)
{
    catcheye::input::InputSourceConfig config;
    config.type = catcheye::input::InputSourceType::VideoFile;
    config.uri = "/tmp/catcheye_missing_video.mp4";
    catcheye::input::OpenCvCaptureSource source(config);

    test_support::assert_true(!source.open(), "missing video should fail to open");
}

TEST_CASE(opencv_capture_source_fails_for_invalid_camera_pipeline)
{
    catcheye::input::InputSourceConfig config;
    config.type = catcheye::input::InputSourceType::Camera;
    config.camera_pipeline = "this_is_not_a_valid_gstreamer_pipeline";
    catcheye::input::OpenCvCaptureSource source(config);

    test_support::assert_true(!source.open(), "invalid camera pipeline should fail to open");
}

TEST_CASE(default_camera_pipeline_is_not_empty)
{
    test_support::assert_true(!catcheye::input::default_camera_pipeline().empty(), "default camera pipeline should not be empty");
}
