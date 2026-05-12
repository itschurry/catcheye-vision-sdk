#include <cassert>
#include <cstdint>
#include <vector>

#include "catcheye/visualization/annotation_renderer.hpp"

int main()
{
    catcheye::input::Frame source;
    source.width = 8;
    source.height = 8;
    source.stride = 8 * 3;
    source.format = catcheye::input::PixelFormat::BGR;
    source.timestamp = 123;
    source.data.assign(static_cast<std::size_t>(source.stride * source.height), 0);

    catcheye::input::Frame output;
    assert(catcheye::visualization::build_annotated_detection_frame(source, {}, output));
    assert(output.width == source.width);
    assert(output.height == source.height);
    assert(output.format == catcheye::input::PixelFormat::BGR);
    assert(output.timestamp == source.timestamp);

    const std::vector<catcheye::Detection> detections{
        catcheye::Detection{.class_id = 0, .score = 1.0F, .box = {.x = -2.0F, .y = -1.0F, .width = 20.0F, .height = 20.0F}},
    };
    assert(catcheye::visualization::build_annotated_detection_frame(source, detections, output));
    assert(output.data.size() == source.data.size());
    return 0;
}
