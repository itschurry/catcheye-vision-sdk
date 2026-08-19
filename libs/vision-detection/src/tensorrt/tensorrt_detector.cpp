#include "catcheye/detection/tensorrt/tensorrt_detector.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <utility>

#include <NvInfer.h>
#include <cuda_fp16.h>
#include <cuda_runtime_api.h>
#include <opencv2/dnn/dnn.hpp>
#include <opencv2/imgproc.hpp>
#include <yaml-cpp/yaml.h>

#include "catcheye/detection/postprocess/postprocess.hpp"
#include "catcheye/detection/postprocess/tensor_view.hpp"
#include "catcheye/detection/postprocess/yolo_decoder.hpp"
#include "catcheye/input/pixel_format.hpp"

namespace catcheye::detection {
namespace {

class TensorRtLogger final : public nvinfer1::ILogger {
public:
    void log(Severity severity, const char* message) noexcept override
    {
        if (severity <= Severity::kWARNING && message != nullptr) {
            std::cerr << "TensorRT: " << message << '\n';
        }
    }
};

class DeviceBuffer {
public:
    DeviceBuffer() = default;
    ~DeviceBuffer() { reset(); }

    DeviceBuffer(const DeviceBuffer&) = delete;
    DeviceBuffer& operator=(const DeviceBuffer&) = delete;

    DeviceBuffer(DeviceBuffer&& other) noexcept
        : pointer_(std::exchange(other.pointer_, nullptr)),
          byte_size_(std::exchange(other.byte_size_, 0U))
    {
    }

    DeviceBuffer& operator=(DeviceBuffer&& other) noexcept
    {
        if (this != &other) {
            reset();
            pointer_ = std::exchange(other.pointer_, nullptr);
            byte_size_ = std::exchange(other.byte_size_, 0U);
        }
        return *this;
    }

    void allocate(std::size_t byte_size)
    {
        reset();
        if (byte_size == 0U) {
            throw std::runtime_error("cannot allocate an empty CUDA buffer");
        }
        check(cudaMalloc(&pointer_, byte_size), "cudaMalloc");
        byte_size_ = byte_size;
    }

    void* data() const { return pointer_; }
    std::size_t byte_size() const { return byte_size_; }

private:
    static void check(cudaError_t status, const char* operation)
    {
        if (status != cudaSuccess) {
            throw std::runtime_error(std::string(operation) + " failed: " + cudaGetErrorString(status));
        }
    }

    void reset() noexcept
    {
        if (pointer_ != nullptr) {
            cudaFree(pointer_);
            pointer_ = nullptr;
            byte_size_ = 0U;
        }
    }

    void* pointer_ = nullptr;
    std::size_t byte_size_ = 0U;
};

struct LetterboxResult {
    cv::Mat image;
    float scale = 1.0F;
    int pad_width = 0;
    int pad_height = 0;
};

struct OutputBinding {
    std::string name;
    nvinfer1::DataType data_type = nvinfer1::DataType::kFLOAT;
    nvinfer1::Dims dimensions{};
    std::size_t element_count = 0U;
    std::size_t byte_size = 0U;
    DeviceBuffer device;
    std::vector<std::uint8_t> host_bytes;
    std::vector<float> float_values;
};

void check_cuda(cudaError_t status, const char* operation)
{
    if (status != cudaSuccess) {
        throw std::runtime_error(std::string(operation) + " failed: " + cudaGetErrorString(status));
    }
}

std::vector<char> read_binary_file(const std::string& path)
{
    std::ifstream stream(path, std::ios::binary | std::ios::ate);
    if (!stream) {
        throw std::runtime_error("failed to open TensorRT engine: " + path);
    }
    const std::streamsize size = stream.tellg();
    if (size <= 0) {
        throw std::runtime_error("TensorRT engine is empty: " + path);
    }
    stream.seekg(0, std::ios::beg);
    std::vector<char> bytes(static_cast<std::size_t>(size));
    if (!stream.read(bytes.data(), size)) {
        throw std::runtime_error("failed to read TensorRT engine: " + path);
    }
    return bytes;
}

std::size_t data_type_size(nvinfer1::DataType data_type)
{
    switch (data_type) {
        case nvinfer1::DataType::kFLOAT:
            return sizeof(float);
        case nvinfer1::DataType::kHALF:
            return sizeof(__half);
        default:
            throw std::runtime_error("TensorRT detector supports only FP32 and FP16 tensors");
    }
}

std::size_t tensor_volume(const nvinfer1::Dims& dimensions)
{
    if (dimensions.nbDims <= 0) {
        throw std::runtime_error("TensorRT tensor has no dimensions");
    }
    std::size_t volume = 1U;
    for (int index = 0; index < dimensions.nbDims; ++index) {
        const std::int64_t dimension = dimensions.d[index];
        if (dimension <= 0) {
            throw std::runtime_error("TensorRT tensor has unresolved dimensions");
        }
        const auto value = static_cast<std::size_t>(dimension);
        if (volume > (std::numeric_limits<std::size_t>::max() / value)) {
            throw std::runtime_error("TensorRT tensor size overflow");
        }
        volume *= value;
    }
    return volume;
}

std::vector<int> tensor_shape(const nvinfer1::Dims& dimensions)
{
    std::vector<int> shape;
    shape.reserve(static_cast<std::size_t>(dimensions.nbDims));
    for (int index = 0; index < dimensions.nbDims; ++index) {
        const std::int64_t dimension = dimensions.d[index];
        if (dimension < static_cast<std::int64_t>(std::numeric_limits<int>::min()) ||
            dimension > static_cast<std::int64_t>(std::numeric_limits<int>::max())) {
            throw std::runtime_error("TensorRT dimension does not fit the decoder shape type");
        }
        shape.push_back(static_cast<int>(dimension));
    }
    return shape;
}

bool has_dynamic_dimension(const nvinfer1::Dims& dimensions)
{
    for (int index = 0; index < dimensions.nbDims; ++index) {
        if (dimensions.d[index] < 0) {
            return true;
        }
    }
    return false;
}

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
            cv::Mat wrapped(frame.height + (frame.height / 2), frame.width, CV_8UC1, raw,
                            static_cast<std::size_t>(frame.stride));
            cv::Mat bgr;
            cv::cvtColor(wrapped, bgr, cv::COLOR_YUV2BGR_NV12);
            return bgr;
        }
        case catcheye::input::PixelFormat::UNKNOWN:
        default:
            return {};
    }
}

LetterboxResult letterbox(const cv::Mat& image, int target_width, int target_height)
{
    LetterboxResult result;
    result.scale = std::min(
        static_cast<float>(target_width) / static_cast<float>(image.cols),
        static_cast<float>(target_height) / static_cast<float>(image.rows));

    const int resized_width = static_cast<int>(std::round(static_cast<float>(image.cols) * result.scale));
    const int resized_height = static_cast<int>(std::round(static_cast<float>(image.rows) * result.scale));
    cv::Mat resized;
    cv::resize(image, resized, cv::Size(resized_width, resized_height));

    result.pad_width = target_width - resized_width;
    result.pad_height = target_height - resized_height;
    const int left = result.pad_width / 2;
    const int right = result.pad_width - left;
    const int top = result.pad_height / 2;
    const int bottom = result.pad_height - top;
    cv::copyMakeBorder(resized, result.image, top, bottom, left, right,
                       cv::BORDER_CONSTANT, cv::Scalar(114, 114, 114));
    return result;
}

std::map<int, std::string> load_class_names(const std::string& yaml_path)
{
    std::map<int, std::string> class_names;
    if (yaml_path.empty()) {
        return class_names;
    }
    const YAML::Node root = YAML::LoadFile(yaml_path);
    const YAML::Node names = root["names"];
    if (!names) {
        throw std::runtime_error("metadata has no names field: " + yaml_path);
    }
    if (names.IsSequence()) {
        for (std::size_t index = 0; index < names.size(); ++index) {
            class_names[static_cast<int>(index)] = names[index].as<std::string>();
        }
    } else if (names.IsMap()) {
        for (const auto& entry : names) {
            class_names[entry.first.as<int>()] = entry.second.as<std::string>();
        }
    } else {
        throw std::runtime_error("metadata names must be a sequence or map: " + yaml_path);
    }
    return class_names;
}

PostprocessOptions make_postprocess_options(const TensorRtDetectorConfig& config)
{
    return PostprocessOptions {
        .confidence_threshold = config.confidence_threshold,
        .nms_threshold = config.nms_threshold,
        .class_aware_nms = true,
        .apply_nms = true,
        .allowed_class_ids = config.allowed_class_ids,
    };
}

} // namespace

struct TensorRtDetector::Impl {
    TensorRtLogger logger;
    std::unique_ptr<nvinfer1::IRuntime> runtime;
    std::unique_ptr<nvinfer1::ICudaEngine> engine;
    std::unique_ptr<nvinfer1::IExecutionContext> context;
    cudaStream_t stream = nullptr;
    std::string input_name;
    nvinfer1::DataType input_data_type = nvinfer1::DataType::kFLOAT;
    nvinfer1::Dims input_dimensions{};
    int input_width = 0;
    int input_height = 0;
    std::size_t input_element_count = 0U;
    DeviceBuffer input_device;
    std::vector<OutputBinding> outputs;
    std::optional<YoloDecoder> decoder;

    ~Impl()
    {
        if (stream != nullptr) {
            cudaStreamDestroy(stream);
        }
    }
};

TensorRtDetector::TensorRtDetector(TensorRtDetectorConfig config)
    : config_(std::move(config))
{
}

TensorRtDetector::~TensorRtDetector() = default;
TensorRtDetector::TensorRtDetector(TensorRtDetector&&) noexcept = default;
TensorRtDetector& TensorRtDetector::operator=(TensorRtDetector&&) noexcept = default;

bool TensorRtDetector::initialize()
{
    if (initialized_) {
        return true;
    }
    if (config_.engine_path.empty()) {
        std::cerr << "TensorRT engine path is required\n";
        return false;
    }

    try {
        check_cuda(cudaSetDevice(config_.device_id), "cudaSetDevice");
        impl_ = std::make_unique<Impl>();
        const std::vector<char> engine_bytes = read_binary_file(config_.engine_path);
        impl_->runtime.reset(nvinfer1::createInferRuntime(impl_->logger));
        if (!impl_->runtime) {
            throw std::runtime_error("failed to create TensorRT runtime");
        }
        impl_->engine.reset(impl_->runtime->deserializeCudaEngine(engine_bytes.data(), engine_bytes.size()));
        if (!impl_->engine) {
            throw std::runtime_error("failed to deserialize TensorRT engine");
        }
        impl_->context.reset(impl_->engine->createExecutionContext());
        if (!impl_->context) {
            throw std::runtime_error("failed to create TensorRT execution context");
        }

        for (int index = 0; index < impl_->engine->getNbIOTensors(); ++index) {
            const char* tensor_name = impl_->engine->getIOTensorName(index);
            if (tensor_name == nullptr) {
                continue;
            }
            if (impl_->engine->getTensorIOMode(tensor_name) == nvinfer1::TensorIOMode::kINPUT) {
                if (!impl_->input_name.empty()) {
                    throw std::runtime_error("TensorRT detector expects exactly one input tensor");
                }
                impl_->input_name = tensor_name;
            }
        }
        if (impl_->input_name.empty()) {
            throw std::runtime_error("TensorRT engine has no input tensor");
        }

        nvinfer1::Dims input_dimensions = impl_->engine->getTensorShape(impl_->input_name.c_str());
        if (input_dimensions.nbDims != 4) {
            throw std::runtime_error("TensorRT detector expects a four-dimensional NCHW input");
        }
        const auto channel_count = input_dimensions.d[1];
        if (channel_count != 3 && channel_count != -1) {
            throw std::runtime_error("TensorRT detector expects a three-channel NCHW input");
        }
        const int input_height = input_dimensions.d[2] > 0
            ? static_cast<int>(input_dimensions.d[2])
            : config_.input_height;
        const int input_width = input_dimensions.d[3] > 0
            ? static_cast<int>(input_dimensions.d[3])
            : config_.input_width;
        if (input_width <= 0 || input_height <= 0) {
            throw std::runtime_error("dynamic TensorRT input requires input_width and input_height");
        }
        input_dimensions.d[0] = 1;
        input_dimensions.d[1] = 3;
        input_dimensions.d[2] = input_height;
        input_dimensions.d[3] = input_width;
        if (has_dynamic_dimension(impl_->engine->getTensorShape(impl_->input_name.c_str())) &&
            !impl_->context->setInputShape(impl_->input_name.c_str(), input_dimensions)) {
            throw std::runtime_error("failed to set TensorRT input shape");
        }

        impl_->input_dimensions = input_dimensions;
        impl_->input_width = input_width;
        impl_->input_height = input_height;
        impl_->input_data_type = impl_->engine->getTensorDataType(impl_->input_name.c_str());
        impl_->input_element_count = tensor_volume(input_dimensions);
        impl_->input_device.allocate(impl_->input_element_count * data_type_size(impl_->input_data_type));
        if (!impl_->context->setTensorAddress(impl_->input_name.c_str(), impl_->input_device.data())) {
            throw std::runtime_error("failed to bind TensorRT input tensor");
        }

        for (int index = 0; index < impl_->engine->getNbIOTensors(); ++index) {
            const char* tensor_name = impl_->engine->getIOTensorName(index);
            if (tensor_name == nullptr ||
                impl_->engine->getTensorIOMode(tensor_name) != nvinfer1::TensorIOMode::kOUTPUT) {
                continue;
            }
            OutputBinding output;
            output.name = tensor_name;
            output.data_type = impl_->engine->getTensorDataType(tensor_name);
            output.dimensions = impl_->context->getTensorShape(tensor_name);
            output.element_count = tensor_volume(output.dimensions);
            output.byte_size = output.element_count * data_type_size(output.data_type);
            output.device.allocate(output.byte_size);
            output.host_bytes.resize(output.byte_size);
            output.float_values.resize(output.element_count);
            if (!impl_->context->setTensorAddress(output.name.c_str(), output.device.data())) {
                throw std::runtime_error("failed to bind TensorRT output tensor: " + output.name);
            }
            impl_->outputs.push_back(std::move(output));
        }
        if (impl_->outputs.empty()) {
            throw std::runtime_error("TensorRT engine has no output tensors");
        }
        check_cuda(cudaStreamCreate(&impl_->stream), "cudaStreamCreate");

        class_names_ = load_class_names(config_.metadata_path);
        const int num_classes = config_.num_classes > 0
            ? config_.num_classes
            : static_cast<int>(class_names_.size());
        if (num_classes <= 0) {
            throw std::runtime_error("TensorRT detector requires class names or num_classes");
        }
        impl_->decoder.emplace(YoloDecoderOptions {
            .num_classes = num_classes,
            .requires_nms = config_.requires_nms,
            .output_format = config_.output_format,
        });

        std::cerr << "TensorRT detector initialized: engine='" << config_.engine_path
                  << "', input=" << impl_->input_width << "x" << impl_->input_height
                  << ", outputs=" << impl_->outputs.size() << '\n';
        initialized_ = true;
        return true;
    } catch (const std::exception& exception) {
        std::cerr << "failed to initialize TensorRT detector: " << exception.what() << '\n';
        impl_.reset();
        return false;
    }
}

bool TensorRtDetector::is_initialized() const
{
    return initialized_;
}

std::vector<Detection> TensorRtDetector::detect(const catcheye::input::Frame& frame)
{
    if (!initialized_ || frame.empty()) {
        return {};
    }

    try {
        const cv::Mat bgr = frame_to_bgr(frame);
        if (bgr.empty()) {
            return {};
        }
        const LetterboxResult preprocessed = letterbox(bgr, impl_->input_width, impl_->input_height);
        cv::Mat blob;
        cv::dnn::blobFromImage(
            preprocessed.image,
            blob,
            1.0 / 255.0,
            cv::Size(impl_->input_width, impl_->input_height),
            cv::Scalar(),
            true,
            false,
            CV_32F);
        if (!blob.isContinuous() || blob.total() != impl_->input_element_count) {
            throw std::runtime_error("unexpected TensorRT input blob layout");
        }

        if (impl_->input_data_type == nvinfer1::DataType::kFLOAT) {
            check_cuda(cudaMemcpyAsync(
                impl_->input_device.data(),
                blob.ptr<float>(),
                impl_->input_device.byte_size(),
                cudaMemcpyHostToDevice,
                impl_->stream), "cudaMemcpyAsync input FP32");
        } else if (impl_->input_data_type == nvinfer1::DataType::kHALF) {
            const float* source = blob.ptr<float>();
            std::vector<__half> half_values(impl_->input_element_count);
            std::transform(source, source + impl_->input_element_count, half_values.begin(),
                           [](float value) { return __float2half(value); });
            check_cuda(cudaMemcpyAsync(
                impl_->input_device.data(),
                half_values.data(),
                impl_->input_device.byte_size(),
                cudaMemcpyHostToDevice,
                impl_->stream), "cudaMemcpyAsync input FP16");
        } else {
            throw std::runtime_error("unsupported TensorRT input data type");
        }

        if (!impl_->context->enqueueV3(impl_->stream)) {
            throw std::runtime_error("TensorRT enqueueV3 failed");
        }
        for (OutputBinding& output : impl_->outputs) {
            check_cuda(cudaMemcpyAsync(
                output.host_bytes.data(),
                output.device.data(),
                output.byte_size,
                cudaMemcpyDeviceToHost,
                impl_->stream), "cudaMemcpyAsync output");
        }
        check_cuda(cudaStreamSynchronize(impl_->stream), "cudaStreamSynchronize");

        std::vector<TensorView> tensor_views;
        tensor_views.reserve(impl_->outputs.size());
        for (OutputBinding& output : impl_->outputs) {
            if (output.data_type == nvinfer1::DataType::kFLOAT) {
                std::memcpy(output.float_values.data(), output.host_bytes.data(), output.byte_size);
            } else if (output.data_type == nvinfer1::DataType::kHALF) {
                for (std::size_t index = 0; index < output.element_count; ++index) {
                    __half value;
                    std::memcpy(
                        &value,
                        output.host_bytes.data() + (index * sizeof(__half)),
                        sizeof(__half));
                    output.float_values[index] = __half2float(value);
                }
            }
            tensor_views.push_back(TensorView {
                .name = output.name,
                .data = output.float_values.data(),
                .byte_size = output.float_values.size() * sizeof(float),
                .shape = tensor_shape(output.dimensions),
                .data_type = TensorDataType::Float32,
            });
        }

        const ModelDecodeContext decode_context {
            .input_width = impl_->input_width,
            .input_height = impl_->input_height,
            .original_width = frame.width,
            .original_height = frame.height,
            .letterbox_scale = preprocessed.scale,
            .pad_width = preprocessed.pad_width,
            .pad_height = preprocessed.pad_height,
        };
        const DecodeResult decoded = impl_->decoder->decode(tensor_views, decode_context);
        return finalize_detections(decoded, make_postprocess_options(config_));
    } catch (const std::exception& exception) {
        std::cerr << "TensorRT inference failed: " << exception.what() << '\n';
        return {};
    }
}

std::string TensorRtDetector::class_name(int class_id) const
{
    const auto found = class_names_.find(class_id);
    return found == class_names_.end() ? std::to_string(class_id) : found->second;
}

} // namespace catcheye::detection
