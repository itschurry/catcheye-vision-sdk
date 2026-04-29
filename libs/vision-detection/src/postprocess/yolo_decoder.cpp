#include "catcheye/detection/postprocess/yolo_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>

namespace catcheye::detection {
namespace {

struct MatrixLayout {
    int attribute_count = 0;
    int candidate_count = 0;
    bool attribute_major = true;
};

bool valid_context(const ModelDecodeContext& context)
{
    return context.input_width > 0 &&
           context.input_height > 0 &&
           context.original_width > 0 &&
           context.original_height > 0 &&
           context.letterbox_scale > 0.0F;
}

float tensor_value(const float* values, const MatrixLayout& layout, int attribute_index, int candidate_index)
{
    if (layout.attribute_major) {
        return values[(static_cast<std::size_t>(attribute_index) * static_cast<std::size_t>(layout.candidate_count)) +
                      static_cast<std::size_t>(candidate_index)];
    }

    return values[(static_cast<std::size_t>(candidate_index) * static_cast<std::size_t>(layout.attribute_count)) +
                  static_cast<std::size_t>(attribute_index)];
}

bool infer_matrix_layout(const TensorView& tensor, int num_classes, MatrixLayout& layout)
{
    if (tensor.data_type != TensorDataType::Float32 || tensor.data == nullptr) {
        return false;
    }

    const std::size_t float_count = tensor.byte_size / sizeof(float);
    if (float_count == 0) {
        return false;
    }

    std::vector<int> dims;
    dims.reserve(tensor.shape.size());
    for (const int dim : tensor.shape) {
        if (dim > 1) {
            dims.push_back(dim);
        }
    }

    if (dims.size() != 2) {
        return false;
    }

    const int first = dims[0];
    const int second = dims[1];
    const int min_attributes = (num_classes > 0) ? (4 + num_classes) : 6;
    const int expected_attributes = (num_classes > 0) ? (4 + num_classes) : 0;

    if (expected_attributes > 0) {
        if (first >= expected_attributes && second > first) {
            layout = MatrixLayout {
                .attribute_count = first,
                .candidate_count = second,
                .attribute_major = true,
            };
            return true;
        }
        if (second >= expected_attributes && first > second) {
            layout = MatrixLayout {
                .attribute_count = second,
                .candidate_count = first,
                .attribute_major = false,
            };
            return true;
        }
    }

    if (first >= min_attributes && second > first) {
        layout = MatrixLayout {
            .attribute_count = first,
            .candidate_count = second,
            .attribute_major = true,
        };
        return true;
    }
    if (second >= min_attributes && first > second) {
        layout = MatrixLayout {
            .attribute_count = second,
            .candidate_count = first,
            .attribute_major = false,
        };
        return true;
    }

    return false;
}

BoundingBox map_center_box(float center_x,
                           float center_y,
                           float width,
                           float height,
                           const ModelDecodeContext& context)
{
    float x_min = center_x - (width * 0.5F);
    float y_min = center_y - (height * 0.5F);
    float x_max = center_x + (width * 0.5F);
    float y_max = center_y + (height * 0.5F);

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

} // namespace

YoloDecoder::YoloDecoder(YoloDecoderOptions options)
    : options_(options)
{
}

DecodeResult YoloDecoder::decode(
    const std::vector<TensorView>& outputs,
    const ModelDecodeContext& context) const
{
    DecodeResult result {
        .candidates = {},
        .nms_already_applied = false,
        .requires_nms = options_.requires_nms,
    };
    if (!valid_context(context)) {
        std::cerr << "invalid YOLO decode context\n";
        return result;
    }

    for (const TensorView& tensor : outputs) {
        MatrixLayout layout;
        if (!infer_matrix_layout(tensor, options_.num_classes, layout)) {
            std::cerr << "unsupported YOLO output tensor";
            if (!tensor.name.empty()) {
                std::cerr << " '" << tensor.name << "'";
            }
            std::cerr << ": shape=[";
            for (std::size_t i = 0; i < tensor.shape.size(); ++i) {
                if (i > 0) {
                    std::cerr << ",";
                }
                std::cerr << tensor.shape[i];
            }
            std::cerr << "], bytes=" << tensor.byte_size << '\n';
            continue;
        }

        const float* values = static_cast<const float*>(tensor.data);
        const int class_count = (options_.num_classes > 0)
            ? std::min(options_.num_classes, layout.attribute_count - 4)
            : (layout.attribute_count - 4);
        result.candidates.reserve(result.candidates.size() + static_cast<std::size_t>(layout.candidate_count));

        for (int index = 0; index < layout.candidate_count; ++index) {
            int best_class_id = -1;
            float best_score = 0.0F;

            for (int class_offset = 0; class_offset < class_count; ++class_offset) {
                const float score = tensor_value(values, layout, 4 + class_offset, index);
                if (score > best_score) {
                    best_score = score;
                    best_class_id = class_offset;
                }
            }

            const BoundingBox box = map_center_box(
                tensor_value(values, layout, 0, index),
                tensor_value(values, layout, 1, index),
                tensor_value(values, layout, 2, index),
                tensor_value(values, layout, 3, index),
                context);

            if (box.width <= 1.0F || box.height <= 1.0F) {
                continue;
            }

            result.candidates.push_back(DetectionCandidate {
                .class_id = best_class_id,
                .score = best_score,
                .box = box,
            });
        }
    }

    return result;
}

} // namespace catcheye::detection
