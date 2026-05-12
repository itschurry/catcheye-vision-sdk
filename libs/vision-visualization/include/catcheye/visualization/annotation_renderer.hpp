#pragma once

#include <span>

#include <opencv2/core.hpp>

#include "catcheye/detection/detector.hpp"
#include "catcheye/input/frame.hpp"

namespace catcheye::visualization {

struct DetectionAnnotationStyle {
    cv::Scalar box_color{0, 255, 0};
    int line_thickness = 2;
};

void draw_detection_boxes(
    cv::Mat& image,
    std::span<const catcheye::Detection> detections,
    const DetectionAnnotationStyle& style = {});

bool build_annotated_detection_frame(
    const catcheye::input::Frame& source,
    std::span<const catcheye::Detection> detections,
    catcheye::input::Frame& output,
    const DetectionAnnotationStyle& style = {});

} // namespace catcheye::visualization
