#include "catcheye/transport/rtsp_publisher.hpp"

#include <cstring>
#include <string>

#include <gst/app/gstappsrc.h>

namespace catcheye::transport {
namespace {

void ensure_gst_init()
{
    static bool initialized = false;
    if (!initialized) {
        gst_init(nullptr, nullptr);
        initialized = true;
    }
}

} // namespace

RtspPublisher::RtspPublisher(RtspPublisherConfig config)
    : config_(std::move(config))
{
    ensure_gst_init();
}

RtspPublisher::~RtspPublisher()
{
    stop();
}

bool RtspPublisher::start()
{
    if (running_) {
        return true;
    }

    server_ = gst_rtsp_server_new();
    gst_rtsp_server_set_address(server_, config_.bind_address.c_str());
    gst_rtsp_server_set_service(server_, std::to_string(config_.port).c_str());

    // Pipeline: appsrc (raw NV12) → hardware H.264 encoder → RTP packetizer
    const std::string pipeline =
        "( appsrc name=src format=time is-live=true do-timestamp=true"
        " caps=video/x-raw,format=NV12"
        ",width=" + std::to_string(config_.width)
        + ",height=" + std::to_string(config_.height)
        + ",framerate=" + std::to_string(config_.framerate) + "/1"
        + " ! v4l2h264enc extra-controls=\"controls,repeat_sequence_header=1\""
        + " ! video/x-h264,level=(string)4"
        + " ! rtph264pay name=pay0 pt=96 )";

    GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_launch(factory, pipeline.c_str());
    gst_rtsp_media_factory_set_shared(factory, TRUE);

    g_signal_connect(factory, "media-configure",
                     G_CALLBACK(RtspPublisher::on_media_configure), this);

    GstRTSPMountPoints* mounts = gst_rtsp_server_get_mount_points(server_);
    gst_rtsp_mount_points_add_factory(mounts, config_.mount_point.c_str(), factory);
    g_object_unref(mounts);

    if (gst_rtsp_server_attach(server_, nullptr) == 0) {
        g_object_unref(server_);
        server_ = nullptr;
        return false;
    }

    loop_ = g_main_loop_new(nullptr, FALSE);
    running_ = true;
    server_thread_ = std::thread(&RtspPublisher::server_loop, this);
    return true;
}

void RtspPublisher::stop()
{
    if (!running_) {
        return;
    }
    running_ = false;

    if (loop_) {
        g_main_loop_quit(loop_);
    }
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    if (loop_) {
        g_main_loop_unref(loop_);
        loop_ = nullptr;
    }
    if (appsrc_) {
        gst_object_unref(appsrc_);
        appsrc_ = nullptr;
    }
    if (server_) {
        g_object_unref(server_);
        server_ = nullptr;
    }
}

void RtspPublisher::publish(
    const catcheye::input::Frame& frame,
    const catcheye::protocol::FrameMessage& /*message*/,
    const PublishContext& context)
{
    if (!running_ || !appsrc_ || frame.empty()) {
        return;
    }

    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, frame.data.size(), nullptr);
    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        gst_buffer_unref(buffer);
        return;
    }

    std::memcpy(map.data, frame.data.data(), frame.data.size());
    gst_buffer_unmap(buffer, &map);

    const guint64 fr = static_cast<guint64>(config_.framerate);
    GST_BUFFER_PTS(buffer) = context.frame_index * GST_SECOND / fr;
    GST_BUFFER_DURATION(buffer) = GST_SECOND / fr;

    gst_app_src_push_buffer(GST_APP_SRC(appsrc_), buffer);
}

void RtspPublisher::on_media_configure(
    GstRTSPMediaFactory* /*factory*/,
    GstRTSPMedia* media,
    gpointer user_data)
{
    auto* self = static_cast<RtspPublisher*>(user_data);
    GstElement* element = gst_rtsp_media_get_element(media);
    self->appsrc_ = gst_bin_get_by_name_recurse_up(GST_BIN(element), "src");
    gst_object_unref(element);
}

void RtspPublisher::server_loop()
{
    g_main_loop_run(loop_);
}

} // namespace catcheye::transport
