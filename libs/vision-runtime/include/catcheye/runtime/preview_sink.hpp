#pragma once

#include <opencv2/core/mat.hpp>

namespace catcheye::runtime {

class PreviewSink {
   public:
    virtual ~PreviewSink() = default;

    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual void publish(const cv::Mat& frame) = 0;
};

} // namespace catcheye::runtime
