#pragma once

#include <cstdint>

#include <opencv2/core/mat.hpp>

#include "catcheye/input/frame.hpp"

namespace catcheye::runtime {

struct ProcessContext {
    std::uint64_t frame_index = 0;
    bool should_process = true;
    bool needs_visualization = false;
};

struct ProcessOutput {
    bool has_visualization = false;
    cv::Mat visualization;
};

class FrameProcessor {
   public:
    virtual ~FrameProcessor() = default;

    virtual bool initialize() = 0;
    virtual ProcessOutput process(const catcheye::input::Frame& frame, const ProcessContext& context) = 0;
};

} // namespace catcheye::runtime
