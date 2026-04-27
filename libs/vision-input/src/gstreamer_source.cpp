#include "catcheye/input/gstreamer_source.hpp"

#include <chrono>
#include <iostream>
#include <string>

#include <gst/video/video.h>

namespace catcheye::input {
namespace {

Timestamp now_millis()
{
    return static_cast<Timestamp>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

PixelFormat gst_format_to_pixel_format(GstVideoFormat fmt)
{
    switch (fmt) {
        case GST_VIDEO_FORMAT_NV12: return PixelFormat::NV12;
        case GST_VIDEO_FORMAT_RGB:  return PixelFormat::RGB;
        case GST_VIDEO_FORMAT_BGR:  return PixelFormat::BGR;
        case GST_VIDEO_FORMAT_RGBA: return PixelFormat::RGBA;
        case GST_VIDEO_FORMAT_BGRA: return PixelFormat::BGRA;
        case GST_VIDEO_FORMAT_GRAY8: return PixelFormat::GRAY8;
        default:                    return PixelFormat::UNKNOWN;
    }
}

void ensure_gst_init()
{
    static bool initialized = false;
    if (!initialized) {
        gst_init(nullptr, nullptr);
        initialized = true;
    }
}

void log_gst_message(GstMessage* message, const std::string& context)
{
    GError* error = nullptr;
    gchar* debug_info = nullptr;

    switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR:
            gst_message_parse_error(message, &error, &debug_info);
            std::cerr << "gstreamer error while " << context << ": "
                      << (error ? error->message : "unknown error") << '\n';
            break;
        case GST_MESSAGE_WARNING:
            gst_message_parse_warning(message, &error, &debug_info);
            std::cerr << "gstreamer warning while " << context << ": "
                      << (error ? error->message : "unknown warning") << '\n';
            break;
        default:
            break;
    }

    if (debug_info) {
        std::cerr << "gstreamer debug: " << debug_info << '\n';
    }
    if (error) {
        g_error_free(error);
    }
    if (debug_info) {
        g_free(debug_info);
    }
}

void log_gst_bus_messages(GstElement* pipeline, const std::string& context)
{
    GstBus* bus = gst_element_get_bus(pipeline);
    if (!bus) {
        return;
    }

    while (GstMessage* message = gst_bus_pop_filtered(
               bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_WARNING))) {
        log_gst_message(message, context);
        gst_message_unref(message);
    }

    gst_object_unref(bus);
}

} // namespace

GStreamerSource::GStreamerSource(GStreamerSourceConfig config)
    : config_(std::move(config))
{
    ensure_gst_init();
}

GStreamerSource::~GStreamerSource()
{
    close();
}

bool GStreamerSource::open()
{
    if (opened_) {
        return true;
    }

    const std::string full_pipeline =
        config_.pipeline + " ! appsink name=sink sync=false max-buffers=1 drop=true";

    GError* error = nullptr;
    pipeline_ = gst_parse_launch(full_pipeline.c_str(), &error);
    if (error) {
        std::cerr << "failed to parse gstreamer pipeline: " << error->message << '\n';
        std::cerr << "pipeline: " << full_pipeline << '\n';
        g_error_free(error);
    }
    if (!pipeline_) {
        return false;
    }

    appsink_ = gst_bin_get_by_name(GST_BIN(pipeline_), "sink");
    if (!appsink_) {
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
        return false;
    }

    gst_app_sink_set_emit_signals(GST_APP_SINK(appsink_), FALSE);

    const GstStateChangeReturn ret = gst_element_set_state(pipeline_, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        log_gst_bus_messages(pipeline_, "starting pipeline");
        std::cerr << "failed to start gstreamer pipeline: " << full_pipeline << '\n';
        gst_object_unref(appsink_);
        gst_object_unref(pipeline_);
        appsink_ = nullptr;
        pipeline_ = nullptr;
        return false;
    }

    opened_ = true;
    return true;
}

bool GStreamerSource::is_open() const
{
    return opened_;
}

FrameReadStatus GStreamerSource::read(Frame& frame)
{
    if (!opened_) {
        return FrameReadStatus::Error;
    }

    GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(appsink_));
    if (!sample) {
        return FrameReadStatus::EndOfStream;
    }

    GstCaps* caps = gst_sample_get_caps(sample);
    GstVideoInfo info;
    if (!gst_video_info_from_caps(&info, caps)) {
        gst_sample_unref(sample);
        return FrameReadStatus::Error;
    }

    const PixelFormat fmt = gst_format_to_pixel_format(GST_VIDEO_INFO_FORMAT(&info));
    if (fmt == PixelFormat::UNKNOWN) {
        gst_sample_unref(sample);
        return FrameReadStatus::Error;
    }

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        gst_sample_unref(sample);
        return FrameReadStatus::Error;
    }

    frame.width = GST_VIDEO_INFO_WIDTH(&info);
    frame.height = GST_VIDEO_INFO_HEIGHT(&info);
    frame.stride = GST_VIDEO_INFO_PLANE_STRIDE(&info, 0);
    frame.format = fmt;
    frame.timestamp = now_millis();
    frame.data.assign(map.data, map.data + map.size);

    gst_buffer_unmap(buffer, &map);
    gst_sample_unref(sample);
    return FrameReadStatus::Ok;
}

void GStreamerSource::close()
{
    if (!opened_) {
        return;
    }
    opened_ = false;

    if (pipeline_) {
        gst_element_set_state(pipeline_, GST_STATE_NULL);
    }
    if (appsink_) {
        gst_object_unref(appsink_);
        appsink_ = nullptr;
    }
    if (pipeline_) {
        gst_object_unref(pipeline_);
        pipeline_ = nullptr;
    }
}

std::string GStreamerSource::describe() const
{
    return "gstreamer:" + config_.pipeline;
}

std::string GStreamerSource::usb_camera_pipeline(
    const std::string& device, int width, int height)
{
    return "v4l2src device=" + device
        + " ! video/x-raw,width=" + std::to_string(width)
        + ",height=" + std::to_string(height)
        + " ! videoconvert ! video/x-raw,format=NV12";
}

std::string GStreamerSource::video_file_pipeline(const std::string& path)
{
    return "filesrc location=" + path
        + " ! decodebin ! videoconvert ! video/x-raw,format=NV12";
}

std::string GStreamerSource::test_pattern_pipeline(int width, int height)
{
    return "videotestsrc"
        " ! video/x-raw,width=" + std::to_string(width)
        + ",height=" + std::to_string(height)
        + " ! videoconvert ! video/x-raw,format=NV12";
}

} // namespace catcheye::input
