#pragma once

#include <vector>

#include "catcheye/detection/postprocess/detection_candidate.hpp"

namespace catcheye::detection {

struct DecodeResult {
    std::vector<DetectionCandidate> candidates;
    bool nms_already_applied = false;
    bool requires_nms = true;
};

} // namespace catcheye::detection

