#include "catcheye/transport/websocket_publisher.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <sstream>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace catcheye::transport {
namespace {

constexpr int SOCKET_ENABLE = 1;
constexpr int POLL_TIMEOUT_MS = 200;
constexpr std::size_t MAX_HANDSHAKE_BYTES = 8192;

const char* pixel_format_name(catcheye::input::PixelFormat format) {
    switch (format) {
    case catcheye::input::PixelFormat::RGB:
        return "RGB";
    case catcheye::input::PixelFormat::BGR:
        return "BGR";
    case catcheye::input::PixelFormat::RGBA:
        return "RGBA";
    case catcheye::input::PixelFormat::BGRA:
        return "BGRA";
    case catcheye::input::PixelFormat::GRAY8:
        return "GRAY8";
    case catcheye::input::PixelFormat::NV12:
        return "NV12";
    case catcheye::input::PixelFormat::UNKNOWN:
        return "UNKNOWN";
    }

    return "UNKNOWN";
}

cv::Mat frame_to_bgr(const catcheye::input::Frame& frame) {
    if (frame.empty() || frame.width <= 0 || frame.height <= 0 || frame.stride <= 0) {
        return {};
    }

    const std::size_t expected_size =
        catcheye::input::frame_data_size(frame.format, frame.stride, frame.height);
    if (frame.data.size() < expected_size) {
        return {};
    }

    auto* raw = const_cast<std::uint8_t*>(frame.data.data());
    switch (frame.format) {
    case catcheye::input::PixelFormat::BGR: {
        cv::Mat wrapped(frame.height, frame.width, CV_8UC3, raw, static_cast<std::size_t>(frame.stride));
        return wrapped.clone();
    }
    case catcheye::input::PixelFormat::RGB: {
        cv::Mat wrapped(frame.height, frame.width, CV_8UC3, raw, static_cast<std::size_t>(frame.stride));
        cv::Mat bgr;
        cv::cvtColor(wrapped, bgr, cv::COLOR_RGB2BGR);
        return bgr;
    }
    case catcheye::input::PixelFormat::RGBA: {
        cv::Mat wrapped(frame.height, frame.width, CV_8UC4, raw, static_cast<std::size_t>(frame.stride));
        cv::Mat bgr;
        cv::cvtColor(wrapped, bgr, cv::COLOR_RGBA2BGR);
        return bgr;
    }
    case catcheye::input::PixelFormat::BGRA: {
        cv::Mat wrapped(frame.height, frame.width, CV_8UC4, raw, static_cast<std::size_t>(frame.stride));
        cv::Mat bgr;
        cv::cvtColor(wrapped, bgr, cv::COLOR_BGRA2BGR);
        return bgr;
    }
    case catcheye::input::PixelFormat::GRAY8: {
        cv::Mat wrapped(frame.height, frame.width, CV_8UC1, raw, static_cast<std::size_t>(frame.stride));
        cv::Mat bgr;
        cv::cvtColor(wrapped, bgr, cv::COLOR_GRAY2BGR);
        return bgr;
    }
    case catcheye::input::PixelFormat::NV12: {
        cv::Mat wrapped(frame.height + (frame.height / 2), frame.width, CV_8UC1, raw, static_cast<std::size_t>(frame.stride));
        cv::Mat bgr;
        cv::cvtColor(wrapped, bgr, cv::COLOR_YUV2BGR_NV12);
        return bgr;
    }
    case catcheye::input::PixelFormat::UNKNOWN:
        break;
    }

    return {};
}

bool encode_jpeg_payload(const catcheye::input::Frame& frame, std::vector<std::uint8_t>& jpeg_bytes) {
    const cv::Mat bgr = frame_to_bgr(frame);
    if (bgr.empty()) {
        return false;
    }

    const std::vector<int> encode_params {
        cv::IMWRITE_JPEG_QUALITY,
        80,
    };
    return cv::imencode(".jpg", bgr, jpeg_bytes, encode_params);
}

bool send_all(int sock_fd, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const std::byte*>(data);
    std::span<const std::byte> remaining{bytes, size};

    while (!remaining.empty()) {
        const ssize_t written = ::send(sock_fd, remaining.data(), remaining.size(), MSG_NOSIGNAL);
        if (written <= 0) {
            return false;
        }
        remaining = remaining.subspan(static_cast<std::size_t>(written));
    }

    return true;
}

std::string base64_encode(std::span<const std::uint8_t> input) {
    static constexpr std::string_view TABLE = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::string output;
    output.reserve(((input.size() + 2U) / 3U) * 4U);

    for (std::size_t i = 0; i < input.size(); i += 3U) {
        const std::uint32_t octet_a = input[i];
        const std::uint32_t octet_b = (i + 1U < input.size()) ? input[i + 1U] : 0U;
        const std::uint32_t octet_c = (i + 2U < input.size()) ? input[i + 2U] : 0U;

        const std::uint32_t triple = (octet_a << 16U) | (octet_b << 8U) | octet_c;

        output.push_back(TABLE[(triple >> 18U) & 0x3FU]);
        output.push_back(TABLE[(triple >> 12U) & 0x3FU]);
        output.push_back(i + 1U < input.size() ? TABLE[(triple >> 6U) & 0x3FU] : '=');
        output.push_back(i + 2U < input.size() ? TABLE[triple & 0x3FU] : '=');
    }

    return output;
}

std::array<std::uint32_t, 5> sha1_digest(std::string_view input) {
    auto rotate_left = [](std::uint32_t value, int bits) { return static_cast<std::uint32_t>((value << bits) | (value >> (32 - bits))); };

    std::vector<std::uint8_t> bytes(input.begin(), input.end());
    const std::uint64_t bit_length = static_cast<std::uint64_t>(bytes.size()) * 8ULL;

    bytes.push_back(0x80U);
    while ((bytes.size() % 64U) != 56U) {
        bytes.push_back(0U);
    }

    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::uint8_t>((bit_length >> shift) & 0xFFU));
    }

    std::uint32_t h0 = 0x67452301U;
    std::uint32_t h1 = 0xEFCDAB89U;
    std::uint32_t h2 = 0x98BADCFEU;
    std::uint32_t h3 = 0x10325476U;
    std::uint32_t h4 = 0xC3D2E1F0U;

    std::array<std::uint32_t, 80> w{};
    for (std::size_t chunk = 0; chunk < bytes.size(); chunk += 64U) {
        for (std::size_t i = 0; i < 16U; ++i) {
            const std::size_t offset = chunk + (i * 4U);
            w[i] = (static_cast<std::uint32_t>(bytes[offset]) << 24U) | (static_cast<std::uint32_t>(bytes[offset + 1U]) << 16U) |
                   (static_cast<std::uint32_t>(bytes[offset + 2U]) << 8U) | static_cast<std::uint32_t>(bytes[offset + 3U]);
        }
        for (std::size_t i = 16U; i < 80U; ++i) {
            w[i] = rotate_left(w[i - 3U] ^ w[i - 8U] ^ w[i - 14U] ^ w[i - 16U], 1);
        }

        std::uint32_t a = h0;
        std::uint32_t b = h1;
        std::uint32_t c = h2;
        std::uint32_t d = h3;
        std::uint32_t e = h4;

        for (std::size_t i = 0; i < 80U; ++i) {
            std::uint32_t f = 0U;
            std::uint32_t k = 0U;
            if (i < 20U) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999U;
            } else if (i < 40U) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1U;
            } else if (i < 60U) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCU;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6U;
            }

            const std::uint32_t temp = rotate_left(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotate_left(b, 30);
            b = a;
            a = temp;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    return {h0, h1, h2, h3, h4};
}

std::string websocket_accept_key(std::string_view client_key) {
    static constexpr std::string_view MAGIC = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    const std::string combined = std::string(client_key) + std::string(MAGIC);
    const auto digest_words = sha1_digest(combined);

    std::array<std::uint8_t, 20> digest_bytes{};
    for (std::size_t i = 0; i < digest_words.size(); ++i) {
        const std::uint32_t word = digest_words[i];
        digest_bytes[i * 4U] = static_cast<std::uint8_t>((word >> 24U) & 0xFFU);
        digest_bytes[i * 4U + 1U] = static_cast<std::uint8_t>((word >> 16U) & 0xFFU);
        digest_bytes[i * 4U + 2U] = static_cast<std::uint8_t>((word >> 8U) & 0xFFU);
        digest_bytes[i * 4U + 3U] = static_cast<std::uint8_t>(word & 0xFFU);
    }

    return base64_encode(digest_bytes);
}

std::string find_header_value(std::string_view request, std::string_view header_name) {
    const std::string needle = std::string(header_name) + ":";
    std::istringstream iss{std::string(request)};
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.size() <= needle.size()) {
            continue;
        }
        const std::string prefix = line.substr(0, needle.size());
        std::string prefix_lower = prefix;
        std::string needle_lower = needle;
        std::transform(prefix_lower.begin(), prefix_lower.end(), prefix_lower.begin(), ::tolower);
        std::transform(needle_lower.begin(), needle_lower.end(), needle_lower.begin(), ::tolower);
        if (prefix_lower == needle_lower) {
            std::string value = line.substr(needle.size());
            while (!value.empty() && value.front() == ' ') {
                value.erase(value.begin());
            }
            return value;
        }
    }
    return {};
}

std::string build_metadata_frame(
    const catcheye::input::Frame& frame,
    const catcheye::protocol::FrameMessage& message,
    const PublishContext& context,
    std::size_t payload_size) {
    std::ostringstream oss;
    oss << "{"
        << "\"type\":\"frame\","
        << "\"frame_index\":" << context.frame_index << ','
        << "\"stream_name\":\"" << message.stream_name << "\","
        << "\"width\":" << frame.width << ','
        << "\"height\":" << frame.height << ','
        << "\"stride\":" << frame.stride << ','
        << "\"pixel_format\":\"" << pixel_format_name(frame.format) << "\","
        << "\"timestamp\":" << frame.timestamp << ','
        << "\"payload_encoding\":\"jpeg\","
        << "\"payload_size\":" << payload_size << ','
        << "\"metadata\":" << message.metadata_json
        << "}";
    return oss.str();
}

std::vector<std::uint8_t> websocket_frame(std::span<const std::uint8_t> payload, std::uint8_t opcode) {
    std::vector<std::uint8_t> frame;
    frame.reserve(payload.size() + 16U);

    frame.push_back(static_cast<std::uint8_t>(0x80U | opcode));
    if (payload.size() <= 125U) {
        frame.push_back(static_cast<std::uint8_t>(payload.size()));
    } else if (payload.size() <= 0xFFFFU) {
        frame.push_back(126U);
        frame.push_back(static_cast<std::uint8_t>((payload.size() >> 8U) & 0xFFU));
        frame.push_back(static_cast<std::uint8_t>(payload.size() & 0xFFU));
    } else {
        frame.push_back(127U);
        const std::uint64_t size = static_cast<std::uint64_t>(payload.size());
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.push_back(static_cast<std::uint8_t>((size >> shift) & 0xFFU));
        }
    }

    frame.insert(frame.end(), payload.begin(), payload.end());
    return frame;
}

} // namespace

WebSocketPublisher::WebSocketPublisher(WebSocketPublisherConfig config) : config_(std::move(config)) {}

WebSocketPublisher::~WebSocketPublisher() {
    stop();
}

bool WebSocketPublisher::configure_from_frame(const catcheye::input::Frame& frame) {
    if (frame.empty() || frame.width <= 0 || frame.height <= 0 || frame.stride <= 0) {
        std::cerr << "WebSocket publisher: invalid first frame\n";
        return false;
    }
    return true;
}

bool WebSocketPublisher::start() {
    if (running_) {
        return true;
    }

    server_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0) {
        return false;
    }

    ::setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &SOCKET_ENABLE, sizeof(SOCKET_ENABLE));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(config_.port));
    addr.sin_addr.s_addr = config_.bind_address == "0.0.0.0" ? INADDR_ANY : inet_addr(config_.bind_address.c_str());

    if (::bind(server_fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    if (::listen(server_fd_, config_.max_clients) != 0) {
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    running_ = true;
    accept_thread_ = std::thread(&WebSocketPublisher::accept_loop, this);
    return true;
}

void WebSocketPublisher::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    if (server_fd_ >= 0) {
        ::shutdown(server_fd_, SHUT_RDWR);
        ::close(server_fd_);
        server_fd_ = -1;
    }

    if (accept_thread_.joinable()) {
        accept_thread_.join();
    }

    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (const int fd : client_fds_) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }
    client_fds_.clear();
}

void WebSocketPublisher::publish(
    const catcheye::input::Frame& frame,
    const catcheye::protocol::FrameMessage& message,
    const PublishContext& context) {
    if (!running_ || frame.empty() || message.empty()) {
        return;
    }

    std::vector<std::uint8_t> jpeg_bytes;
    if (!encode_jpeg_payload(frame, jpeg_bytes)) {
        std::cerr << "WebSocket publisher: failed to encode JPEG payload at frame " << context.frame_index << '\n';
        return;
    }

    const std::string metadata = build_metadata_frame(frame, message, context, jpeg_bytes.size());
    const auto metadata_frame =
        websocket_frame(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(metadata.data()), metadata.size()), 0x1U);
    const auto binary_frame = websocket_frame(jpeg_bytes, 0x2U);

    std::lock_guard<std::mutex> lock(clients_mutex_);
    auto it = client_fds_.begin();
    while (it != client_fds_.end()) {
        const bool ok =
            send_all(*it, metadata_frame.data(), metadata_frame.size()) && send_all(*it, binary_frame.data(), binary_frame.size());
        if (!ok) {
            ::shutdown(*it, SHUT_RDWR);
            ::close(*it);
            it = client_fds_.erase(it);
            continue;
        }
        ++it;
    }
}

void WebSocketPublisher::accept_loop() {
    pollfd pfd{};
    pfd.fd = server_fd_;
    pfd.events = POLLIN;

    while (running_) {
        const int poll_result = ::poll(&pfd, 1, POLL_TIMEOUT_MS);
        if (poll_result <= 0) {
            continue;
        }

        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        const int client_fd = ::accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0) {
            continue;
        }

        if (!handshake_client(client_fd)) {
            ::close(client_fd);
            continue;
        }

        std::lock_guard<std::mutex> lock(clients_mutex_);
        if (static_cast<int>(client_fds_.size()) >= config_.max_clients) {
            ::close(client_fd);
            continue;
        }
        client_fds_.push_back(client_fd);
    }
}

bool WebSocketPublisher::handshake_client(int client_fd) {
    std::string request;
    request.reserve(MAX_HANDSHAKE_BYTES);
    std::array<char, 1024> buffer{};

    while (request.size() < MAX_HANDSHAKE_BYTES) {
        const ssize_t received = ::recv(client_fd, buffer.data(), buffer.size(), 0);
        if (received <= 0) {
            return false;
        }
        request.append(buffer.data(), static_cast<std::size_t>(received));
        if (request.find("\r\n\r\n") != std::string::npos) {
            break;
        }
    }

    const std::string client_key = find_header_value(request, "Sec-WebSocket-Key");
    if (client_key.empty()) {
        return false;
    }

    const std::string accept_key = websocket_accept_key(client_key);
    const std::string response = "HTTP/1.1 101 Switching Protocols\r\n"
                                 "Upgrade: websocket\r\n"
                                 "Connection: Upgrade\r\n"
                                 "Sec-WebSocket-Accept: " +
                                 accept_key + "\r\n\r\n";

    return send_all(client_fd, response.data(), response.size());
}

} // namespace catcheye::transport
