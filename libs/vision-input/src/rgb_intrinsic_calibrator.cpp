#include "catcheye/input/rgb_intrinsic_calibrator.hpp"

#include <stdexcept>

#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>

namespace catcheye::input {
namespace {

cv::Mat frame_to_gray(const Frame& frame)
{
    if (frame.empty() || frame.width <= 0 || frame.height <= 0 || frame.stride <= 0) {
        throw std::runtime_error("invalid RGB frame");
    }
    const auto stride = static_cast<std::size_t>(frame.stride);

    switch (frame.format) {
        case PixelFormat::GRAY8:
            return cv::Mat(frame.height, frame.width, CV_8UC1, const_cast<std::uint8_t*>(frame.data.data()), stride).clone();
        case PixelFormat::NV12: {
            const int uv_height = frame.height / 2;
            cv::Mat nv12(frame.height + uv_height, frame.stride, CV_8UC1, const_cast<std::uint8_t*>(frame.data.data()));
            cv::Mat gray;
            cv::cvtColor(nv12, gray, cv::COLOR_YUV2GRAY_NV12);
            return gray(cv::Rect(0, 0, frame.width, frame.height)).clone();
        }
        case PixelFormat::RGB: {
            cv::Mat rgb(frame.height, frame.width, CV_8UC3, const_cast<std::uint8_t*>(frame.data.data()), stride);
            cv::Mat gray;
            cv::cvtColor(rgb, gray, cv::COLOR_RGB2GRAY);
            return gray;
        }
        case PixelFormat::BGR: {
            cv::Mat bgr(frame.height, frame.width, CV_8UC3, const_cast<std::uint8_t*>(frame.data.data()), stride);
            cv::Mat gray;
            cv::cvtColor(bgr, gray, cv::COLOR_BGR2GRAY);
            return gray;
        }
        case PixelFormat::RGBA: {
            cv::Mat rgba(frame.height, frame.width, CV_8UC4, const_cast<std::uint8_t*>(frame.data.data()), stride);
            cv::Mat gray;
            cv::cvtColor(rgba, gray, cv::COLOR_RGBA2GRAY);
            return gray;
        }
        case PixelFormat::BGRA: {
            cv::Mat bgra(frame.height, frame.width, CV_8UC4, const_cast<std::uint8_t*>(frame.data.data()), stride);
            cv::Mat gray;
            cv::cvtColor(bgra, gray, cv::COLOR_BGRA2GRAY);
            return gray;
        }
        case PixelFormat::UNKNOWN:
            break;
    }
    throw std::runtime_error("unsupported RGB frame format");
}

std::vector<cv::Point3f> object_points(const RgbIntrinsicCalibrationBoard& board)
{
    std::vector<cv::Point3f> points;
    points.reserve(static_cast<std::size_t>(board.pattern_width * board.pattern_height));
    for (int y = 0; y < board.pattern_height; ++y) {
        for (int x = 0; x < board.pattern_width; ++x) {
            points.emplace_back(
                static_cast<float>(x) * board.square_size_m,
                static_cast<float>(y) * board.square_size_m,
                0.0F);
        }
    }
    return points;
}

} // namespace

RgbIntrinsicCalibrator::RgbIntrinsicCalibrator(RgbIntrinsicCalibrationBoard board)
    : board_(board)
{
    if (board_.pattern_width <= 0 || board_.pattern_height <= 0 || board_.square_size_m <= 0.0F) {
        throw std::invalid_argument("invalid RGB intrinsic calibration board");
    }
}

bool RgbIntrinsicCalibrator::add_frame(const Frame& frame)
{
    cv::Mat gray = frame_to_gray(frame);
    if (image_width_ == 0 || image_height_ == 0) {
        image_width_ = frame.width;
        image_height_ = frame.height;
    }
    if (frame.width != image_width_ || frame.height != image_height_) {
        throw std::runtime_error("RGB calibration frame size changed");
    }

    std::vector<cv::Point2f> corners;
    const cv::Size pattern_size(board_.pattern_width, board_.pattern_height);
    const bool found = cv::findChessboardCorners(
        gray,
        pattern_size,
        corners,
        cv::CALIB_CB_ADAPTIVE_THRESH | cv::CALIB_CB_NORMALIZE_IMAGE | cv::CALIB_CB_FAST_CHECK);
    if (!found) {
        return false;
    }

    cv::cornerSubPix(
        gray,
        corners,
        cv::Size(11, 11),
        cv::Size(-1, -1),
        cv::TermCriteria(cv::TermCriteria::EPS + cv::TermCriteria::MAX_ITER, 30, 0.001));

    std::vector<float> flat;
    flat.reserve(corners.size() * 2U);
    for (const auto& corner : corners) {
        flat.push_back(corner.x);
        flat.push_back(corner.y);
    }
    image_points_.push_back(std::move(flat));
    return true;
}

RgbIntrinsicCalibrationResult RgbIntrinsicCalibrator::calibrate() const
{
    if (image_points_.size() < 8U || image_width_ <= 0 || image_height_ <= 0) {
        throw std::runtime_error("not enough RGB intrinsic calibration captures");
    }

    std::vector<std::vector<cv::Point2f>> image_points;
    image_points.reserve(image_points_.size());
    for (const auto& flat : image_points_) {
        std::vector<cv::Point2f> corners;
        corners.reserve(flat.size() / 2U);
        for (std::size_t i = 0; i + 1U < flat.size(); i += 2U) {
            corners.emplace_back(flat[i], flat[i + 1U]);
        }
        image_points.push_back(std::move(corners));
    }

    const auto object_template = object_points(board_);
    std::vector<std::vector<cv::Point3f>> object_points_list(image_points.size(), object_template);

    cv::Mat camera_matrix = cv::Mat::eye(3, 3, CV_64F);
    cv::Mat distortion = cv::Mat::zeros(1, 5, CV_64F);
    std::vector<cv::Mat> rvecs;
    std::vector<cv::Mat> tvecs;
    const double rms = cv::calibrateCamera(
        object_points_list,
        image_points,
        cv::Size(image_width_, image_height_),
        camera_matrix,
        distortion,
        rvecs,
        tvecs);

    return RgbIntrinsicCalibrationResult{
        .image_width = image_width_,
        .image_height = image_height_,
        .rms_error = rms,
        .fx = camera_matrix.at<double>(0, 0),
        .fy = camera_matrix.at<double>(1, 1),
        .cx = camera_matrix.at<double>(0, 2),
        .cy = camera_matrix.at<double>(1, 2),
        .dist_k1 = distortion.at<double>(0, 0),
        .dist_k2 = distortion.at<double>(0, 1),
        .dist_p1 = distortion.at<double>(0, 2),
        .dist_p2 = distortion.at<double>(0, 3),
        .dist_k3 = distortion.at<double>(0, 4),
    };
}

int RgbIntrinsicCalibrator::capture_count() const
{
    return static_cast<int>(image_points_.size());
}

void RgbIntrinsicCalibrator::clear()
{
    image_width_ = 0;
    image_height_ = 0;
    image_points_.clear();
}

} // namespace catcheye::input
