#pragma once

namespace catcheye::detection {

struct ModelDecodeContext {
    int input_width = 0;
    int input_height = 0;
    int original_width = 0;
    int original_height = 0;
    float letterbox_scale = 1.0F;
    int pad_width = 0;
    int pad_height = 0;
};

} // namespace catcheye::detection

