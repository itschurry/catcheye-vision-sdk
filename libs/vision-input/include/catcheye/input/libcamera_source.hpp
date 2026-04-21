#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include <libcamera/libcamera.h>

#include "catcheye/input/frame_source.hpp"

namespace catcheye::input {

struct LibCameraConfig {
    int width = 1280;
    int height = 720;
    int framerate = 30;
    std::string camera_id;  // empty = first available camera
};

class LibCameraSource final : public FrameSource {
   public:
    explicit LibCameraSource(LibCameraConfig config = {});
    ~LibCameraSource() override;

    LibCameraSource(const LibCameraSource&) = delete;
    LibCameraSource& operator=(const LibCameraSource&) = delete;

    bool open() override;
    bool is_open() const override;
    FrameReadStatus read(Frame& frame) override;
    void close() override;
    std::string describe() const override;

   private:
    void on_request_completed(libcamera::Request* request);

    LibCameraConfig config_;
    bool opened_ = false;

    std::unique_ptr<libcamera::CameraManager> camera_manager_;
    std::shared_ptr<libcamera::Camera> camera_;
    std::unique_ptr<libcamera::CameraConfiguration> camera_config_;
    std::unique_ptr<libcamera::FrameBufferAllocator> allocator_;
    std::vector<std::unique_ptr<libcamera::Request>> requests_;
    libcamera::Stream* stream_ = nullptr;

    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<libcamera::Request*> completed_requests_;
};

} // namespace catcheye::input
