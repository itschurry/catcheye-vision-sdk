#include "catcheye/detection/postprocess/yolo_decoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <map>
#include <utility>

namespace catcheye::detection {
namespace {

struct MatrixLayout {
    int attribute_count = 0;
    int candidate_count = 0;
    bool attribute_major = true;
};

struct SplitHeadPair {
    const TensorView* boxes = nullptr;
    const TensorView* scores = nullptr;
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

bool is_split_box_head(const TensorView& tensor)
{
    return tensor.data_type == TensorDataType::Float32 &&
           tensor.data != nullptr &&
           tensor.shape.size() == 3 &&
           tensor.shape[0] > 0 &&
           tensor.shape[1] > 0 &&
           tensor.shape[2] == 4;
}

bool is_split_score_head(const TensorView& tensor, int num_classes)
{
    if (tensor.data_type != TensorDataType::Float32 ||
        tensor.data == nullptr ||
        tensor.shape.size() != 3 ||
        tensor.shape[0] <= 0 ||
        tensor.shape[1] <= 0) {
        return false;
    }

    return num_classes > 0 ? tensor.shape[2] >= num_classes : tensor.shape[2] > 4;
}

std::vector<SplitHeadPair> find_split_head_pairs(const std::vector<TensorView>& outputs, int num_classes)
{
    std::map<std::pair<int, int>, SplitHeadPair> pairs;

    for (const TensorView& tensor : outputs) {
        const bool box_head = is_split_box_head(tensor);
        const bool score_head = is_split_score_head(tensor, num_classes);
        if (!box_head && !score_head) {
            continue;
        }

        SplitHeadPair& pair = pairs[{tensor.shape[0], tensor.shape[1]}];
        if (box_head) {
            pair.boxes = &tensor;
        } else {
            pair.scores = &tensor;
        }
    }

    std::vector<SplitHeadPair> result;
    result.reserve(pairs.size());
    for (const auto& [shape, pair] : pairs) {
        (void)shape;
        if (pair.boxes != nullptr && pair.scores != nullptr) {
            result.push_back(pair);
        }
    }

    return result;
}

float nhwc_value(const TensorView& tensor, int row, int col, int channel)
{
    const auto* values = static_cast<const float*>(tensor.data);
    const auto width = static_cast<std::size_t>(tensor.shape[1]);
    const auto channels = static_cast<std::size_t>(tensor.shape[2]);
    const auto offset =
        ((static_cast<std::size_t>(row) * width + static_cast<std::size_t>(col)) * channels) +
        static_cast<std::size_t>(channel);
    return values[offset];
}

BoundingBox map_grid_distance_box(
    float left_distance,
    float top_distance,
    float right_distance,
    float bottom_distance,
    int row,
    int col,
    int grid_width,
    int grid_height,
    const ModelDecodeContext& context)
{
    const float stride_x = static_cast<float>(context.input_width) / static_cast<float>(grid_width);
    const float stride_y = static_cast<float>(context.input_height) / static_cast<float>(grid_height);
    const float anchor_x = static_cast<float>(col) + 0.5F;
    const float anchor_y = static_cast<float>(row) + 0.5F;

    float x_min = (anchor_x - left_distance) * stride_x;
    float y_min = (anchor_y - top_distance) * stride_y;
    float x_max = (anchor_x + right_distance) * stride_x;
    float y_max = (anchor_y + bottom_distance) * stride_y;

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

DecodeResult decode_split_heads(
    const std::vector<SplitHeadPair>& pairs,
    const ModelDecodeContext& context,
    int num_classes,
    bool requires_nms)
{
    DecodeResult result {
        .candidates = {},
        .nms_already_applied = false,
        .requires_nms = requires_nms,
    };

    std::size_t candidate_count = 0;
    for (const SplitHeadPair& pair : pairs) {
        candidate_count += static_cast<std::size_t>(pair.boxes->shape[0]) * static_cast<std::size_t>(pair.boxes->shape[1]);
    }
    result.candidates.reserve(candidate_count);

    for (const SplitHeadPair& pair : pairs) {
        const TensorView& boxes = *pair.boxes;
        const TensorView& scores = *pair.scores;
        const int height = boxes.shape[0];
        const int width = boxes.shape[1];
        const int class_count = (num_classes > 0) ? std::min(num_classes, scores.shape[2]) : scores.shape[2];

        for (int row = 0; row < height; ++row) {
            for (int col = 0; col < width; ++col) {
                int best_class_id = -1;
                float best_score = 0.0F;

                for (int class_id = 0; class_id < class_count; ++class_id) {
                    const float score = nhwc_value(scores, row, col, class_id);
                    if (score > best_score) {
                        best_score = score;
                        best_class_id = class_id;
                    }
                }

                const BoundingBox box = map_grid_distance_box(
                    nhwc_value(boxes, row, col, 0),
                    nhwc_value(boxes, row, col, 1),
                    nhwc_value(boxes, row, col, 2),
                    nhwc_value(boxes, row, col, 3),
                    row,
                    col,
                    width,
                    height,
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
    }

    return result;
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

    const std::vector<SplitHeadPair> split_head_pairs = find_split_head_pairs(outputs, options_.num_classes);
    if (!split_head_pairs.empty()) {
        return decode_split_heads(split_head_pairs, context, options_.num_classes, options_.requires_nms);
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
