#pragma once

namespace catcheye::detection {

struct BoundingBox {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;
};

} // namespace catcheye::detection

namespace catcheye {

using BoundingBox = detection::BoundingBox;

} // namespace catcheye
