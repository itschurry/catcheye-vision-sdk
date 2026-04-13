#include "test_support.hpp"

#include <memory>
#include <utility>
#include <vector>

#include <opencv2/core.hpp>

#include "catcheye/input/frame_source.hpp"
#include "catcheye/runtime/frame_processing_runner.hpp"

namespace {

class FakeFrameSource final : public catcheye::input::FrameSource {
   public:
    explicit FakeFrameSource(std::vector<catcheye::input::FrameReadStatus> statuses)
        : statuses_(std::move(statuses)) {}

    bool open() override
    {
        opened_ = true;
        return true;
    }

    bool is_open() const override
    {
        return opened_;
    }

    catcheye::input::FrameReadStatus read(catcheye::input::Frame& frame) override
    {
        if (!opened_ || index_ >= statuses_.size()) {
            return catcheye::input::FrameReadStatus::EndOfStream;
        }

        const auto status = statuses_[index_++];
        if (status == catcheye::input::FrameReadStatus::Ok) {
            frame.image = cv::Mat(16, 16, CV_8UC3, cv::Scalar(1, 2, 3)).clone();
            frame.format = catcheye::input::PixelFormat::BGR;
            frame.timestamp = 42;
        }
        return status;
    }

    void close() override
    {
        opened_ = false;
    }

    std::string describe() const override
    {
        return "fake";
    }

   private:
    std::vector<catcheye::input::FrameReadStatus> statuses_;
    std::size_t index_ = 0;
    bool opened_ = false;
};

class FakeProcessor final : public catcheye::runtime::FrameProcessor {
   public:
    bool initialize() override
    {
        initialized = true;
        return initialize_result;
    }

    catcheye::runtime::ProcessOutput process(
        const catcheye::input::Frame& frame,
        const catcheye::runtime::ProcessContext& context) override
    {
        ++process_calls;
        should_process_flags.push_back(context.should_process);

        catcheye::runtime::ProcessOutput output;
        if (context.needs_visualization && !frame.empty()) {
            output.has_visualization = true;
            output.visualization = frame.image.clone();
        }
        return output;
    }

    bool initialize_result = true;
    bool initialized = false;
    int process_calls = 0;
    std::vector<bool> should_process_flags;
};

class FakePreviewSink final : public catcheye::runtime::PreviewSink {
   public:
    bool start() override
    {
        started = true;
        return start_result;
    }

    void stop() override
    {
        stopped = true;
    }

    void publish(const cv::Mat&) override
    {
        ++publish_calls;
    }

    bool start_result = true;
    bool started = false;
    bool stopped = false;
    int publish_calls = 0;
};

} // namespace

TEST_CASE(frame_processing_runner_returns_zero_after_processing_before_eos)
{
    auto source = std::make_unique<FakeFrameSource>(
        std::vector<catcheye::input::FrameReadStatus> {
            catcheye::input::FrameReadStatus::Ok,
            catcheye::input::FrameReadStatus::EndOfStream,
        });
    auto* processor_ptr = new FakeProcessor();
    std::unique_ptr<catcheye::runtime::FrameProcessor> processor(processor_ptr);

    catcheye::runtime::FrameProcessingRunner runner(
        {.render_preview = false, .stream_preview = false},
        std::move(source),
        std::move(processor));

    test_support::assert_true(runner.run() == 0, "runner should succeed after one processed frame");
    test_support::assert_true(processor_ptr->initialized, "processor should initialize");
    test_support::assert_true(processor_ptr->process_calls == 1, "processor should run once");
}

TEST_CASE(frame_processing_runner_returns_one_on_read_error)
{
    auto source = std::make_unique<FakeFrameSource>(
        std::vector<catcheye::input::FrameReadStatus> {
            catcheye::input::FrameReadStatus::Error,
        });
    std::unique_ptr<catcheye::runtime::FrameProcessor> processor = std::make_unique<FakeProcessor>();

    catcheye::runtime::FrameProcessingRunner runner(
        {.render_preview = false, .stream_preview = false},
        std::move(source),
        std::move(processor));

    test_support::assert_true(runner.run() == 1, "runner should fail on read error");
}

TEST_CASE(frame_processing_runner_returns_one_when_source_ends_before_first_frame)
{
    auto source = std::make_unique<FakeFrameSource>(
        std::vector<catcheye::input::FrameReadStatus> {
            catcheye::input::FrameReadStatus::EndOfStream,
        });
    std::unique_ptr<catcheye::runtime::FrameProcessor> processor = std::make_unique<FakeProcessor>();

    catcheye::runtime::FrameProcessingRunner runner(
        {.render_preview = false, .stream_preview = false},
        std::move(source),
        std::move(processor));

    test_support::assert_true(runner.run() == 1, "runner should fail on empty input");
}

TEST_CASE(frame_processing_runner_applies_process_cadence)
{
    auto source = std::make_unique<FakeFrameSource>(
        std::vector<catcheye::input::FrameReadStatus> {
            catcheye::input::FrameReadStatus::Ok,
            catcheye::input::FrameReadStatus::Ok,
            catcheye::input::FrameReadStatus::Ok,
            catcheye::input::FrameReadStatus::EndOfStream,
        });
    auto* processor_ptr = new FakeProcessor();
    std::unique_ptr<catcheye::runtime::FrameProcessor> processor(processor_ptr);

    catcheye::runtime::FrameProcessingRunner runner(
        {.render_preview = false, .stream_preview = false, .process_every_n_frames = 2},
        std::move(source),
        std::move(processor));

    test_support::assert_true(runner.run() == 0, "runner should succeed");
    test_support::assert_true(processor_ptr->should_process_flags.size() == 3, "processor should see three frames");
    test_support::assert_true(processor_ptr->should_process_flags[0], "frame 1 should process");
    test_support::assert_true(!processor_ptr->should_process_flags[1], "frame 2 should skip");
    test_support::assert_true(processor_ptr->should_process_flags[2], "frame 3 should process");
}

TEST_CASE(frame_processing_runner_publishes_preview_frames_to_sink)
{
    auto source = std::make_unique<FakeFrameSource>(
        std::vector<catcheye::input::FrameReadStatus> {
            catcheye::input::FrameReadStatus::Ok,
            catcheye::input::FrameReadStatus::Ok,
            catcheye::input::FrameReadStatus::EndOfStream,
        });
    std::unique_ptr<catcheye::runtime::FrameProcessor> processor = std::make_unique<FakeProcessor>();
    auto* sink_ptr = new FakePreviewSink();
    std::unique_ptr<catcheye::runtime::PreviewSink> sink(sink_ptr);

    catcheye::runtime::FrameProcessingRunner runner(
        {.render_preview = false, .stream_preview = true},
        std::move(source),
        std::move(processor),
        std::move(sink));

    test_support::assert_true(runner.run() == 0, "runner should succeed with preview sink");
    test_support::assert_true(sink_ptr->started, "sink should start");
    test_support::assert_true(sink_ptr->stopped, "sink should stop");
    test_support::assert_true(sink_ptr->publish_calls == 2, "sink should receive processed preview frames");
}
