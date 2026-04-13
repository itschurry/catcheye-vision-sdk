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
    std::unique_ptr<PreviewSink> preview_sink)
    : config_(std::move(config)),
      source_(std::move(source)),
      processor_(std::move(processor)),
      preview_sink_(std::move(preview_sink)) {}

int FrameProcessingRunner::run()
{
    const bool needs_visualization = config_.render_preview || config_.stream_preview;
    const std::uint64_t process_interval =
        static_cast<std::uint64_t>(std::max(config_.process_every_n_frames, 1));

    if (!source_ || !processor_) {
        cv::destroyAllWindows();
        return 1;
    }

    if (config_.stream_preview && !preview_sink_) {
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

    if (config_.stream_preview && preview_sink_ && !preview_sink_->start()) {
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
            .needs_visualization = needs_visualization,
        };
        ProcessOutput output = processor_->process(frame, context);

        if (!context.needs_visualization || !output.has_visualization) {
            continue;
        }

        if (config_.stream_preview && preview_sink_) {
            preview_sink_->publish(output.visualization);
        }

        if (config_.render_preview) {
            cv::imshow(config_.window_name, output.visualization);
            const int key = cv::waitKey(1);
            if (key == 27 || key == 'q') {
                exit_code = 0;
                break;
            }
        }
    }

    source_->close();
    if (preview_sink_) {
        preview_sink_->stop();
    }
    cv::destroyAllWindows();
    return exit_code;
}

} // namespace catcheye::runtime
