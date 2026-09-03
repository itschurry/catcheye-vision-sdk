#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <stdexcept>
#include "catcheye/http/http_server.hpp"

namespace {
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int available_port()
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    require(fd >= 0, "socket");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    require(::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "bind");
    socklen_t length = sizeof(address);
    require(::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) == 0, "getsockname");
    ::close(fd);
    return ntohs(address.sin_port);
}
std::string request(int port, const std::string& path, const std::string& headers = "")
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    require(fd >= 0, "socket");
    timeval timeout{3, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    sockaddr_in address{};
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(static_cast<unsigned short>(port));
    require(::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "connect");
    const auto data = "GET " + path + " HTTP/1.1\r\nHost: localhost\r\nContent-Length: 0\r\n" + headers + "\r\n";
    require(::send(fd, data.data(), data.size(), MSG_NOSIGNAL) == static_cast<ssize_t>(data.size()), "send");
    std::string response;
    char buffer[4096];
    ssize_t count;
    while ((count = ::recv(fd, buffer, sizeof(buffer), 0)) > 0) response.append(buffer, static_cast<std::size_t>(count));
    ::close(fd);
    return response;
}
}
int main()
{
    try {
        const int port = available_port();
        catcheye::http::HttpServer server({.bind_address = "127.0.0.1", .port = port});
        server.add_route("/fields", [](const catcheye::http::HttpRequest& request) {
            if (!request.headers.contains("authorization")) return catcheye::http::HttpResponse{401, "Unauthorized"};
            return catcheye::http::HttpResponse{200, "OK", request.headers.at("authorization") + '|' +
                request.query.at("limit") + '|' + request.query.at("cursor")};
        });
        require(server.start(), "start");
        const std::string path = "/fields?limit=1&cursor=refrev_012-ab";
        require(request(port, path, "AuThOrIzAtIoN: Bearer test\r\n").find("Bearer test|1|refrev_012-ab") != std::string::npos, "header/query propagation");
        require(request(port, path).starts_with("HTTP/1.1 401"), "missing header");
        for (const auto& headers : {"Authorization: one\r\nauthorization: two\r\n", "Content-Length: 0\r\n",
             "Transfer-Encoding: chunked\r\n", "Bad Header: value\r\n", "Authorization: bad\x01token\r\n"}) {
            require(request(port, path, headers).starts_with("HTTP/1.1 400"), "invalid header accepted");
        }
        for (const auto& query : {"limit=1&limit=2", "limit=", "=1", "cursor=%2fetc", "limit=1&", "limit=1&&cursor=x"}) {
            require(request(port, std::string("/fields?") + query).starts_with("HTTP/1.1 400"), "invalid query accepted");
        }
        server.stop();
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
