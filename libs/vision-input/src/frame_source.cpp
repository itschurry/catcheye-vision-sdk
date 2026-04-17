#include "catcheye/input/frame_source.hpp"

#include <chrono>
#include <utility>

#include <opencv2/imgcodecs.hpp>

namespace catcheye::input {
namespace {

std::string source_type_name(InputSourceType type) {
    switch (type) {
        case InputSourceType::Camera:
            return "camera";
        case InputSourceType::VideoFile:
            return "video";
        case InputSourceType::ImageFile:
            return "image";
    }

    return "unknown";
}

Timestamp now_millis() {
    return static_cast<Timestamp>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::string capture_target(const InputSourceConfig& config) {
    if (config.type == InputSourceType::Camera) {
        if (!config.camera_pipeline.empty()) {
            return config.camera_pipeline;
        }
        return default_camera_pipeline();
    }

    return config.uri;
}

int capture_api_preference(const InputSourceConfig& config) {
    if (config.type == InputSourceType::Camera && config.api_preference == cv::CAP_ANY) {
        return cv::CAP_GSTREAMER;
    }
    return config.api_preference;
}

} // namespace

OpenCvCaptureSource::OpenCvCaptureSource(InputSourceConfig config)
    : config_(std::move(config)) {}

bool OpenCvCaptureSource::open() {
    if (capture_.isOpened()) {
        return true;
    }

    return capture_.open(capture_target(config_), capture_api_preference(config_));
}

bool OpenCvCaptureSource::is_open() const {
    return capture_.isOpened();
}

FrameReadStatus OpenCvCaptureSource::read(Frame& frame) {
    cv::Mat image;
    if (!capture_.read(image)) {
        return capture_.isOpened() ? FrameReadStatus::EndOfStream : FrameReadStatus::Error;
    }
    if (image.empty()) {
        return FrameReadStatus::Error;
    }

    frame.image = std::move(image);
    frame.format = PixelFormat::BGR;
    frame.timestamp = now_millis();
    return FrameReadStatus::Ok;
}

void OpenCvCaptureSource::close() {
    if (capture_.isOpened()) {
        capture_.release();
    }
}

std::string OpenCvCaptureSource::describe() const {
    return source_type_name(config_.type) + ":" + capture_target(config_);
}

ImageFileSource::ImageFileSource(InputSourceConfig config)
    : config_(std::move(config)) {}

bool ImageFileSource::open() {
    if (opened_) {
        return true;
    }

    image_ = cv::imread(config_.uri, cv::IMREAD_COLOR);
    opened_ = !image_.empty();
    delivered_ = false;
    return opened_;
}

bool ImageFileSource::is_open() const {
    return opened_;
}

FrameReadStatus ImageFileSource::read(Frame& frame) {
    if (!opened_) {
        return FrameReadStatus::Error;
    }
    if (delivered_) {
        return FrameReadStatus::EndOfStream;
    }
    if (image_.empty()) {
        return FrameReadStatus::Error;
    }

    frame.image = image_.clone();
    frame.format = PixelFormat::BGR;
    frame.timestamp = now_millis();
    delivered_ = true;
    return FrameReadStatus::Ok;
}

void ImageFileSource::close() {
    image_.release();
    opened_ = false;
    delivered_ = false;
}

std::string ImageFileSource::describe() const {
    return source_type_name(config_.type) + ":" + config_.uri;
}

std::unique_ptr<FrameSource> create_frame_source(InputSourceConfig config) {
    switch (config.type) {
        case InputSourceType::Camera:
        case InputSourceType::VideoFile:
            return std::make_unique<OpenCvCaptureSource>(std::move(config));
        case InputSourceType::ImageFile:
            return std::make_unique<ImageFileSource>(std::move(config));
    }

    return nullptr;
}

std::string default_camera_pipeline() {
    return "libcamerasrc ! "
           "video/x-raw,width=640,height=480,framerate=15/1,format=NV12 ! "
           "videoflip video-direction=vert ! "
           "videoconvert ! "
           "video/x-raw,format=BGR ! "
           "appsink drop=true max-buffers=1 sync=false";
}

} // namespace catcheye::input
