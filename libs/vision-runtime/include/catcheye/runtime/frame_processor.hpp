#pragma once

#include <cstdint>

#include "catcheye/input/frame.hpp"
#include "catcheye/protocol/frame_message.hpp"

namespace catcheye::runtime {

struct ProcessContext {
    std::uint64_t frame_index = 0;
    bool should_process = true;
    bool needs_publish = false;
};

struct ProcessOutput {
    bool has_message = false;
    catcheye::protocol::FrameMessage message;
    bool has_publish_frame = false;
    catcheye::input::Frame publish_frame;
};

class FrameProcessor {
   public:
    virtual ~FrameProcessor() = default;

    virtual bool initialize() = 0;
    virtual ProcessOutput process(
        const catcheye::input::Frame& frame,
        const ProcessContext& context) = 0;
};

} // namespace catcheye::runtime
