#pragma once

#include "../interface/udp.hpp"

#include "udp_reassembly.hpp"
#include "udp_packet.hpp"

#include <boost/asio.hpp>
#include <atomic>
#include <mutex>

namespace Utils::Net::Udp {

    namespace asio = boost::asio;
    using asio::ip::udp;

    class ServerImpl;

    class SessionImpl final :
        public Session,
        public std::enable_shared_from_this<SessionImpl>
    {
    public:
        SessionImpl(ServerImpl* server,
                   std::uint64_t sessionId,
                   const udp::endpoint& endpoint,
                   const Logging::Logger::Shared& logger,
                   const ReassemblyConfig& reassemblyConfig);

        [[nodiscard]] std::uint64_t SessionId() const override;

        [[nodiscard]] std::string RemoteAddress() override;
        [[nodiscard]] std::uint16_t RemotePort() override;

        Logging::Logger::Shared& Log() override;

        void Close() override;

        void Send(const std::vector<std::uint8_t>& data) override;
        void Send(std::string_view text) override;
        void Send(const JsonValue& json) override;

        void OnDatagram(const ParsedPacket& packet);
        void TickCleanup(std::uint32_t nowMs, std::uint32_t sessionTimeoutMs);

        void UpdateEndpoint(const udp::endpoint& endpoint);

        bool IsClosed() const;
        bool IsTimedOut() const;

    private:
        void SendInternal(std::span<const std::uint8_t> payload);
        void EmitCompletedMessages();

    private:
        ServerImpl* server_ { nullptr };
        std::uint64_t sessionId_ { 0 };

        std::mutex endpointMutex_;
        udp::endpoint endpoint_;

        Logging::Logger::Shared logger_;

        ReassemblyBuffer reassembly_;
        std::atomic<std::uint32_t> lastSeenMs_ { 0 };
        std::atomic<bool> closed_ { false };

        std::atomic<std::uint32_t> nextMessageId_ { 1 };
    };

} // namespace Utils::Net::Udp