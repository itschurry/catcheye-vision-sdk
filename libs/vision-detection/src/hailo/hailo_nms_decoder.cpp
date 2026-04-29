#include "hailo_nms_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace catcheye::detection {
namespace {

BoundingBox map_hailo_box(float x_min,
                          float y_min,
                          float x_max,
                          float y_max,
                          const ModelDecodeContext& context)
{
    if (std::max({std::abs(x_min), std::abs(y_min), std::abs(x_max), std::abs(y_max)}) <= 1.5F) {
        x_min *= static_cast<float>(context.input_width);
        x_max *= static_cast<float>(context.input_width);
        y_min *= static_cast<float>(context.input_height);
        y_max *= static_cast<float>(context.input_height);
    }

    const float left = static_cast<float>(context.pad_width / 2);
    const float top = static_cast<float>(context.pad_height / 2);

    x_min = (x_min - left) / context.letterbox_scale;
    x_max = (x_max - left) / context.letterbox_scale;
    y_min = (y_min - top) / context.letterbox_scale;
    y_max = (y_max - top) / context.letterbox_scale;

    x_min = std::clamp(x_min, 0.0F, static_cast<float>(context.original_width - 1));
    x_max = std::clamp(x_max, 0.0F, static_cast<float>(context.original_width - 1));
    y_min = std::clamp(y_min, 0.0F, static_cast<float>(context.original_height - 1));
    y_max = std::clamp(y_max, 0.0F, static_cast<float>(context.original_height - 1));

    return BoundingBox {
        .x = x_min,
        .y = y_min,
        .width = std::max(0.0F, x_max - x_min),
        .height = std::max(0.0F, y_max - y_min),
    };
}

void append_candidate(DecodeResult& result,
                      int class_id,
                      float score,
                      float x_min,
                      float y_min,
                      float x_max,
                      float y_max,
                      const ModelDecodeContext& context)
{
    const BoundingBox box = map_hailo_box(x_min, y_min, x_max, y_max, context);
    if (box.width <= 1.0F || box.height <= 1.0F) {
        return;
    }

    result.candidates.push_back(DetectionCandidate {
        .class_id = class_id,
        .score = score,
        .box = box,
    });
}

DecodeResult decode_nms_by_class(const TensorView& output,
                                 const hailo_nms_shape_t& shape,
                                 const ModelDecodeContext& context)
{
    DecodeResult result {
        .candidates = {},
        .nms_already_applied = true,
        .requires_nms = false,
    };

    const auto* values = static_cast<const float*>(output.data);
    const std::size_t float_count = output.byte_size / sizeof(float);
    std::size_t offset = 0;

    for (std::uint32_t class_id = 0; class_id < shape.number_of_classes; ++class_id) {
        if (offset >= float_count) {
            break;
        }

        const auto bbox_count = static_cast<std::uint32_t>(std::max(0.0F, values[offset++]));
        const std::uint32_t boxes_to_read = std::min(bbox_count, shape.max_bboxes_per_class);
        for (std::uint32_t box_index = 0; box_index < shape.max_bboxes_per_class; ++box_index) {
            if ((offset + 5U) > float_count) {
                return result;
            }

            const float y_min = values[offset++];
            const float x_min = values[offset++];
            const float y_max = values[offset++];
            const float x_max = values[offset++];
            const float score = values[offset++];

            if (box_index >= boxes_to_read) {
                continue;
            }

            append_candidate(result, static_cast<int>(class_id), score, x_min, y_min, x_max, y_max, context);
        }
    }

    return result;
}

DecodeResult decode_nms_by_score(const TensorView& output, const ModelDecodeContext& context)
{
    DecodeResult result {
        .candidates = {},
        .nms_already_applied = true,
        .requires_nms = false,
    };

    if (output.byte_size < sizeof(hailo_detections_t)) {
        return result;
    }

    const auto* hailo_detections = static_cast<const hailo_detections_t*>(output.data);
    const std::size_t max_count = (output.byte_size - sizeof(hailo_detections_t)) / sizeof(hailo_detection_t);
    const std::size_t count = std::min<std::size_t>(hailo_detections->count, max_count);

    result.candidates.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        const hailo_detection_t& item = hailo_detections->detections[i];
        append_candidate(
            result,
            static_cast<int>(item.class_id),
            item.score,
            item.x_min,
            item.y_min,
            item.x_max,
            item.y_max,
            context);
    }

    return result;
}

} // namespace

DecodeResult HailoNmsDecoder::decode(
    const TensorView& output,
    hailo_format_order_t order,
    const hailo_nms_shape_t& nms_shape,
    const ModelDecodeContext& context) const
{
    if (order == HAILO_FORMAT_ORDER_HAILO_NMS_BY_SCORE) {
        return decode_nms_by_score(output, context);
    }

    return decode_nms_by_class(output, nms_shape, context);
}

} // namespace catcheye::detection

