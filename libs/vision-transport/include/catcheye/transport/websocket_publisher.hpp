#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "catcheye/transport/result_publisher.hpp"

namespace catcheye::transport {

struct WebSocketPublisherConfig {
    std::string bind_address = "0.0.0.0";
    int port = 8080;
    int max_clients = 4;
};

class WebSocketPublisher final : public ResultPublisher {
   public:
    explicit WebSocketPublisher(WebSocketPublisherConfig config = {});
    ~WebSocketPublisher() override;

    WebSocketPublisher(const WebSocketPublisher&) = delete;
    WebSocketPublisher& operator=(const WebSocketPublisher&) = delete;

    bool start() override;
    void stop() override;
    void publish(const catcheye::protocol::FrameMessage& message, const PublishContext& context) override;

   private:
    void accept_loop();
    bool handshake_client(int client_fd);

    WebSocketPublisherConfig config_;
    int server_fd_ {-1};
    std::atomic<bool> running_ {false};
    std::thread accept_thread_;

    std::mutex clients_mutex_;
    std::vector<int> client_fds_;
};

} // namespace catcheye::transport
