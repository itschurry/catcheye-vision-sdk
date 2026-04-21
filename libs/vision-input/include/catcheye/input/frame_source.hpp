#pragma once

#include <memory>
#include <string>

#include "catcheye/input/frame.hpp"

namespace catcheye::input {

enum class FrameReadStatus {
    Ok,
    EndOfStream,
    Error,
};

class FrameSource {
   public:
    virtual ~FrameSource() = default;

    virtual bool open() = 0;
    virtual bool is_open() const = 0;
    virtual FrameReadStatus read(Frame& frame) = 0;
    virtual void close() = 0;
    virtual std::string describe() const = 0;
};

} // namespace catcheye::input
