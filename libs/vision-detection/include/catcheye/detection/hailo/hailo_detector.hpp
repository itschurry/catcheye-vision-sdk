#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "catcheye/detection/detector.hpp"

namespace catcheye::detection {

enum class HailoOutputDecoder {
    Auto,
    NmsByClass,
    NmsByScore,
};

struct HailoDetectorConfig {
    std::string hef_path;
    std::string metadata_path;
    std::string input_name;
    std::string output_name;
    int input_width = 640;
    int input_height = 640;
    float confidence_threshold = 0.25F;
    float nms_threshold = 0.45F;
    int max_proposals_per_class = 100;
    int max_proposals_total = 100;
    int inference_timeout_ms = 1000;
    HailoOutputDecoder output_decoder = HailoOutputDecoder::Auto;
};

class HailoDetector final : public IDetector {
public:
    explicit HailoDetector(HailoDetectorConfig config = {});
    ~HailoDetector() override;

    HailoDetector(const HailoDetector&) = delete;
    HailoDetector& operator=(const HailoDetector&) = delete;
    HailoDetector(HailoDetector&&) noexcept;
    HailoDetector& operator=(HailoDetector&&) noexcept;

    bool initialize() override;
    bool is_initialized() const override;
    std::vector<Detection> detect(const catcheye::input::Frame& frame) override;
    std::string class_name(int class_id) const override;

private:
    struct Impl;

    HailoDetectorConfig config_;
    std::map<int, std::string> class_names_;
    std::unique_ptr<Impl> impl_;
    bool initialized_ = false;
};

} // namespace catcheye::detection

namespace catcheye {

using HailoDetectorConfig = detection::HailoDetectorConfig;
using HailoDetector = detection::HailoDetector;
using HailoOutputDecoder = detection::HailoOutputDecoder;

} // namespace catcheye
