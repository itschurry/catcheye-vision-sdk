#pragma once

#include <string>

namespace catcheye::protocol {

// Application-level message contract shared between processors and publishers.
// Raw frame bytes and transport-specific encoding are handled elsewhere.
struct FrameMessage {
    std::string stream_name;
    std::string metadata_json = "{}";

    bool empty() const { return stream_name.empty(); }
};

} // namespace catcheye::protocol
