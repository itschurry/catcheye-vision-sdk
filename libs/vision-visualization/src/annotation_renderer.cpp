#include "catcheye/visualization/annotation_renderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include <opencv2/imgproc.hpp>

#include "catcheye/input/pixel_format.hpp"

namespace catcheye::visualization {
namespace {

cv::Mat frame_to_bgr(const catcheye::input::Frame& frame)
{
    if (frame.empty() || frame.width <= 0 || frame.height <= 0 || frame.stride <= 0) {
        return {};
    }

    const std::size_t expected_size = catcheye::input::frame_data_size(frame.format, frame.stride, frame.height);
    if (frame.data.size() < expected_size) {
        return {};
    }

    auto* raw = const_cast<std::uint8_t*>(frame.data.data());
    switch (frame.format) {
    case catcheye::input::PixelFormat::BGR: {
        cv::Mat wrapped(frame.height, frame.width, CV_8UC3, raw, static_cast<std::size_t>(frame.stride));
        return wrapped.clone();
    }
    case catcheye::input::PixelFormat::RGB: {
        cv::Mat wrapped(frame.height, frame.width, CV_8UC3, raw, static_cast<std::size_t>(frame.stride));
        cv::Mat bgr;
        cv::cvtColor(wrapped, bgr, cv::COLOR_RGB2BGR);
        return bgr;
    }
    case catcheye::input::PixelFormat::RGBA: {
        cv::Mat wrapped(frame.height, frame.width, CV_8UC4, raw, static_cast<std::size_t>(frame.stride));
        cv::Mat bgr;
        cv::cvtColor(wrapped, bgr, cv::COLOR_RGBA2BGR);
        return bgr;
    }
    case catcheye::input::PixelFormat::BGRA: {
        cv::Mat wrapped(frame.height, frame.width, CV_8UC4, raw, static_cast<std::size_t>(frame.stride));
        cv::Mat bgr;
        cv::cvtColor(wrapped, bgr, cv::COLOR_BGRA2BGR);
        return bgr;
    }
    case catcheye::input::PixelFormat::GRAY8: {
        cv::Mat wrapped(frame.height, frame.width, CV_8UC1, raw, static_cast<std::size_t>(frame.stride));
        cv::Mat bgr;
        cv::cvtColor(wrapped, bgr, cv::COLOR_GRAY2BGR);
        return bgr;
    }
    case catcheye::input::PixelFormat::NV12: {
        cv::Mat wrapped(frame.height + (frame.height / 2), frame.width, CV_8UC1, raw, static_cast<std::size_t>(frame.stride));
        cv::Mat bgr;
        cv::cvtColor(wrapped, bgr, cv::COLOR_YUV2BGR_NV12);
        return bgr;
    }
    case catcheye::input::PixelFormat::UNKNOWN:
        break;
    }
    return {};
}

cv::Rect detection_rect(const catcheye::Detection& detection, int width, int height)
{
    const int x = std::clamp(static_cast<int>(std::lround(detection.box.x)), 0, width - 1);
    const int y = std::clamp(static_cast<int>(std::lround(detection.box.y)), 0, height - 1);
    const int right = std::clamp(static_cast<int>(std::lround(detection.box.x + detection.box.width)), 0, width);
    const int bottom = std::clamp(static_cast<int>(std::lround(detection.box.y + detection.box.height)), 0, height);
    return cv::Rect(cv::Point(x, y), cv::Point(std::max(x + 1, right), std::max(y + 1, bottom)));
}

} // namespace

bool build_annotated_detection_frame(
    const catcheye::input::Frame& source,
    std::span<const catcheye::Detection> detections,
    catcheye::input::Frame& output,
    const DetectionAnnotationStyle& style)
{
    cv::Mat bgr = frame_to_bgr(source);
    if (bgr.empty()) {
        return false;
    }

    draw_detection_boxes(bgr, detections, style);

    output.data.assign(bgr.datastart, bgr.dataend);
    output.width = bgr.cols;
    output.height = bgr.rows;
    output.stride = static_cast<int>(bgr.step);
    output.format = catcheye::input::PixelFormat::BGR;
    output.timestamp = source.timestamp;
    return true;
}

void draw_detection_boxes(
    cv::Mat& image,
    std::span<const catcheye::Detection> detections,
    const DetectionAnnotationStyle& style)
{
    if (image.empty()) {
        return;
    }

    for (const auto& detection : detections) {
        cv::rectangle(image, detection_rect(detection, image.cols, image.rows), style.box_color, style.line_thickness);
    }
}

} // namespace catcheye::visualization
