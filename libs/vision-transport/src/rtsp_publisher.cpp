#include "catcheye/transport/rtsp_publisher.hpp"

#include <cstring>
#include <iostream>
#include <string>

#include <gst/app/gstappsrc.h>
#include <gst/video/video.h>

namespace catcheye::transport {
namespace {

void ensure_gst_init() {
    static bool initialized = false;
    if (!initialized) {
        gst_init(nullptr, nullptr);
        initialized = true;
    }
}

bool is_valid_bgr_frame(const catcheye::input::Frame& frame, const RtspPublisherConfig& config) {
    if (frame.width != config.width || frame.height != config.height) {
        return false;
    }
    if (frame.format != catcheye::input::PixelFormat::BGR) {
        return false;
    }
    if (frame.stride < (frame.width * 3) || frame.stride <= 0) {
        return false;
    }

    const std::size_t expected_size = catcheye::input::frame_data_size(frame.format, frame.stride, frame.height);
    return frame.data.size() == expected_size;
}

void log_invalid_frame_reason(const catcheye::input::Frame& frame, const RtspPublisherConfig& config) {
    std::cerr << "RTSP publisher: dropping invalid frame" << " width=" << frame.width << " height=" << frame.height
              << " stride=" << frame.stride << " format=" << static_cast<int>(frame.format) << " bytes=" << frame.data.size()
              << " expected_width=" << config.width << " expected_height=" << config.height << " expected_framerate=" << config.framerate
              << '\n';
}

std::string select_h264_encoder_pipeline() {
    if (gst_element_factory_find("x264enc") != nullptr) {
        return "videoconvert"
               " ! x264enc tune=zerolatency speed-preset=ultrafast bitrate=2000 key-int-max=30"
               " ! video/x-h264,profile=baseline";
    }

    return {};
}

} // namespace

RtspPublisher::RtspPublisher(RtspPublisherConfig config) : config_(std::move(config)) {
    ensure_gst_init();
}

RtspPublisher::~RtspPublisher() {
    stop();
}

bool RtspPublisher::configure_from_frame(const catcheye::input::Frame& frame) {
    if (running_) {
        return true;
    }
    if (frame.empty() || frame.width <= 0 || frame.height <= 0) {
        return false;
    }
    if ((frame.width % 2) != 0 || (frame.height % 2) != 0) {
        return false;
    }

    if (config_.width != frame.width || config_.height != frame.height) {
        std::cerr << "RTSP publisher: configuring stream to match first frame " << frame.width << "x" << frame.height << " (was "
                  << config_.width << "x" << config_.height << ")\n";
        config_.width = frame.width;
        config_.height = frame.height;
    }
    return true;
}

bool RtspPublisher::start() {
    if (running_) {
        return true;
    }
    if (config_.framerate <= 0 || config_.width <= 0 || config_.height <= 0) {
        return false;
    }
    if ((config_.width % 2) != 0 || (config_.height % 2) != 0) {
        return false;
    }

    const std::string encoder_pipeline = select_h264_encoder_pipeline();
    if (encoder_pipeline.empty()) {
        std::cerr << "RTSP publisher: required H.264 encoder plugin 'x264enc' not found\n";
        return false;
    }

    server_ = gst_rtsp_server_new();
    gst_rtsp_server_set_address(server_, config_.bind_address.c_str());
    gst_rtsp_server_set_service(server_, std::to_string(config_.port).c_str());

    std::cerr << "RTSP publisher: using encoder pipeline '" << encoder_pipeline << "'\n";

    // Pipeline: appsrc (raw BGR) -> leaky queue -> H.264 encoder -> RTP packetizer.
    // Keep only the newest frame so slow clients don't accumulate stale video.
    const std::string pipeline = "( appsrc name=src format=time is-live=true do-timestamp=true"
                                 " block=false max-buffers=1 leaky-type=downstream"
                                 " caps=video/x-raw,format=BGR"
                                 ",width=" +
                                 std::to_string(config_.width) + ",height=" + std::to_string(config_.height) +
                                 ",framerate=" + std::to_string(config_.framerate) + "/1" +
                                 " ! queue leaky=downstream max-size-buffers=1 max-size-bytes=0 max-size-time=0" + " ! " +
                                 encoder_pipeline + " ! rtph264pay name=pay0 pt=96 )";

    GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_launch(factory, pipeline.c_str());
    gst_rtsp_media_factory_set_shared(factory, TRUE);
    // gst_rtsp_media_factory_set_protocols(factory, GST_RTSP_LOWER_TRANS_UDP);

    g_signal_connect(factory, "media-configure", G_CALLBACK(RtspPublisher::on_media_configure), this);

    GstRTSPMountPoints* mounts = gst_rtsp_server_get_mount_points(server_);
    gst_rtsp_mount_points_add_factory(mounts, config_.mount_point.c_str(), factory);
    g_object_unref(mounts);

    if (gst_rtsp_server_attach(server_, nullptr) == 0) {
        g_object_unref(server_);
        server_ = nullptr;
        return false;
    }

    std::cerr << "RTSP publisher: listening on rtsp://" << config_.bind_address << ":" << config_.port << config_.mount_point << '\n';

    loop_ = g_main_loop_new(nullptr, FALSE);
    pushed_frames_ = 0;
    dropped_frames_no_client_ = 0;
    running_ = true;
    server_thread_ = std::thread(&RtspPublisher::server_loop, this);
    return true;
}

void RtspPublisher::stop() {
    const bool was_running = running_.exchange(false);

    if (was_running && loop_) {
        g_main_loop_quit(loop_);
    }
    if (was_running && server_thread_.joinable()) {
        server_thread_.join();
    }
    if (loop_) {
        g_main_loop_unref(loop_);
        loop_ = nullptr;
    }
    {
        std::lock_guard<std::mutex> lock(appsrc_mutex_);
        if (appsrc_) {
            gst_object_unref(appsrc_);
            appsrc_ = nullptr;
        }
    }
    if (server_) {
        g_object_unref(server_);
        server_ = nullptr;
    }
}

void RtspPublisher::publish(const catcheye::input::Frame& frame, const catcheye::protocol::FrameMessage& /*message*/,
                            const PublishContext& context) {
    if (!running_ || frame.empty()) {
        return;
    }

    if (!is_valid_bgr_frame(frame, config_)) {
        if (!warned_invalid_frame_.exchange(true)) {
            log_invalid_frame_reason(frame, config_);
        }
        return;
    }

    GstElement* appsrc = nullptr;
    {
        std::lock_guard<std::mutex> lock(appsrc_mutex_);
        if (!appsrc_) {
            ++dropped_frames_no_client_;
            if (!warned_missing_appsrc_.exchange(true)) {
                std::cerr << "RTSP publisher: appsrc not ready yet, dropping frames until a client connects\n";
            }
            return;
        }
        appsrc = GST_ELEMENT(gst_object_ref(appsrc_));
    }

    warned_missing_appsrc_ = false;

    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, frame.data.size(), nullptr);
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        std::cerr << "RTSP publisher: failed to map GstBuffer for frame " << context.frame_index << '\n';
        gst_buffer_unref(buffer);
        gst_object_unref(appsrc);
        return;
    }

    std::memcpy(map.data, frame.data.data(), frame.data.size());
    gst_buffer_unmap(buffer, &map);

    // Let appsrc timestamp buffers with the live pipeline clock. Deriving PTS from
    // frame_index assumes the producer runs at exactly config_.framerate and can
    // accumulate latency when the real capture rate differs.

    const GstFlowReturn push_result = gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer);
    if (push_result != GST_FLOW_OK) {
        std::cerr << "RTSP publisher: gst_app_src_push_buffer failed with code " << static_cast<int>(push_result) << " at frame "
                  << context.frame_index << '\n';
    } else {
        ++pushed_frames_;
        if (pushed_frames_ == 1) {
            std::cerr << "RTSP publisher: first frame pushed at runner frame " << context.frame_index << '\n';
        } else if ((pushed_frames_ % 150U) == 0U) {
            std::cerr << "RTSP publisher: pushed_frames=" << pushed_frames_ << " last_runner_frame=" << context.frame_index
                      << " dropped_before_client=" << dropped_frames_no_client_ << '\n';
        }
    }
    gst_object_unref(appsrc);
}

void RtspPublisher::on_media_configure(GstRTSPMediaFactory* /*factory*/, GstRTSPMedia* media, gpointer user_data) {
    auto* self = static_cast<RtspPublisher*>(user_data);
    GstElement* element = gst_rtsp_media_get_element(media);
    GstElement* new_appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(element), "src");
    std::cerr << "RTSP publisher: media configured for client, appsrc=" << (new_appsrc != nullptr ? "ready" : "missing") << '\n';
    {
        std::lock_guard<std::mutex> lock(self->appsrc_mutex_);
        if (self->appsrc_) {
            gst_object_unref(self->appsrc_);
        }
        self->appsrc_ = new_appsrc;
    }
    self->warned_missing_appsrc_ = false;
    gst_object_unref(element);
}

void RtspPublisher::server_loop() {
    g_main_loop_run(loop_);
}

} // namespace catcheye::transport
