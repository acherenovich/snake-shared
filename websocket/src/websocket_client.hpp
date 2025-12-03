#pragma once

#include "../interface/websocket.hpp"

#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <thread>      // std::jthread
#include <memory>      // std::enable_shared_from_this
#include <atomic>

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/json.hpp>

#include <logging.hpp>

namespace Utils::Net::WebSocket {

    namespace asio      = boost::asio;
    namespace beast     = boost::beast;
    namespace websocket = beast::websocket;
    namespace json      = boost::json;
    using boost::system::error_code;

    namespace Logging = Utils::Logging;

    enum class ClientEventType
    {
        Connected,
        Disconnected,
        Bytes,
        Text,
        Json
    };

    struct ClientEvent
    {
        ClientEventType type{ ClientEventType::Disconnected };

        std::vector<std::uint8_t> bytes;
        std::string text;
        json::value jsonValue;
    };

    class ClientImpl final :
        public Client,
        public std::enable_shared_from_this<ClientImpl>
    {
    public:
        ClientImpl(const ClientConfig& config,
                   const ClientListener::Shared& listener,
                   const Logging::Logger::Shared& logger);

        ~ClientImpl() override;

        // Вызывает фабрика после создания shared_ptr
        void Initialise();

        void ProcessTick() override;

        void Send(const std::vector<std::uint8_t>& data) override;
        void Send(std::string_view text) override;
        void Send(const json::value& jsonValue) override;

        void Close() override;

    private:
        void StartThreads();
        void StopThreads();

        void Resolve();
        void OnResolve(error_code ec,
                       const asio::ip::tcp::resolver::results_type& results);

        void OnConnect(error_code ec,
                       const asio::ip::tcp::endpoint& endpoint);

        void OnSslHandshake(error_code ec);
        void OnWsHandshake(error_code ec);

        void DoRead();
        void OnRead(error_code ec, std::size_t bytesTransferred);

        void EnqueueSendInternal(const std::string& payload);
        void DoWrite();
        void OnWrite(error_code ec, std::size_t bytesTransferred);

        void EnqueueEvent(const ClientEvent& event);

        void ScheduleReconnect();
        void DoReconnect(error_code ec);

        void DoAsyncConnect(websocket::stream<beast::tcp_stream>& ws,
                    const asio::ip::tcp::resolver::results_type& results);

        void DoAsyncConnect(websocket::stream<asio::ssl::stream<asio::ip::tcp::socket>>& ws,
                            const asio::ip::tcp::resolver::results_type& results);

    private:
        ClientConfig config_;
        ClientListener::Shared listener_;
        Logging::Logger::Shared logger_;

        asio::io_context ioContext_;
        asio::ip::tcp::resolver resolver_;

        asio::executor_work_guard<asio::io_context::executor_type> workGuard_;

        // plain ws
        websocket::stream<beast::tcp_stream> wsPlain_;

        // tls
        asio::ssl::context sslContext_;
        std::unique_ptr<websocket::stream<asio::ssl::stream<asio::ip::tcp::socket>>> wsTls_;

        asio::steady_timer reconnectTimer_;

        std::vector<std::jthread> threads_;
        std::atomic<bool> stopRequested_{ false };
        std::atomic<bool> manuallyClosed_{ false };

        beast::flat_buffer readBuffer_;

        std::mutex sendMutex_;
        std::deque<std::string> sendQueue_;
        bool writeInProgress_{ false };

        std::mutex eventsMutex_;
        std::deque<ClientEvent> events_;
    };

} // namespace Utils::Net::WebSocket