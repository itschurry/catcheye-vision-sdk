#pragma once

#include "catcheye/detection/bounding_box.hpp"

namespace catcheye::detection {

struct DetectionCandidate {
    int class_id = -1;
    float score = 0.0F;
    BoundingBox box{};
};

} // namespace catcheye::detection

