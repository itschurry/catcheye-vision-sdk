#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include <glib.h>
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include "catcheye/transport/result_publisher.hpp"

namespace catcheye::transport {

struct RtspPublisherConfig {
    std::string bind_address = "0.0.0.0";
    int port = 8554;
    std::string mount_point = "/stream";
    int width = 1280;
    int height = 720;
    int framerate = 30;
};

class RtspPublisher final : public ResultPublisher {
   public:
    explicit RtspPublisher(RtspPublisherConfig config = {});
    ~RtspPublisher() override;

    RtspPublisher(const RtspPublisher&) = delete;
    RtspPublisher& operator=(const RtspPublisher&) = delete;

    bool start() override;
    void stop() override;
    void publish(
        const catcheye::input::Frame& frame,
        const catcheye::protocol::FrameMessage& message,
        const PublishContext& context) override;

   private:
    static void on_media_configure(
        GstRTSPMediaFactory* factory,
        GstRTSPMedia* media,
        gpointer user_data);

    void server_loop();

    RtspPublisherConfig config_;
    GstRTSPServer* server_ = nullptr;
    GstElement* appsrc_ = nullptr;
    std::mutex appsrc_mutex_;
    GMainLoop* loop_ = nullptr;
    std::thread server_thread_;
    std::atomic<bool> running_ {false};
};

} // namespace catcheye::transport
