#pragma once

#include <cstdint>

#include "catcheye/input/frame.hpp"
#include "catcheye/protocol/frame_message.hpp"

namespace catcheye::transport {

struct PublishContext {
    std::uint64_t frame_index = 0;
};

class ResultPublisher {
   public:
    virtual ~ResultPublisher() = default;

    virtual bool configure_from_frame(const catcheye::input::Frame& /*frame*/) { return true; }
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual void publish(
        const catcheye::input::Frame& frame,
        const catcheye::protocol::FrameMessage& message,
        const PublishContext& context) = 0;
};

} // namespace catcheye::transport
