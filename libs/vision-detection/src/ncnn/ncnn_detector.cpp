#include "catcheye/detection/ncnn/ncnn_detector.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <memory>
#include <utility>

#include <opencv2/imgproc.hpp>
#include <yaml-cpp/yaml.h>

#include "catcheye/input/pixel_format.hpp"
#include "catcheye/detection/postprocess/postprocess.hpp"
#include "catcheye/detection/postprocess/tensor_view.hpp"
#include "catcheye/detection/postprocess/yolo_decoder.hpp"
#include <net.h>

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

DecodeResult decode_yolo_output(
    const ncnn::Mat& output,
    float scale,
    int pad_width,
    int pad_height,
    int original_width,
    int original_height,
    int input_width,
    int input_height,
    int num_classes)
{
    if (output.dims != 2) {
        std::cerr << "unexpected output dims: " << output.dims << '\n';
        return DecodeResult {};
    }

    const TensorView tensor {
        .name = "ncnn_output",
        .data = output.row(0),
        .byte_size = static_cast<std::size_t>(output.w) * static_cast<std::size_t>(output.h) * sizeof(float),
        .shape = {output.h, output.w},
        .data_type = TensorDataType::Float32,
    };

    const ModelDecodeContext context {
        .input_width = input_width,
        .input_height = input_height,
        .original_width = original_width,
        .original_height = original_height,
        .letterbox_scale = scale,
        .pad_width = pad_width,
        .pad_height = pad_height,
    };

    return YoloDecoder(YoloDecoderOptions {.num_classes = num_classes}).decode({tensor}, context);
}

} // namespace

struct NcnnDetector::Impl {
    ncnn::Net net;
};

NcnnDetector::NcnnDetector(NcnnDetectorConfig config)
    : config_(std::move(config)),
      impl_(std::make_unique<Impl>())
{
}

NcnnDetector::~NcnnDetector() = default;
NcnnDetector::NcnnDetector(NcnnDetector&&) noexcept = default;
NcnnDetector& NcnnDetector::operator=(NcnnDetector&&) noexcept = default;

bool NcnnDetector::initialize()
{
    if (initialized_) {
        return true;
    }

    impl_->net.opt.use_vulkan_compute = config_.use_vulkan_compute;
    impl_->net.opt.num_threads = config_.num_threads;

    if (impl_->net.load_param(config_.param_path.c_str()) != 0) {
        std::cerr << "failed to load ncnn param '" << config_.param_path << "'\n";
        return false;
    }

    if (impl_->net.load_model(config_.bin_path.c_str()) != 0) {
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

    ncnn::Extractor extractor = impl_->net.create_extractor();
    if (extractor.input(config_.input_blob_name.c_str(), input) != 0) {
        std::cerr << "failed to bind input blob '" << config_.input_blob_name << "'\n";
        return {};
    }

    ncnn::Mat output;
    if (extractor.extract(config_.output_blob_name.c_str(), output) != 0) {
        std::cerr << "failed to extract output blob '" << config_.output_blob_name << "'\n";
        return {};
    }

    const DecodeResult decoded = decode_yolo_output(
        output,
        preprocessed.scale,
        preprocessed.pad_width,
        preprocessed.pad_height,
        frame.width,
        frame.height,
        config_.input_width,
        config_.input_height,
        static_cast<int>(class_names_.size()));

    return finalize_detections(
        decoded,
        PostprocessOptions {
            .confidence_threshold = config_.confidence_threshold,
            .nms_threshold = config_.nms_threshold,
            .class_aware_nms = true,
            .apply_nms = true,
            .allowed_class_ids = config_.allowed_class_ids,
        });
}

std::string NcnnDetector::class_name(int class_id) const
{
    const auto it = class_names_.find(class_id);
    return (it != class_names_.end()) ? it->second : "cls:" + std::to_string(class_id);
}

} // namespace catcheye::detection
