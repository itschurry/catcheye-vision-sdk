#include "catcheye/detection/postprocess/postprocess.hpp"

#include <algorithm>
#include <cstddef>
#include <map>

#include <opencv2/dnn/dnn.hpp>

namespace catcheye::detection {
namespace {

bool class_allowed(int class_id, const PostprocessOptions& options)
{
    return options.allowed_class_ids.empty() || options.allowed_class_ids.contains(class_id);
}

Detection to_detection(const DetectionCandidate& candidate)
{
    return Detection {
        .class_id = candidate.class_id,
        .score = candidate.score,
        .box = candidate.box,
    };
}

std::vector<Detection> run_nms_for_candidates(
    const std::vector<DetectionCandidate>& candidates,
    const PostprocessOptions& options)
{
    std::vector<cv::Rect> boxes;
    std::vector<float> scores;
    boxes.reserve(candidates.size());
    scores.reserve(candidates.size());

    for (const DetectionCandidate& candidate : candidates) {
        boxes.emplace_back(
            static_cast<int>(candidate.box.x),
            static_cast<int>(candidate.box.y),
            std::max(0, static_cast<int>(candidate.box.width)),
            std::max(0, static_cast<int>(candidate.box.height)));
        scores.push_back(candidate.score);
    }

    std::vector<int> selected_indices;
    cv::dnn::NMSBoxes(boxes, scores, options.confidence_threshold, options.nms_threshold, selected_indices);

    std::vector<Detection> detections;
    detections.reserve(selected_indices.size());
    for (const int idx : selected_indices) {
        detections.push_back(to_detection(candidates[static_cast<std::size_t>(idx)]));
    }

    return detections;
}

} // namespace

std::vector<DetectionCandidate> filter_candidates(
    const std::vector<DetectionCandidate>& candidates,
    const PostprocessOptions& options)
{
    std::vector<DetectionCandidate> filtered;
    filtered.reserve(candidates.size());

    for (const DetectionCandidate& candidate : candidates) {
        if (candidate.score < options.confidence_threshold || !class_allowed(candidate.class_id, options)) {
            continue;
        }
        if (candidate.box.width <= 1.0F || candidate.box.height <= 1.0F) {
            continue;
        }
        filtered.push_back(candidate);
    }

    return filtered;
}

std::vector<Detection> postprocess_detections(
    const std::vector<DetectionCandidate>& candidates,
    const PostprocessOptions& options)
{
    const std::vector<DetectionCandidate> filtered = filter_candidates(candidates, options);
    if (!options.apply_nms) {
        std::vector<Detection> detections;
        detections.reserve(filtered.size());
        for (const DetectionCandidate& candidate : filtered) {
            detections.push_back(to_detection(candidate));
        }
        return detections;
    }

    if (!options.class_aware_nms) {
        return run_nms_for_candidates(filtered, options);
    }

    std::map<int, std::vector<DetectionCandidate>> candidates_by_class;
    for (const DetectionCandidate& candidate : filtered) {
        candidates_by_class[candidate.class_id].push_back(candidate);
    }

    std::vector<Detection> detections;
    for (const auto& [class_id, class_candidates] : candidates_by_class) {
        (void)class_id;
        std::vector<Detection> class_detections = run_nms_for_candidates(class_candidates, options);
        detections.insert(detections.end(), class_detections.begin(), class_detections.end());
    }

    return detections;
}

std::vector<Detection> finalize_detections(
    const DecodeResult& decoded,
    const PostprocessOptions& options)
{
    PostprocessOptions effective_options = options;
    if (decoded.nms_already_applied || !decoded.requires_nms) {
        effective_options.apply_nms = false;
    }

    return postprocess_detections(decoded.candidates, effective_options);
}

std::vector<Detection> filter_detections(
    const std::vector<Detection>& detections,
    const PostprocessOptions& options)
{
    std::vector<Detection> filtered;
    filtered.reserve(detections.size());

    for (const Detection& detection : detections) {
        if (detection.score < options.confidence_threshold || !class_allowed(detection.class_id, options)) {
            continue;
        }
        if (detection.box.width <= 1.0F || detection.box.height <= 1.0F) {
            continue;
        }
        filtered.push_back(detection);
    }

    return filtered;
}

} // namespace catcheye::detection
