#pragma once

#include <memory>
#include <string>

#include <opencv2/videoio.hpp>

#include "catcheye/input/frame.hpp"

namespace catcheye::input {

enum class FrameReadStatus {
    Ok,
    EndOfStream,
    Error,
};

enum class InputSourceType {
    Camera,
    VideoFile,
    ImageFile,
};

struct InputSourceConfig {
    InputSourceType type = InputSourceType::Camera;
    std::string uri;
    int api_preference = cv::CAP_ANY;
    std::string camera_pipeline;
};

class FrameSource {
   public:
    virtual ~FrameSource() = default;

    virtual bool open() = 0;
    virtual bool is_open() const = 0;
    virtual FrameReadStatus read(Frame& frame) = 0;
    virtual void close() = 0;
    virtual std::string describe() const = 0;
};

class OpenCvCaptureSource final : public FrameSource {
   public:
    explicit OpenCvCaptureSource(InputSourceConfig config);

    bool open() override;
    bool is_open() const override;
    FrameReadStatus read(Frame& frame) override;
    void close() override;
    std::string describe() const override;

   private:
    InputSourceConfig config_;
    cv::VideoCapture capture_;
};

class ImageFileSource final : public FrameSource {
   public:
    explicit ImageFileSource(InputSourceConfig config);

    bool open() override;
    bool is_open() const override;
    FrameReadStatus read(Frame& frame) override;
    void close() override;
    std::string describe() const override;

   private:
    InputSourceConfig config_;
    cv::Mat image_;
    bool opened_ = false;
    bool delivered_ = false;
};

std::unique_ptr<FrameSource> create_frame_source(InputSourceConfig config);
std::string default_camera_pipeline();

} // namespace catcheye::input
