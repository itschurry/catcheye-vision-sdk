#pragma once

#include "catcheye/detection/postprocess/decode_context.hpp"
#include "catcheye/detection/postprocess/decode_result.hpp"
#include "catcheye/detection/postprocess/tensor_view.hpp"

#include "hailo/hailort.h"

namespace catcheye::detection {

class HailoNmsDecoder {
public:
    DecodeResult decode(
        const TensorView& output,
        hailo_format_order_t order,
        const hailo_nms_shape_t& nms_shape,
        const ModelDecodeContext& context) const;
};

} // namespace catcheye::detection

