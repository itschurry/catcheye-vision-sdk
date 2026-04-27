#include "catcheye/detection/ncnn/ncnn_detector.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <utility>

#include <opencv2/dnn/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <yaml-cpp/yaml.h>

#include "catcheye/input/pixel_format.hpp"

namespace catcheye::detection {
namespace {

struct LetterboxResult {
    cv::Mat image;
    float scale = 1.0F;
    int pad_width = 0;
    int pad_height = 0;
};

cv::Mat frame_to_bgr(const catcheye::input::Frame& frame)
{
    if (frame.empty() || frame.width <= 0 || frame.height <= 0 || frame.stride <= 0) {
        return {};
    }

    const std::size_t expected_size =
        catcheye::input::frame_data_size(frame.format, frame.stride, frame.height);
    if (frame.data.size() < expected_size) {
        std::cerr << "frame buffer too small: actual=" << frame.data.size()
                  << ", expected=" << expected_size
                  << ", format=" << static_cast<int>(frame.format) << '\n';
        return {};
    }

    const auto* raw = frame.data.data();
    switch (frame.format) {
        case catcheye::input::PixelFormat::BGR: {
            cv::Mat wrapped(frame.height, frame.width, CV_8UC3, const_cast<std::uint8_t*>(raw), static_cast<std::size_t>(frame.stride));
            return wrapped.clone();
        }
        case catcheye::input::PixelFormat::RGB: {
            cv::Mat wrapped(frame.height, frame.width, CV_8UC3, const_cast<std::uint8_t*>(raw), static_cast<std::size_t>(frame.stride));
            cv::Mat bgr;
            cv::cvtColor(wrapped, bgr, cv::COLOR_RGB2BGR);
            return bgr;
        }
        case catcheye::input::PixelFormat::RGBA: {
            cv::Mat wrapped(frame.height, frame.width, CV_8UC4, const_cast<std::uint8_t*>(raw), static_cast<std::size_t>(frame.stride));
            cv::Mat bgr;
            cv::cvtColor(wrapped, bgr, cv::COLOR_RGBA2BGR);
            return bgr;
        }
        case catcheye::input::PixelFormat::BGRA: {
            cv::Mat wrapped(frame.height, frame.width, CV_8UC4, const_cast<std::uint8_t*>(raw), static_cast<std::size_t>(frame.stride));
            cv::Mat bgr;
            cv::cvtColor(wrapped, bgr, cv::COLOR_BGRA2BGR);
            return bgr;
        }
        case catcheye::input::PixelFormat::GRAY8: {
            cv::Mat wrapped(frame.height, frame.width, CV_8UC1, const_cast<std::uint8_t*>(raw), static_cast<std::size_t>(frame.stride));
            cv::Mat bgr;
            cv::cvtColor(wrapped, bgr, cv::COLOR_GRAY2BGR);
            return bgr;
        }
        case catcheye::input::PixelFormat::NV12: {
            cv::Mat wrapped(frame.height + (frame.height / 2), frame.width, CV_8UC1, const_cast<std::uint8_t*>(raw),
                            static_cast<std::size_t>(frame.stride));
            cv::Mat bgr;
            cv::cvtColor(wrapped, bgr, cv::COLOR_YUV2BGR_NV12);
            return bgr;
        }
        case catcheye::input::PixelFormat::UNKNOWN:
        default:
            break;
    }

    std::cerr << "unsupported pixel format: " << static_cast<int>(frame.format) << '\n';
    return {};
}

std::map<int, std::string> load_class_names(const std::string& yaml_path)
{
    std::map<int, std::string> class_names;
    if (yaml_path.empty()) {
        return class_names;
    }

    try {
        const YAML::Node root = YAML::LoadFile(yaml_path);
        const YAML::Node names = root["names"];
        if (!names) {
            std::cerr << "no 'names' field in metadata file '" << yaml_path << "'\n";
            return class_names;
        }

        if (names.IsSequence()) {
            for (std::size_t i = 0; i < names.size(); ++i) {
                class_names[static_cast<int>(i)] = names[i].as<std::string>();
            }
            return class_names;
        }

        if (names.IsMap()) {
            for (const auto& entry : names) {
                class_names[entry.first.as<int>()] = entry.second.as<std::string>();
            }
        }
    } catch (const std::exception& exception) {
        std::cerr << "failed to load metadata '" << yaml_path << "': " << exception.what() << '\n';
    }

    return class_names;
}

LetterboxResult letterbox(const cv::Mat& image, int target_width, int target_height)
{
    LetterboxResult result;

    const float scale = std::min(
        static_cast<float>(target_width) / static_cast<float>(image.cols),
        static_cast<float>(target_height) / static_cast<float>(image.rows));

    const int resized_width = static_cast<int>(std::round(static_cast<float>(image.cols) * scale));
    const int resized_height = static_cast<int>(std::round(static_cast<float>(image.rows) * scale));

    cv::Mat resized;
    cv::resize(image, resized, cv::Size(resized_width, resized_height));

    result.pad_width = target_width - resized_width;
    result.pad_height = target_height - resized_height;
    result.scale = scale;

    const int left = result.pad_width / 2;
    const int right = result.pad_width - left;
    const int top = result.pad_height / 2;
    const int bottom = result.pad_height - top;

    cv::copyMakeBorder(
        resized,
        result.image,
        top,
        bottom,
        left,
        right,
        cv::BORDER_CONSTANT,
        cv::Scalar(114, 114, 114));

    return result;
}

std::vector<Detection> decode_yolo_output(
    const ncnn::Mat& output,
    float confidence_threshold,
    float nms_threshold,
    float scale,
    int pad_width,
    int pad_height,
    int original_width,
    int original_height)
{
    std::vector<Detection> detections;

    if (output.dims != 2) {
        std::cerr << "unexpected output dims: " << output.dims << '\n';
        return detections;
    }

    const int candidate_count = output.w;
    const int attribute_count = output.h;
    if (attribute_count < 6) {
        std::cerr << "unexpected output attribute count: " << attribute_count << '\n';
        return detections;
    }

    const int left = pad_width / 2;
    const int top = pad_height / 2;

    const float* row_center_x = output.row(0);
    const float* row_center_y = output.row(1);
    const float* row_width = output.row(2);
    const float* row_height = output.row(3);

    std::vector<int> class_ids;
    std::vector<float> scores;
    std::vector<cv::Rect> boxes;

    for (int index = 0; index < candidate_count; ++index) {
        const float center_x = row_center_x[index];
        const float center_y = row_center_y[index];
        const float width = row_width[index];
        const float height = row_height[index];

        int best_class_id = -1;
        float best_score = 0.0F;
        for (int class_offset = 4; class_offset < attribute_count; ++class_offset) {
            const float* class_row = output.row(class_offset);
            const float score = class_row[index];
            if (score > best_score) {
                best_score = score;
                best_class_id = class_offset - 4;
            }
        }

        if (best_score < confidence_threshold) {
            continue;
        }

        float x1 = center_x - (width * 0.5F);
        float y1 = center_y - (height * 0.5F);
        float x2 = center_x + (width * 0.5F);
        float y2 = center_y + (height * 0.5F);

        x1 = (x1 - static_cast<float>(left)) / scale;
        y1 = (y1 - static_cast<float>(top)) / scale;
        x2 = (x2 - static_cast<float>(left)) / scale;
        y2 = (y2 - static_cast<float>(top)) / scale;

        x1 = std::clamp(x1, 0.0F, static_cast<float>(original_width - 1));
        y1 = std::clamp(y1, 0.0F, static_cast<float>(original_height - 1));
        x2 = std::clamp(x2, 0.0F, static_cast<float>(original_width - 1));
        y2 = std::clamp(y2, 0.0F, static_cast<float>(original_height - 1));

        const int box_width = std::max(0, static_cast<int>(x2 - x1));
        const int box_height = std::max(0, static_cast<int>(y2 - y1));
        if (box_width <= 1 || box_height <= 1) {
            continue;
        }

        class_ids.push_back(best_class_id);
        scores.push_back(best_score);
        boxes.emplace_back(static_cast<int>(x1), static_cast<int>(y1), box_width, box_height);
    }

    std::vector<int> selected_indices;
    cv::dnn::NMSBoxes(boxes, scores, confidence_threshold, nms_threshold, selected_indices);

    detections.reserve(selected_indices.size());
    for (const int idx : selected_indices) {
        const cv::Rect& box = boxes[static_cast<std::size_t>(idx)];
        detections.push_back(Detection {
            .class_id = class_ids[static_cast<std::size_t>(idx)],
            .score = scores[static_cast<std::size_t>(idx)],
            .box = BoundingBox {
                .x = static_cast<float>(box.x),
                .y = static_cast<float>(box.y),
                .width = static_cast<float>(box.width),
                .height = static_cast<float>(box.height),
            },
        });
    }

    return detections;
}

} // namespace

NcnnDetector::NcnnDetector(NcnnDetectorConfig config)
    : config_(std::move(config))
{
}

bool NcnnDetector::initialize()
{
    if (initialized_) {
        return true;
    }

    net_.opt.use_vulkan_compute = config_.use_vulkan_compute;
    net_.opt.num_threads = config_.num_threads;

    if (net_.load_param(config_.param_path.c_str()) != 0) {
        std::cerr << "failed to load ncnn param '" << config_.param_path << "'\n";
        return false;
    }

    if (net_.load_model(config_.bin_path.c_str()) != 0) {
        std::cerr << "failed to load ncnn model '" << config_.bin_path << "'\n";
        return false;
    }

    class_names_ = load_class_names(config_.metadata_path);
    initialized_ = true;
    return true;
}

bool NcnnDetector::is_initialized() const
{
    return initialized_;
}

std::vector<Detection> NcnnDetector::detect(const catcheye::input::Frame& frame)
{
    if (!initialized_ || frame.empty()) {
        return {};
    }

    const cv::Mat bgr = frame_to_bgr(frame);
    if (bgr.empty()) {
        return {};
    }

    const LetterboxResult preprocessed = letterbox(
        bgr,
        config_.input_width,
        config_.input_height);

    cv::Mat rgb;
    cv::cvtColor(preprocessed.image, rgb, cv::COLOR_BGR2RGB);

    ncnn::Mat input = ncnn::Mat::from_pixels(
        rgb.data,
        ncnn::Mat::PIXEL_RGB,
        rgb.cols,
        rgb.rows);

    const float norm[3] = {1.0F / 255.0F, 1.0F / 255.0F, 1.0F / 255.0F};
    input.substract_mean_normalize(nullptr, norm);

    ncnn::Extractor extractor = net_.create_extractor();
    if (extractor.input(config_.input_blob_name.c_str(), input) != 0) {
        std::cerr << "failed to bind input blob '" << config_.input_blob_name << "'\n";
        return {};
    }

    ncnn::Mat output;
    if (extractor.extract(config_.output_blob_name.c_str(), output) != 0) {
        std::cerr << "failed to extract output blob '" << config_.output_blob_name << "'\n";
        return {};
    }

    return decode_yolo_output(
        output,
        config_.confidence_threshold,
        config_.nms_threshold,
        preprocessed.scale,
        preprocessed.pad_width,
        preprocessed.pad_height,
        frame.width,
        frame.height);
}

std::string NcnnDetector::class_name(int class_id) const
{
    const auto it = class_names_.find(class_id);
    return (it != class_names_.end()) ? it->second : "cls:" + std::to_string(class_id);
}

} // namespace catcheye::detection
