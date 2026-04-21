#include "catcheye/input/libcamera_source.hpp"

#include <chrono>
#include <cstring>

#include <sys/mman.h>

#include <libcamera/formats.h>

namespace catcheye::input {
namespace {

Timestamp now_millis()
{
    return static_cast<Timestamp>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

} // namespace

LibCameraSource::LibCameraSource(LibCameraConfig config)
    : config_(std::move(config)) {}

LibCameraSource::~LibCameraSource()
{
    close();
}

bool LibCameraSource::open()
{
    if (opened_) {
        return true;
    }

    camera_manager_ = std::make_unique<libcamera::CameraManager>();
    if (camera_manager_->start() != 0) {
        return false;
    }

    if (camera_manager_->cameras().empty()) {
        camera_manager_->stop();
        return false;
    }

    camera_ = config_.camera_id.empty()
        ? camera_manager_->cameras().front()
        : camera_manager_->get(config_.camera_id);

    if (!camera_) {
        camera_manager_->stop();
        return false;
    }

    if (camera_->acquire() != 0) {
        camera_manager_->stop();
        return false;
    }

    camera_config_ = camera_->generateConfiguration({libcamera::StreamRole::VideoRecording});
    if (!camera_config_) {
        camera_->release();
        camera_manager_->stop();
        return false;
    }

    libcamera::StreamConfiguration& stream_cfg = camera_config_->at(0);
    stream_cfg.size = {
        static_cast<unsigned int>(config_.width),
        static_cast<unsigned int>(config_.height),
    };
    stream_cfg.pixelFormat = libcamera::formats::NV12;
    stream_cfg.bufferCount = 4;

    if (camera_config_->validate() == libcamera::CameraConfiguration::Invalid) {
        camera_->release();
        camera_manager_->stop();
        return false;
    }

    if (camera_->configure(camera_config_.get()) != 0) {
        camera_->release();
        camera_manager_->stop();
        return false;
    }

    stream_ = stream_cfg.stream();

    allocator_ = std::make_unique<libcamera::FrameBufferAllocator>(camera_);
    if (allocator_->allocate(stream_) < 0) {
        camera_->release();
        camera_manager_->stop();
        return false;
    }

    for (const auto& buffer : allocator_->buffers(stream_)) {
        auto request = camera_->createRequest();
        if (!request || request->addBuffer(stream_, buffer.get()) != 0) {
            camera_->release();
            camera_manager_->stop();
            return false;
        }
        requests_.push_back(std::move(request));
    }

    camera_->requestCompleted.connect(this, &LibCameraSource::on_request_completed);

    if (camera_->start() != 0) {
        camera_->release();
        camera_manager_->stop();
        return false;
    }

    for (auto& req : requests_) {
        camera_->queueRequest(req.get());
    }

    opened_ = true;
    return true;
}

bool LibCameraSource::is_open() const
{
    return opened_;
}

FrameReadStatus LibCameraSource::read(Frame& frame)
{
    if (!opened_) {
        return FrameReadStatus::Error;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return !completed_requests_.empty(); });
    libcamera::Request* request = completed_requests_.front();
    completed_requests_.pop();
    lock.unlock();

    libcamera::FrameBuffer* buffer = request->buffers().at(stream_);

    const libcamera::StreamConfiguration& stream_cfg = camera_config_->at(0);
    const int width = static_cast<int>(stream_cfg.size.width);
    const int height = static_cast<int>(stream_cfg.size.height);
    const int stride = static_cast<int>(stream_cfg.stride);
    const std::size_t total = frame_data_size(PixelFormat::NV12, stride, height);

    frame.width = width;
    frame.height = height;
    frame.stride = stride;
    frame.format = PixelFormat::NV12;
    frame.timestamp = now_millis();
    frame.data.resize(total);

    std::size_t offset = 0;
    for (const libcamera::FrameBuffer::Plane& plane : buffer->planes()) {
        void* mapped = ::mmap(
            nullptr, plane.length, PROT_READ, MAP_SHARED,
            plane.fd.get(), static_cast<off_t>(plane.offset));

        if (mapped == MAP_FAILED) {
            request->reuse(libcamera::Request::ReuseBuffers);
            camera_->queueRequest(request);
            return FrameReadStatus::Error;
        }

        const std::size_t copy_size = std::min(plane.length, total - offset);
        std::memcpy(frame.data.data() + offset, mapped, copy_size);
        ::munmap(mapped, plane.length);
        offset += plane.length;
    }

    request->reuse(libcamera::Request::ReuseBuffers);
    camera_->queueRequest(request);
    return FrameReadStatus::Ok;
}

void LibCameraSource::close()
{
    if (!opened_) {
        return;
    }
    opened_ = false;

    camera_->stop();
    camera_->requestCompleted.disconnect(this, &LibCameraSource::on_request_completed);
    requests_.clear();
    allocator_.reset();
    camera_->release();
    camera_.reset();
    camera_manager_->stop();
    camera_manager_.reset();
    stream_ = nullptr;
}

std::string LibCameraSource::describe() const
{
    if (camera_) {
        return "libcamera:" + camera_->id();
    }
    return "libcamera:" + config_.camera_id;
}

void LibCameraSource::on_request_completed(libcamera::Request* request)
{
    if (request->status() == libcamera::Request::RequestCancelled) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    completed_requests_.push(request);
    cv_.notify_one();
}

} // namespace catcheye::input
