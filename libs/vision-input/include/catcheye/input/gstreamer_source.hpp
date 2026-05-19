#pragma once

#include <string>
#include <mutex>
#include <optional>
#include <string_view>

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
    std::optional<std::string> property_json(std::string_view key) const override;
    bool set_bool_property(std::string_view key, bool value) override;
    bool set_int_property(std::string_view key, int value) override;
    bool set_float_property(std::string_view key, float value) override;
    bool set_string_property(std::string_view key, std::string_view value) override;

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
    mutable std::mutex property_mutex_;
    bool opened_ = false;
};

} // namespace catcheye::input
