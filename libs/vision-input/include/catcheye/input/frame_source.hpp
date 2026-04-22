#pragma once

#include <memory>
#include <string>

#include "catcheye/input/frame.hpp"

namespace catcheye::input {

enum class InputSourceType {
    Camera,
    ImageFile,
    VideoFile,
};

struct InputSourceConfig {
    InputSourceType type = InputSourceType::Camera;
    std::string uri;
    std::string camera_pipeline;
    std::string camera_device;
    int camera_width = 1280;
    int camera_height = 720;
};

enum class FrameReadStatus {
    Ok,
    EndOfStream,
    Error,
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

std::unique_ptr<FrameSource> create_frame_source(const InputSourceConfig& config);
std::string default_camera_pipeline();

} // namespace catcheye::input
