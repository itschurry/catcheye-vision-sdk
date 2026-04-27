#pragma once

#include <memory>

#include "catcheye/detection/detector.hpp"
#include "catcheye/detection/ncnn/ncnn_detector.hpp"

namespace catcheye::detection {

struct DetectorFactoryConfig {
    NcnnDetectorConfig ncnn;
};

inline std::unique_ptr<IDetector> create_detector(const DetectorFactoryConfig& cfg)
{
    return std::make_unique<NcnnDetector>(cfg.ncnn);
}

} // namespace catcheye::detection

namespace catcheye {

using DetectorFactoryConfig = detection::DetectorFactoryConfig;
using detection::create_detector;

} // namespace catcheye
