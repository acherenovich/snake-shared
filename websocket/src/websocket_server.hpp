#pragma once

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>
#include <memory>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>

#include <websocket.hpp>
#include <logging.hpp>

namespace Utils::Net::Websocket {
    namespace asio  = boost::asio;
    namespace beast = boost::beast;
    using boost::system::error_code;
    namespace ssl   = asio::ssl;

    class BasicSessionBase;

    enum class EventType
    {
        Connected,
        Disconnected,
        Bytes,
        Text,
        Json
    };

    struct Event
    {
        EventType type;
        std::shared_ptr<Session> session;

        std::vector<std::uint8_t> bytes;
        std::string text;
        boost::json::value jsonValue;
    };

    class ServerImpl final :
        public Server,
        public std::enable_shared_from_this<ServerImpl>
    {
    public:
        ServerImpl(const ServerConfig& config,
                   const Listener::Shared& listener,
                   const Logging::Logger::Shared& logger);

        ~ServerImpl() override;

        void ProcessTick() override;

        Logging::Logger::Shared& Log() override;

        void SetupListener(const Listener::Shared& listener) override;

        [[nodiscard]] Mode GetMode() const override;

        void EnqueueEvent(const Event& event);
        void RemoveSession(const std::shared_ptr<Session>& session);

        ssl::context& SslContext();

    private:
        void AcceptLoop();
        void OnAccept(const error_code ec, asio::ip::tcp::socket socket);

        void StartThreads();
        void StopThreads();

    private:
        ServerConfig config_;
        Listener::Shared listener_;
        Logging::Logger::Shared logger_;

        asio::io_context ioContext_;
        asio::ip::tcp::acceptor acceptor_;
        ssl::context sslContext_;

        std::vector<std::jthread> threads_;
        std::atomic<bool> stopRequested_ { false };

        std::mutex sessionsMutex_;
        std::vector<std::shared_ptr<Session>> sessions_;

        std::mutex eventsMutex_;
        std::deque<Event> events_;
    };

} // namespace Utils::Net::Websocket