#include "catcheye/input/frame_source.hpp"

#include <memory>
#include <stdexcept>

#include "catcheye/input/gstreamer_source.hpp"
#include "catcheye/input/libcamera_source.hpp"

namespace catcheye::input {

std::unique_ptr<FrameSource> create_frame_source(const InputSourceConfig& config)
{
    switch (config.type) {
        case InputSourceType::ImageFile:
        case InputSourceType::VideoFile:
            if (config.uri.empty()) {
                throw std::runtime_error("input uri is required");
            }
            return std::make_unique<GStreamerSource>(
                GStreamerSourceConfig {
                    .pipeline = GStreamerSource::video_file_pipeline(config.uri),
                });
        case InputSourceType::Camera:
            if (!config.camera_pipeline.empty()) {
                return std::make_unique<GStreamerSource>(
                    GStreamerSourceConfig {
                        .pipeline = config.camera_pipeline,
                    });
            }
            if (!config.camera_device.empty()) {
                return std::make_unique<GStreamerSource>(
                    GStreamerSourceConfig {
                        .pipeline = GStreamerSource::usb_camera_pipeline(
                            config.camera_device,
                            config.camera_width,
                            config.camera_height),
                    });
            }
            return std::make_unique<LibCameraSource>(
                LibCameraConfig {
                    .width = config.camera_width,
                    .height = config.camera_height,
                    .camera_id = {},
                });
    }

    throw std::runtime_error("unsupported input source type");
}

std::string default_camera_pipeline()
{
    return GStreamerSource::usb_camera_pipeline();
}

} // namespace catcheye::input
