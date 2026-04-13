#pragma once

#include <memory>
#include <string>

#include "catcheye/input/frame_source.hpp"
#include "catcheye/runtime/frame_processor.hpp"
#include "catcheye/runtime/preview_sink.hpp"

namespace catcheye::runtime {

struct RuntimeConfig {
    std::string window_name = "Vision Runtime";
    bool render_preview = true;
    bool stream_preview = false;
    int process_every_n_frames = 1;
};

class FrameProcessingRunner {
   public:
    FrameProcessingRunner(
        RuntimeConfig config,
        std::unique_ptr<catcheye::input::FrameSource> source,
        std::unique_ptr<FrameProcessor> processor,
        std::unique_ptr<PreviewSink> preview_sink = nullptr);

    int run();

   private:
    RuntimeConfig config_;
    std::unique_ptr<catcheye::input::FrameSource> source_;
    std::unique_ptr<FrameProcessor> processor_;
    std::unique_ptr<PreviewSink> preview_sink_;
};

} // namespace catcheye::runtime
