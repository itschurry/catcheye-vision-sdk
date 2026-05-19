#pragma once

#include <vector>

#include "catcheye/input/frame.hpp"

namespace catcheye::input {

struct RgbIntrinsicCalibrationBoard {
    int pattern_width = 0;
    int pattern_height = 0;
    float square_size_m = 0.0F;
};

struct RgbIntrinsicCalibrationResult {
    int image_width = 0;
    int image_height = 0;
    double rms_error = 0.0;
    double fx = 0.0;
    double fy = 0.0;
    double cx = 0.0;
    double cy = 0.0;
    double dist_k1 = 0.0;
    double dist_k2 = 0.0;
    double dist_p1 = 0.0;
    double dist_p2 = 0.0;
    double dist_k3 = 0.0;
};

class RgbIntrinsicCalibrator final {
  public:
    explicit RgbIntrinsicCalibrator(RgbIntrinsicCalibrationBoard board);

    bool add_frame(const Frame& frame);
    RgbIntrinsicCalibrationResult calibrate() const;
    int capture_count() const;
    void clear();

  private:
    RgbIntrinsicCalibrationBoard board_;
    int image_width_ = 0;
    int image_height_ = 0;
    std::vector<std::vector<float>> image_points_;
};

} // namespace catcheye::input
