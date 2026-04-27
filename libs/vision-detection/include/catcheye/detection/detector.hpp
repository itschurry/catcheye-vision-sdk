#pragma once

#include <string>
#include <vector>

#include "catcheye/detection/bounding_box.hpp"
#include "catcheye/input/frame.hpp"

namespace catcheye::detection {

struct Detection {
    int class_id = -1;
    float score = 0.0F;
    BoundingBox box{};
};

class IDetector {
public:
    virtual ~IDetector() = default;

    virtual bool initialize() = 0;
    virtual bool is_initialized() const = 0;
    virtual std::vector<Detection> detect(const catcheye::input::Frame& frame) = 0;
    virtual std::string class_name(int class_id) const = 0;
};

} // namespace catcheye::detection

namespace catcheye {

using Detection = detection::Detection;
using IDetector = detection::IDetector;

} // namespace catcheye
