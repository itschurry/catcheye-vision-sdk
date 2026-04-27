#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "catcheye/detection/detector.hpp"

namespace catcheye::detection {

struct NcnnDetectorConfig {
    std::string param_path;
    std::string bin_path;
    std::string metadata_path;
    std::string input_blob_name  = "in0";
    std::string output_blob_name = "out0";
    int input_width              = 640;
    int input_height             = 640;
    float confidence_threshold   = 0.25F;
    float nms_threshold          = 0.45F;
    int num_threads              = 2;
    bool use_vulkan_compute      = false;
};

class NcnnDetector final : public IDetector {
public:
    explicit NcnnDetector(NcnnDetectorConfig config = {});
    ~NcnnDetector() override;

    NcnnDetector(const NcnnDetector&) = delete;
    NcnnDetector& operator=(const NcnnDetector&) = delete;
    NcnnDetector(NcnnDetector&&) noexcept;
    NcnnDetector& operator=(NcnnDetector&&) noexcept;

    bool initialize() override;
    bool is_initialized() const override;
    std::vector<Detection> detect(const catcheye::input::Frame& frame) override;
    std::string class_name(int class_id) const override;

private:
    struct Impl;

    NcnnDetectorConfig config_;
    std::map<int, std::string> class_names_;
    std::unique_ptr<Impl> impl_;
    bool initialized_ = false;
};

using DetectorConfig = NcnnDetectorConfig;
using Detector = NcnnDetector;

} // namespace catcheye::detection

namespace catcheye {

using NcnnDetectorConfig = detection::NcnnDetectorConfig;
using NcnnDetector = detection::NcnnDetector;
using DetectorConfig = detection::DetectorConfig;
using Detector = detection::Detector;

} // namespace catcheye
