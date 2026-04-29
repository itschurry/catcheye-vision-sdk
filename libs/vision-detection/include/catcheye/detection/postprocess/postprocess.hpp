#pragma once

#include <set>
#include <vector>

#include "catcheye/detection/detector.hpp"
#include "catcheye/detection/postprocess/decode_result.hpp"
#include "catcheye/detection/postprocess/detection_candidate.hpp"

namespace catcheye::detection {

struct PostprocessOptions {
    float confidence_threshold = 0.25F;
    float nms_threshold = 0.45F;
    bool class_aware_nms = true;
    bool apply_nms = true;
    std::set<int> allowed_class_ids;
};

std::vector<DetectionCandidate> filter_candidates(
    const std::vector<DetectionCandidate>& candidates,
    const PostprocessOptions& options);

std::vector<Detection> postprocess_detections(
    const std::vector<DetectionCandidate>& candidates,
    const PostprocessOptions& options);

std::vector<Detection> finalize_detections(
    const DecodeResult& decoded,
    const PostprocessOptions& options);

std::vector<Detection> filter_detections(
    const std::vector<Detection>& detections,
    const PostprocessOptions& options);

} // namespace catcheye::detection
