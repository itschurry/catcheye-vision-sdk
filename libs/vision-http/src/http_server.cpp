#include "catcheye/http/http_server.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <span>
#include <string_view>
#include <utility>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace catcheye::http {
namespace {

constexpr int SOCKET_ENABLE = 1;
constexpr int POLL_TIMEOUT_MS = 200;
constexpr std::size_t MAX_REQUEST_BYTES = 1024 * 1024;

bool send_all(int sock_fd, const void* data, std::size_t size)
{
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

std::string trim(std::string value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.pop_back();
    }
    return value;
}

std::string header_value(std::string_view request, std::string_view header_name)
{
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
        std::string prefix = line.substr(0, needle.size());
        std::string needle_lower = needle;
        std::transform(prefix.begin(), prefix.end(), prefix.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        std::transform(needle_lower.begin(), needle_lower.end(), needle_lower.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        if (prefix == needle_lower) {
            return trim(line.substr(needle.size()));
        }
    }
    return {};
}

bool parse_request_line(std::string_view request, std::string& method, std::string& path)
{
    const std::size_t line_end = request.find("\r\n");
    if (line_end == std::string_view::npos) {
        return false;
    }
    std::string version;
    std::istringstream iss{std::string(request.substr(0, line_end))};
    return static_cast<bool>(iss >> method >> path >> version);
}

bool read_http_request(int client_fd, std::string& request, std::string& body)
{
    request.clear();
    body.clear();

    std::array<char, 4096> buffer{};
    std::size_t header_end = std::string::npos;
    while (request.size() < MAX_REQUEST_BYTES) {
        const ssize_t received = ::recv(client_fd, buffer.data(), buffer.size(), 0);
        if (received <= 0) {
            return false;
        }
        request.append(buffer.data(), static_cast<std::size_t>(received));
        header_end = request.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            break;
        }
    }

    if (header_end == std::string::npos) {
        return false;
    }

    const std::string headers = request.substr(0, header_end + 4U);
    std::size_t content_length = 0;
    const std::string content_length_text = header_value(headers, "Content-Length");
    if (!content_length_text.empty()) {
        try {
            content_length = static_cast<std::size_t>(std::stoul(content_length_text));
        } catch (...) {
            return false;
        }
    }

    body = request.substr(header_end + 4U);
    while (body.size() < content_length && request.size() < MAX_REQUEST_BYTES) {
        const ssize_t received = ::recv(client_fd, buffer.data(), buffer.size(), 0);
        if (received <= 0) {
            return false;
        }
        request.append(buffer.data(), static_cast<std::size_t>(received));
        body.append(buffer.data(), static_cast<std::size_t>(received));
    }

    if (body.size() > content_length) {
        body.resize(content_length);
    }
    return body.size() == content_length;
}

} // namespace

std::string escape_json_string(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '"':
            escaped += "\\\"";
            break;
        case '\\':
            escaped += "\\\\";
            break;
        case '\n':
            escaped += "\\n";
            break;
        case '\r':
            escaped += "\\r";
            break;
        case '\t':
            escaped += "\\t";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

std::string json_error_body(std::string_view message)
{
    return "{\"error\":\"" + escape_json_string(message) + "\"}";
}

std::string json_error_body(std::string_view message, const std::vector<std::string>& details)
{
    std::ostringstream oss;
    oss << "{\"error\":\"" << escape_json_string(message) << "\"";
    if (!details.empty()) {
        oss << ",\"details\":[";
        for (std::size_t i = 0; i < details.size(); ++i) {
            if (i > 0) {
                oss << ',';
            }
            oss << "\"" << escape_json_string(details[i]) << "\"";
        }
        oss << "]";
    }
    oss << "}";
    return oss.str();
}

HttpServer::HttpServer(HttpServerConfig config)
    : config_(std::move(config)) {}

HttpServer::~HttpServer()
{
    stop();
}

void HttpServer::add_route(std::string path, RouteHandler handler)
{
    routes_[std::move(path)] = std::move(handler);
}

void HttpServer::add_prefix_route(std::string prefix, RouteHandler handler)
{
    prefix_routes_.emplace_back(std::move(prefix), std::move(handler));
}

bool HttpServer::start()
{
    if (running_) {
        return true;
    }
    if (config_.port <= 0) {
        return false;
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

    if (::listen(server_fd_, 8) != 0) {
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    running_ = true;
    accept_thread_ = std::thread(&HttpServer::accept_loop, this);
    return true;
}

void HttpServer::stop()
{
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
}

void HttpServer::accept_loop()
{
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

        handle_client(client_fd);
        ::shutdown(client_fd, SHUT_RDWR);
        ::close(client_fd);
    }
}

void HttpServer::handle_client(int client_fd)
{
    std::string raw_request;
    std::string body;
    if (!read_http_request(client_fd, raw_request, body)) {
        send_response(client_fd, HttpResponse{400, "Bad Request", json_error_body("failed to read HTTP request")});
        return;
    }

    HttpRequest request;
    request.body = std::move(body);
    if (!parse_request_line(raw_request, request.method, request.path)) {
        send_response(client_fd, HttpResponse{400, "Bad Request", json_error_body("invalid HTTP request line")});
        return;
    }

    const auto route = routes_.find(request.path);
    if (route != routes_.end()) {
        send_response(client_fd, route->second(request));
        return;
    }

    for (const auto& [prefix, handler] : prefix_routes_) {
        if (request.path.rfind(prefix, 0) == 0) {
            send_response(client_fd, handler(request));
            return;
        }
    }

    send_response(client_fd, HttpResponse{404, "Not Found", json_error_body("unknown endpoint")});
}

bool HttpServer::send_response(int client_fd, const HttpResponse& response) const
{
    std::ostringstream oss;
    oss << "HTTP/1.1 " << response.status_code << ' ' << response.status_text << "\r\n"
        << "Content-Type: application/json\r\n"
        << "Content-Length: " << response.body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << response.body;
    const std::string payload = oss.str();
    return send_all(client_fd, payload.data(), payload.size());
}

} // namespace catcheye::http
