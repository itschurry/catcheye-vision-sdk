#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace catcheye::http {

struct HttpServerConfig {
    std::string bind_address = "0.0.0.0";
    int port = 8090;
};

struct HttpRequest {
    std::string method;
    std::string path;
    std::string body;
};

struct HttpResponse {
    int status_code = 200;
    std::string status_text = "OK";
    std::string body = "{}";
};

using RouteHandler = std::function<HttpResponse(const HttpRequest&)>;

std::string escape_json_string(std::string_view value);
std::string json_error_body(std::string_view message);
std::string json_error_body(std::string_view message, const std::vector<std::string>& details);

class HttpServer {
  public:
    explicit HttpServer(HttpServerConfig config);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    void add_route(std::string path, RouteHandler handler);
    void add_prefix_route(std::string prefix, RouteHandler handler);
    bool start();
    void stop();

  private:
    void accept_loop();
    void handle_client(int client_fd);
    bool send_response(int client_fd, const HttpResponse& response) const;

    HttpServerConfig config_;
    std::map<std::string, RouteHandler> routes_;
    std::vector<std::pair<std::string, RouteHandler>> prefix_routes_;
    int server_fd_ = -1;
    std::atomic<bool> running_ = false;
    std::thread accept_thread_;
};

} // namespace catcheye::http
