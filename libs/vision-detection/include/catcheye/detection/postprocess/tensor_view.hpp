#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace catcheye::detection {

enum class TensorDataType {
    Float32,
    UInt8,
};

struct TensorView {
    std::string name;
    const void* data = nullptr;
    std::size_t byte_size = 0;
    std::vector<int> shape;
    TensorDataType data_type = TensorDataType::Float32;
    float quant_scale = 1.0F;
    int quant_zero_point = 0;
};

} // namespace catcheye::detection

