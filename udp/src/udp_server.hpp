#pragma once

#include "../interface/udp.hpp"

#include "udp_reassembly.hpp"
#include "udp_packet.hpp"

#include <boost/asio.hpp>

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace Utils::Net::Udp {

    namespace asio = boost::asio;
    using boost::system::error_code;
    using asio::ip::udp;

    class ServerImpl;
    class SessionImpl;

    enum class ServerEventType
    {
        Connected,
        Disconnected,
        Bytes,
        Text,
        Json
    };

    struct ServerEvent
    {
        ServerEventType type { ServerEventType::Disconnected };
        std::shared_ptr<Session> session;

        std::vector<std::uint8_t> bytes;
        std::string text;
        JsonValue jsonValue;
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

        void EnqueueEvent(const ServerEvent& event);

        // send raw datagram to endpoint
        void SendDatagram(const udp::endpoint& endpoint, std::span<const std::uint8_t> data);

        // access config
        const ServerConfig& Config() const;

        // session management
        std::shared_ptr<SessionImpl> GetSession(std::uint64_t sessionId);
        std::shared_ptr<SessionImpl> CreateSession(const udp::endpoint& endpoint);
        void CloseSession(std::uint64_t sessionId);

        void UpdateSessionEndpoint(std::uint64_t sessionId, const udp::endpoint& endpoint);

    private:
        void StartThreads();
        void StopThreads();

        void StartReceive();
        void OnReceive(error_code ec, std::size_t bytes);

        void HandleHandshakeHello(const ParsedPacket& packet, const udp::endpoint& sender);
        void HandleHandshakeWelcome(const ParsedPacket& packet, const udp::endpoint& sender);

        void HandleDataFragment(const ParsedPacket& packet, const udp::endpoint& sender);

        void TickSessionsCleanup();

    private:
        ServerConfig config_;
        Listener::Shared listener_;
        Logging::Logger::Shared logger_;

        asio::io_context ioContext_;
        udp::socket socket_;
        udp::endpoint remoteEndpoint_;

        std::vector<std::jthread> threads_;
        std::atomic<bool> stopRequested_ { false };

        std::array<std::uint8_t, 2048> rxBuffer_ {};

        // sessions
        std::mutex sessionsMutex_;
        std::unordered_map<std::uint64_t, std::shared_ptr<SessionImpl>> sessions_;

        // events to main-thread
        std::mutex eventsMutex_;
        std::deque<ServerEvent> events_;

        asio::steady_timer cleanupTimer_;

        std::atomic<std::uint64_t> nextSessionId_ { 1 };
    };

} // namespace Utils::Net::Udp