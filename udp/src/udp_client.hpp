#pragma once

#include "../interface/udp.hpp"

#include "udp_reassembly.hpp"
#include "udp_packet.hpp"

#include <boost/asio.hpp>
#include <boost/asio/steady_timer.hpp>

#include <atomic>
#include <deque>
#include <mutex>
#include <thread>

namespace Utils::Net::Udp {

    namespace asio = boost::asio;
    using boost::system::error_code;
    using asio::ip::udp;

    enum class ClientEventType
    {
        Connected,
        Disconnected,
        ConnectionError,
        Bytes,
        Text,
        Json
    };

    struct ClientEvent
    {
        ClientEventType type { ClientEventType::Disconnected };
        std::vector<std::uint8_t> bytes;
        std::string text;
        JsonValue jsonValue;
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

        void Initialise();

        void ProcessTick() override;

        Logging::Logger::Shared& Log() override;

        void Send(const std::vector<std::uint8_t>& data) override;
        void Send(std::string_view text) override;
        void Send(const JsonValue& json) override;

        void Close() override;

    private:
        void StartThreads();
        void StopThreads();

        void Resolve();
        void OnResolve(error_code ec, const udp::resolver::results_type& results);

        void OpenSocket();
        void StartReceive();
        void OnReceive(error_code ec, std::size_t bytes);

        void StartHandshake();
        void SendHandshakeHello();
        void OnHandshakeTimeout(error_code ec);

        void OnWelcome(const ParsedPacket& packet);

        void EnqueueEvent(const ClientEvent& event);

        void ScheduleReconnect();
        void DoReconnect(error_code ec);

        void SendInternal(std::span<const std::uint8_t> payload);
        void EmitCompletedMessages();

    private:
        ClientConfig config_;
        ClientListener::Shared listener_;
        Logging::Logger::Shared logger_;

        asio::io_context ioContext_;
        udp::resolver resolver_;
        udp::socket socket_;
        udp::endpoint serverEndpoint_;
        udp::endpoint remoteEndpoint_;

        asio::executor_work_guard<asio::io_context::executor_type> workGuard_;

        std::vector<std::jthread> threads_;
        std::atomic<bool> stopRequested_ { false };
        std::atomic<bool> manuallyClosed_ { false };

        std::array<std::uint8_t, 2048> rxBuffer_ {};

        // handshake
        asio::steady_timer handshakeTimer_;
        std::atomic<bool> handshakeComplete_ { false };
        std::atomic<std::uint64_t> sessionId_ { 0 };
        std::uint32_t handshakeStartMs_ { 0 };

        asio::steady_timer reconnectTimer_;

        // send
        std::mutex sendMutex_;
        std::deque<std::vector<std::uint8_t>> sendQueue_;
        bool writeInProgress_ { false };

        // events
        std::mutex eventsMutex_;
        std::deque<ClientEvent> events_;

        ReassemblyBuffer reassembly_;
        std::atomic<std::uint32_t> nextMessageId_ { 1 };
    };

} // namespace Utils::Net::Udp