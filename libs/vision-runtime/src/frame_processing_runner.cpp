#include "catcheye/runtime/frame_processing_runner.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

#include <opencv2/highgui.hpp>

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
    const bool needs_preview = config_.render_preview;
    const bool needs_publish = publisher_ != nullptr;
    const std::uint64_t process_interval =
        static_cast<std::uint64_t>(std::max(config_.process_every_n_frames, 1));

    if (!source_ || !processor_) {
        cv::destroyAllWindows();
        return 1;
    }

    if (!source_->open()) {
        cv::destroyAllWindows();
        return 1;
    }

    if (!processor_->initialize()) {
        source_->close();
        cv::destroyAllWindows();
        return 1;
    }

    if (publisher_ && !publisher_->start()) {
        source_->close();
        cv::destroyAllWindows();
        return 1;
    }

    catcheye::input::Frame frame;
    std::uint64_t frame_count = 0;
    int exit_code = 0;

    while (true) {
        const catcheye::input::FrameReadStatus read_status = source_->read(frame);
        if (read_status == catcheye::input::FrameReadStatus::EndOfStream) {
            exit_code = frame_count == 0 ? 1 : 0;
            break;
        }
        if (read_status == catcheye::input::FrameReadStatus::Error) {
            exit_code = 1;
            break;
        }

        ++frame_count;

        const ProcessContext context {
            .frame_index = frame_count,
            .should_process = ((frame_count - 1U) % process_interval) == 0U,
            .needs_preview = needs_preview,
            .needs_publish = needs_publish,
        };
        ProcessOutput output = processor_->process(frame, context);

        if (publisher_ && output.has_message) {
            publisher_->publish(output.message, {.frame_index = frame_count});
        }

        if (config_.render_preview && output.has_preview) {
            cv::imshow(config_.window_name, output.preview_frame);
            const int key = cv::waitKey(1);
            if (key == 27 || key == 'q') {
                exit_code = 0;
                break;
            }
        }
    }

    source_->close();
    if (publisher_) {
        publisher_->stop();
    }
    cv::destroyAllWindows();
    return exit_code;
}

} // namespace catcheye::runtime
