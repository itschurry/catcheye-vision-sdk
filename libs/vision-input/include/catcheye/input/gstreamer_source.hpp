#pragma once

#include <string>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include "catcheye/input/frame_source.hpp"

namespace catcheye::input {

struct GStreamerSourceConfig {
    // Full GStreamer pipeline string ending before appsink.
    // appsink is appended automatically.
    std::string pipeline;
};

class GStreamerSource final : public FrameSource {
   public:
    explicit GStreamerSource(GStreamerSourceConfig config);
    ~GStreamerSource() override;

    GStreamerSource(const GStreamerSource&) = delete;
    GStreamerSource& operator=(const GStreamerSource&) = delete;

    bool open() override;
    bool is_open() const override;
    FrameReadStatus read(Frame& frame) override;
    void close() override;
    std::string describe() const override;

    // Pipeline string helpers — callers pass the result to GStreamerSourceConfig::pipeline
    static std::string usb_camera_pipeline(
        const std::string& device = "/dev/video0",
        int width = 640,
        int height = 480);

    static std::string video_file_pipeline(const std::string& path);

    static std::string test_pattern_pipeline(int width = 640, int height = 480);

   private:
    GStreamerSourceConfig config_;
    GstElement* pipeline_ = nullptr;
    GstElement* appsink_ = nullptr;
    bool opened_ = false;
};

} // namespace catcheye::input
