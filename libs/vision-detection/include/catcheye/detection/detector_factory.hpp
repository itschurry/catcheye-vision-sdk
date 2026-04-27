#pragma once

#include <memory>
#include <stdexcept>

#include "catcheye/detection/detector.hpp"
#include "catcheye/detection/hailo/hailo_detector.hpp"
#include "catcheye/detection/ncnn/ncnn_detector.hpp"

namespace catcheye::detection {

enum class DetectorBackend {
    Ncnn,
    Hailo,
};

struct DetectorFactoryConfig {
    DetectorBackend backend = DetectorBackend::Ncnn;
    NcnnDetectorConfig ncnn;
    HailoDetectorConfig hailo;
};

inline std::unique_ptr<IDetector> create_detector(const DetectorFactoryConfig& cfg)
{
    switch (cfg.backend) {
        case DetectorBackend::Ncnn:
#if defined(CATCHEYE_VISION_DETECTION_HAS_NCNN)
            return std::make_unique<NcnnDetector>(cfg.ncnn);
#else
            throw std::runtime_error("NCNN detector backend was not built");
#endif
        case DetectorBackend::Hailo:
#if defined(CATCHEYE_VISION_DETECTION_HAS_HAILO)
            return std::make_unique<HailoDetector>(cfg.hailo);
#else
            throw std::runtime_error("Hailo detector backend was not built");
#endif
    }

    throw std::runtime_error("unknown detector backend");
}

} // namespace catcheye::detection

namespace catcheye {

using DetectorBackend = detection::DetectorBackend;
using DetectorFactoryConfig = detection::DetectorFactoryConfig;
using detection::create_detector;

} // namespace catcheye
