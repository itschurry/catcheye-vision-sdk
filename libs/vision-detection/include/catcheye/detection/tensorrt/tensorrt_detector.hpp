#pragma once

#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "catcheye/detection/detector.hpp"
#include "catcheye/detection/postprocess/yolo_decoder.hpp"

namespace catcheye::detection {

struct TensorRtDetectorConfig {
    std::string engine_path;
    std::string metadata_path;
    int input_width = 0;
    int input_height = 0;
    int num_classes = 0;
    int device_id = 0;
    float confidence_threshold = 0.25F;
    float nms_threshold = 0.45F;
    bool requires_nms = true;
    YoloOutputFormat output_format = YoloOutputFormat::RawClassScores;
    std::set<int> allowed_class_ids;
};

class TensorRtDetector final : public IDetector {
public:
    explicit TensorRtDetector(TensorRtDetectorConfig config = {});
    ~TensorRtDetector() override;

    TensorRtDetector(const TensorRtDetector&) = delete;
    TensorRtDetector& operator=(const TensorRtDetector&) = delete;
    TensorRtDetector(TensorRtDetector&&) noexcept;
    TensorRtDetector& operator=(TensorRtDetector&&) noexcept;

    bool initialize() override;
    bool is_initialized() const override;
    std::vector<Detection> detect(const catcheye::input::Frame& frame) override;
    std::string class_name(int class_id) const override;

private:
    struct Impl;

    TensorRtDetectorConfig config_;
    std::map<int, std::string> class_names_;
    std::unique_ptr<Impl> impl_;
    bool initialized_ = false;
};

} // namespace catcheye::detection

namespace catcheye {

using TensorRtDetectorConfig = detection::TensorRtDetectorConfig;
using TensorRtDetector = detection::TensorRtDetector;

} // namespace catcheye
