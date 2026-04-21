#pragma once

#include <string>

namespace catcheye::protocol {

// Application-level result metadata attached to a processed frame.
// Raw video data is carried separately by catcheye::input::Frame.
struct FrameMessage {
    std::string stream_name;
    std::string metadata_json = "{}";

    bool empty() const { return stream_name.empty(); }
};

} // namespace catcheye::protocol
