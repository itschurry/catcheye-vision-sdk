#pragma once

#include <vector>

#include "catcheye/detection/postprocess/decode_context.hpp"
#include "catcheye/detection/postprocess/decode_result.hpp"
#include "catcheye/detection/postprocess/tensor_view.hpp"

namespace catcheye::detection {

struct YoloDecoderOptions {
    int num_classes = 0;
    bool requires_nms = true;
};

class YoloDecoder {
public:
    explicit YoloDecoder(YoloDecoderOptions options = {});

    DecodeResult decode(
        const std::vector<TensorView>& outputs,
        const ModelDecodeContext& context) const;

private:
    YoloDecoderOptions options_;
};

} // namespace catcheye::detection
