#include "catcheye/detection/hailo/hailo_detector.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <utility>

#include <opencv2/imgproc.hpp>
#include <yaml-cpp/yaml.h>

#include "catcheye/input/pixel_format.hpp"
#include "hailo/hailort.hpp"

#if defined(__unix__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace catcheye::detection {
namespace {

struct LetterboxResult {
    cv::Mat image;
    float scale = 1.0F;
    int pad_width = 0;
    int pad_height = 0;
};

struct OutputInfo {
    hailo_format_order_t order = HAILO_FORMAT_ORDER_AUTO;
    hailo_nms_shape_t nms_shape{};
    bool is_nms = false;
    std::size_t frame_size = 0;
};

struct AlignedBuffer {
    std::shared_ptr<std::uint8_t> data;
    std::size_t size = 0;
    std::size_t allocated_size = 0;
};

std::size_t page_size()
{
#if defined(__unix__)
    const long value = sysconf(_SC_PAGESIZE);
    return value > 0 ? static_cast<std::size_t>(value) : 4096U;
#else
    return 4096U;
#endif
}

std::size_t align_to_page(std::size_t size)
{
    const std::size_t page = page_size();
    return ((size + page - 1U) / page) * page;
}

AlignedBuffer allocate_aligned_buffer(std::size_t size)
{
    const std::size_t allocated_size = align_to_page(size);
#if defined(__unix__)
    void* address = mmap(nullptr, allocated_size, PROT_WRITE | PROT_READ, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (address == MAP_FAILED) {
        throw std::bad_alloc();
    }
    return AlignedBuffer {
        .data = std::shared_ptr<std::uint8_t>(
            reinterpret_cast<std::uint8_t*>(address),
            [allocated_size](std::uint8_t* ptr) {
                munmap(ptr, allocated_size);
            }),
        .size = size,
        .allocated_size = allocated_size,
    };
#else
    return AlignedBuffer {
        .data = std::shared_ptr<std::uint8_t>(new std::uint8_t[allocated_size], std::default_delete<std::uint8_t[]>()),
        .size = size,
        .allocated_size = allocated_size,
    };
#endif
}

cv::Mat frame_to_bgr(const catcheye::input::Frame& frame)
{
    if (frame.empty() || frame.width <= 0 || frame.height <= 0 || frame.stride <= 0) {
        return {};
    }

    const std::size_t expected_size = catcheye::input::frame_data_size(frame.format, frame.stride, frame.height);
    if (frame.data.size() < expected_size) {
        std::cerr << "frame buffer too small: actual=" << frame.data.size()
                  << ", expected=" << expected_size
                  << ", format=" << static_cast<int>(frame.format) << '\n';
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
        default:
            break;
    }

    std::cerr << "unsupported pixel format: " << static_cast<int>(frame.format) << '\n';
    return {};
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

    cv::copyMakeBorder(resized, result.image, top, bottom, left, right, cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));
    return result;
}

cv::Mat convert_to_hailo_input(const cv::Mat& bgr_image, std::uint32_t features)
{
    cv::Mat converted;
    switch (features) {
        case 1:
            cv::cvtColor(bgr_image, converted, cv::COLOR_BGR2GRAY);
            return converted;
        case 3:
            cv::cvtColor(bgr_image, converted, cv::COLOR_BGR2RGB);
            return converted;
        case 4:
            cv::cvtColor(bgr_image, converted, cv::COLOR_BGR2RGBA);
            return converted;
        default:
            std::cerr << "unsupported Hailo input feature count: " << features << '\n';
            return {};
    }
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

BoundingBox map_hailo_box(float x_min, float y_min, float x_max, float y_max,
                          const LetterboxResult& letterbox_result,
                          int original_width, int original_height,
                          int input_width, int input_height)
{
    if (std::max({std::abs(x_min), std::abs(y_min), std::abs(x_max), std::abs(y_max)}) <= 1.5F) {
        x_min *= static_cast<float>(input_width);
        x_max *= static_cast<float>(input_width);
        y_min *= static_cast<float>(input_height);
        y_max *= static_cast<float>(input_height);
    }

    const float left = static_cast<float>(letterbox_result.pad_width / 2);
    const float top = static_cast<float>(letterbox_result.pad_height / 2);

    x_min = (x_min - left) / letterbox_result.scale;
    x_max = (x_max - left) / letterbox_result.scale;
    y_min = (y_min - top) / letterbox_result.scale;
    y_max = (y_max - top) / letterbox_result.scale;

    x_min = std::clamp(x_min, 0.0F, static_cast<float>(original_width - 1));
    x_max = std::clamp(x_max, 0.0F, static_cast<float>(original_width - 1));
    y_min = std::clamp(y_min, 0.0F, static_cast<float>(original_height - 1));
    y_max = std::clamp(y_max, 0.0F, static_cast<float>(original_height - 1));

    return BoundingBox {
        .x = x_min,
        .y = y_min,
        .width = std::max(0.0F, x_max - x_min),
        .height = std::max(0.0F, y_max - y_min),
    };
}

void append_detection(std::vector<Detection>& detections,
                      int class_id,
                      float score,
                      float x_min,
                      float y_min,
                      float x_max,
                      float y_max,
                      float confidence_threshold,
                      const LetterboxResult& letterbox_result,
                      int original_width,
                      int original_height,
                      int input_width,
                      int input_height)
{
    if (score < confidence_threshold) {
        return;
    }

    BoundingBox box = map_hailo_box(
        x_min, y_min, x_max, y_max,
        letterbox_result,
        original_width,
        original_height,
        input_width,
        input_height);

    if (box.width <= 1.0F || box.height <= 1.0F) {
        return;
    }

    detections.push_back(Detection {
        .class_id = class_id,
        .score = score,
        .box = box,
    });
}

std::vector<Detection> decode_nms_by_class(const std::uint8_t* data,
                                           std::size_t size,
                                           const hailo_nms_shape_t& shape,
                                           const HailoDetectorConfig& config,
                                           const LetterboxResult& letterbox_result,
                                           int original_width,
                                           int original_height,
                                           int input_width,
                                           int input_height)
{
    std::vector<Detection> detections;
    const float* values = reinterpret_cast<const float*>(data);
    const std::size_t float_count = size / sizeof(float);
    std::size_t offset = 0;

    for (std::uint32_t class_id = 0; class_id < shape.number_of_classes; ++class_id) {
        if (offset >= float_count) {
            break;
        }

        const auto bbox_count = static_cast<std::uint32_t>(std::max(0.0F, values[offset++]));
        const std::uint32_t boxes_to_read = std::min(bbox_count, shape.max_bboxes_per_class);
        for (std::uint32_t box_index = 0; box_index < shape.max_bboxes_per_class; ++box_index) {
            if ((offset + 5U) > float_count) {
                return detections;
            }

            const float y_min = values[offset++];
            const float x_min = values[offset++];
            const float y_max = values[offset++];
            const float x_max = values[offset++];
            const float score = values[offset++];

            if (box_index >= boxes_to_read) {
                continue;
            }

            append_detection(
                detections,
                static_cast<int>(class_id),
                score,
                x_min,
                y_min,
                x_max,
                y_max,
                config.confidence_threshold,
                letterbox_result,
                original_width,
                original_height,
                input_width,
                input_height);
        }
    }

    return detections;
}

std::vector<Detection> decode_nms_by_score(const std::uint8_t* data,
                                           std::size_t size,
                                           const HailoDetectorConfig& config,
                                           const LetterboxResult& letterbox_result,
                                           int original_width,
                                           int original_height,
                                           int input_width,
                                           int input_height)
{
    std::vector<Detection> detections;
    if (size < sizeof(hailo_detections_t)) {
        return detections;
    }

    const auto* hailo_detections = reinterpret_cast<const hailo_detections_t*>(data);
    const std::size_t max_count = (size - sizeof(hailo_detections_t)) / sizeof(hailo_detection_t);
    const std::size_t count = std::min<std::size_t>(hailo_detections->count, max_count);

    detections.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const hailo_detection_t& item = hailo_detections->detections[i];
        append_detection(
            detections,
            static_cast<int>(item.class_id),
            item.score,
            item.x_min,
            item.y_min,
            item.x_max,
            item.y_max,
            config.confidence_threshold,
            letterbox_result,
            original_width,
            original_height,
            input_width,
            input_height);
    }

    return detections;
}

} // namespace

struct HailoDetector::Impl {
    std::unique_ptr<hailort::VDevice> vdevice;
    std::shared_ptr<hailort::InferModel> infer_model;
    std::unique_ptr<hailort::ConfiguredInferModel> configured_model;
    std::string input_name;
    std::vector<std::string> output_names;
    std::map<std::string, OutputInfo> output_infos;
    hailo_3d_image_shape_t input_shape{};
    std::size_t input_frame_size = 0;
};

HailoDetector::HailoDetector(HailoDetectorConfig config)
    : config_(std::move(config))
{
}

HailoDetector::~HailoDetector() = default;
HailoDetector::HailoDetector(HailoDetector&&) noexcept = default;
HailoDetector& HailoDetector::operator=(HailoDetector&&) noexcept = default;

bool HailoDetector::initialize()
{
    if (initialized_) {
        return true;
    }

    if (config_.hef_path.empty()) {
        std::cerr << "Hailo HEF path is required\n";
        return false;
    }

    try {
        impl_ = std::make_unique<Impl>();
        impl_->vdevice = hailort::VDevice::create().expect("failed to create Hailo VDevice");
        impl_->infer_model = impl_->vdevice->create_infer_model(config_.hef_path).expect("failed to create Hailo infer model");

        const auto& input_names = impl_->infer_model->get_input_names();
        if (input_names.empty()) {
            std::cerr << "Hailo model has no input streams\n";
            return false;
        }
        impl_->input_name = config_.input_name.empty() ? input_names.front() : config_.input_name;

        auto input_stream = impl_->infer_model->input(impl_->input_name).expect("failed to get Hailo input stream");
        input_stream.set_format_type(HAILO_FORMAT_TYPE_UINT8);
        input_stream.set_format_order(HAILO_FORMAT_ORDER_NHWC);
        impl_->input_shape = input_stream.shape();
        impl_->input_frame_size = input_stream.get_frame_size();
        if (impl_->input_shape.width == 0 || impl_->input_shape.height == 0 || impl_->input_shape.features == 0) {
            std::cerr << "Hailo input stream has invalid shape: height=" << impl_->input_shape.height
                      << ", width=" << impl_->input_shape.width
                      << ", features=" << impl_->input_shape.features << '\n';
            return false;
        }
        std::cerr << "Hailo input stream: name='" << impl_->input_name
                  << "', shape=" << impl_->input_shape.width << "x" << impl_->input_shape.height
                  << "x" << impl_->input_shape.features
                  << ", frame_size=" << impl_->input_frame_size << '\n';

        const auto& output_names = impl_->infer_model->get_output_names();
        if (output_names.empty()) {
            std::cerr << "Hailo model has no output streams\n";
            return false;
        }
        if (config_.output_name.empty()) {
            impl_->output_names = output_names;
        } else {
            impl_->output_names = {config_.output_name};
        }

        for (const std::string& output_name : impl_->output_names) {
            auto output_stream = impl_->infer_model->output(output_name).expect("failed to get Hailo output stream");
            OutputInfo info;
            info.is_nms = output_stream.is_nms();

            if (info.is_nms) {
                output_stream.set_nms_score_threshold(config_.confidence_threshold);
                output_stream.set_nms_iou_threshold(config_.nms_threshold);
                output_stream.set_nms_max_proposals_per_class(static_cast<std::uint32_t>(config_.max_proposals_per_class));
                output_stream.set_nms_max_proposals_total(static_cast<std::uint32_t>(config_.max_proposals_total));

                const HailoOutputDecoder decoder =
                    config_.output_decoder == HailoOutputDecoder::Auto ? HailoOutputDecoder::NmsByClass : config_.output_decoder;
                if (decoder == HailoOutputDecoder::NmsByScore) {
                    output_stream.set_format_order(HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE);
                    output_stream.set_format_type(HAILO_FORMAT_TYPE_UINT8);
                } else {
                    output_stream.set_format_order(HAILO_FORMAT_ORDER_HAILO_NMS_BY_CLASS);
                    output_stream.set_format_type(HAILO_FORMAT_TYPE_FLOAT32);
                }

                info.nms_shape = output_stream.get_nms_shape().expect("failed to get Hailo NMS shape");
            } else {
                output_stream.set_format_type(HAILO_FORMAT_TYPE_FLOAT32);
            }

            info.order = output_stream.format().order;
            info.frame_size = output_stream.get_frame_size();
            impl_->output_infos[output_name] = info;
        }

        auto configured = impl_->infer_model->configure().expect("failed to configure Hailo infer model");
        impl_->configured_model = std::make_unique<hailort::ConfiguredInferModel>(std::move(configured));
        class_names_ = load_class_names(config_.metadata_path);
        initialized_ = true;
        return true;
    } catch (const hailort::hailort_error& exception) {
        std::cerr << "failed to initialize Hailo detector: status=" << exception.status()
                  << ", error=" << exception.what() << '\n';
    } catch (const std::exception& exception) {
        std::cerr << "failed to initialize Hailo detector: " << exception.what() << '\n';
    }

    impl_.reset();
    return false;
}

bool HailoDetector::is_initialized() const
{
    return initialized_;
}

std::vector<Detection> HailoDetector::detect(const catcheye::input::Frame& frame)
{
    if (!initialized_ || frame.empty()) {
        return {};
    }

    const cv::Mat bgr = frame_to_bgr(frame);
    if (bgr.empty()) {
        return {};
    }

    const int input_width = static_cast<int>(impl_->input_shape.width);
    const int input_height = static_cast<int>(impl_->input_shape.height);
    const LetterboxResult preprocessed = letterbox(bgr, input_width, input_height);

    cv::Mat model_input = convert_to_hailo_input(preprocessed.image, impl_->input_shape.features);
    if (model_input.empty()) {
        return {};
    }
    if (!model_input.isContinuous()) {
        model_input = model_input.clone();
    }

    const std::size_t packed_input_bytes = model_input.total() * model_input.elemSize();
    const auto input_height_size = static_cast<std::size_t>(input_height);
    const std::size_t packed_row_bytes = static_cast<std::size_t>(model_input.cols) * model_input.elemSize();
    const bool can_copy_with_row_padding =
        input_height_size > 0 &&
        (impl_->input_frame_size % input_height_size) == 0 &&
        (impl_->input_frame_size / input_height_size) >= packed_row_bytes &&
        model_input.rows == input_height;

    if (packed_input_bytes != impl_->input_frame_size && !can_copy_with_row_padding) {
        std::cerr << "Hailo input frame size mismatch: actual=" << packed_input_bytes
                  << ", expected=" << impl_->input_frame_size
                  << ", shape=" << impl_->input_shape.width << "x" << impl_->input_shape.height
                  << "x" << impl_->input_shape.features << '\n';
        return {};
    }

    try {
        auto bindings = impl_->configured_model->create_bindings().expect("failed to create Hailo bindings");

        AlignedBuffer input_buffer = allocate_aligned_buffer(impl_->input_frame_size);
        if (packed_input_bytes == impl_->input_frame_size) {
            std::memcpy(input_buffer.data.get(), model_input.data, impl_->input_frame_size);
        } else {
            const std::size_t dst_row_bytes = impl_->input_frame_size / input_height_size;
            std::memset(input_buffer.data.get(), 0, impl_->input_frame_size);
            for (int row = 0; row < model_input.rows; ++row) {
                std::memcpy(
                    input_buffer.data.get() + (static_cast<std::size_t>(row) * dst_row_bytes),
                    model_input.ptr(row),
                    packed_row_bytes);
            }
        }

        auto input = bindings.input(impl_->input_name).expect("failed to bind Hailo input");
        hailo_status status = input.set_buffer(hailort::MemoryView(input_buffer.data.get(), input_buffer.size));
        if (status != HAILO_SUCCESS) {
            throw hailort::hailort_error(status, "failed to set Hailo input buffer");
        }

        std::map<std::string, AlignedBuffer> output_buffers;
        for (const std::string& output_name : impl_->output_names) {
            const auto info_it = impl_->output_infos.find(output_name);
            if (info_it == impl_->output_infos.end()) {
                continue;
            }

            AlignedBuffer output_buffer = allocate_aligned_buffer(info_it->second.frame_size);
            auto output = bindings.output(output_name).expect("failed to bind Hailo output");
            status = output.set_buffer(hailort::MemoryView(output_buffer.data.get(), output_buffer.size));
            if (status != HAILO_SUCCESS) {
                throw hailort::hailort_error(status, "failed to set Hailo output buffer");
            }
            output_buffers.emplace(output_name, std::move(output_buffer));
        }

        status = impl_->configured_model->run(bindings, std::chrono::milliseconds(config_.inference_timeout_ms));
        if (status != HAILO_SUCCESS) {
            throw hailort::hailort_error(status, "failed to run Hailo inference");
        }

        std::vector<Detection> detections;
        for (const auto& [output_name, output_buffer] : output_buffers) {
            const OutputInfo& info = impl_->output_infos.at(output_name);
            if (!info.is_nms) {
                std::cerr << "Hailo output '" << output_name << "' is not NMS; raw output decoding is not configured\n";
                continue;
            }

            std::vector<Detection> decoded;
            if (info.order == HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE) {
                decoded = decode_nms_by_score(
                    output_buffer.data.get(),
                    output_buffer.size,
                    config_,
                    preprocessed,
                    frame.width,
                    frame.height,
                    input_width,
                    input_height);
            } else {
                decoded = decode_nms_by_class(
                    output_buffer.data.get(),
                    output_buffer.size,
                    info.nms_shape,
                    config_,
                    preprocessed,
                    frame.width,
                    frame.height,
                    input_width,
                    input_height);
            }

            detections.insert(detections.end(), decoded.begin(), decoded.end());
        }

        return detections;
    } catch (const hailort::hailort_error& exception) {
        std::cerr << "Hailo inference failed: status=" << exception.status()
                  << ", error=" << exception.what() << '\n';
    } catch (const std::exception& exception) {
        std::cerr << "Hailo inference failed: " << exception.what() << '\n';
    }

    return {};
}

std::string HailoDetector::class_name(int class_id) const
{
    const auto it = class_names_.find(class_id);
    return (it != class_names_.end()) ? it->second : "cls:" + std::to_string(class_id);
}

} // namespace catcheye::detection
