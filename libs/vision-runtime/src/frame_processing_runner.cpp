#include "catcheye/runtime/frame_processing_runner.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <utility>

namespace catcheye::runtime {

FrameProcessingRunner::FrameProcessingRunner(
    RuntimeConfig config,
    std::unique_ptr<catcheye::input::FrameSource> source,
    std::unique_ptr<FrameProcessor> processor,
    std::unique_ptr<catcheye::transport::ResultPublisher> publisher)
    : config_(std::move(config)),
      source_(std::move(source)),
      processor_(std::move(processor)),
      publisher_(std::move(publisher)) {}

int FrameProcessingRunner::run()
{
    if (!source_ || !processor_) {
        std::cerr << "runner bootstrap failed: source or processor is null\n";
        return 1;
    }

    if (!source_->open()) {
        std::cerr << "failed to open frame source '" << source_->describe() << "'\n";
        return 1;
    }

    if (!processor_->initialize()) {
        std::cerr << "failed to initialize frame processor\n";
        source_->close();
        return 1;
    }

    if (publisher_ && !publisher_->start()) {
        std::cerr << "failed to start result publisher\n";
        source_->close();
        return 1;
    }

    const bool needs_publish = publisher_ != nullptr;
    const std::uint64_t process_interval =
        static_cast<std::uint64_t>(std::max(config_.process_every_n_frames, 1));

    catcheye::input::Frame frame;
    std::uint64_t frame_count = 0;
    int exit_code = 0;

    while (true) {
        const auto read_status = source_->read(frame);
        if (read_status == catcheye::input::FrameReadStatus::EndOfStream) {
            std::cerr << "frame source '" << source_->describe() << "' reached end of stream after "
                      << frame_count << " frame(s)\n";
            exit_code = frame_count == 0 ? 1 : 0;
            break;
        }
        if (read_status == catcheye::input::FrameReadStatus::Error) {
            std::cerr << "frame source '" << source_->describe() << "' returned read error at frame "
                      << (frame_count + 1U) << '\n';
            exit_code = 1;
            break;
        }

        ++frame_count;

        const ProcessContext context {
            .frame_index = frame_count,
            .should_process = ((frame_count - 1U) % process_interval) == 0U,
            .needs_publish = needs_publish,
        };
        ProcessOutput output = processor_->process(frame, context);

        if (publisher_ && output.has_message) {
            publisher_->publish(frame, output.message, {.frame_index = frame_count});
        }
    }

    source_->close();
    if (publisher_) {
        publisher_->stop();
    }
    std::cerr << "runner finished with exit code " << exit_code << '\n';
    return exit_code;
}

} // namespace catcheye::runtime
