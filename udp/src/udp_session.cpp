#include "udp_session.hpp"
#include "udp_server.hpp"

#include <format>
#include <boost/json.hpp>

namespace Utils::Net::Udp {

    namespace json = boost::json;

    SessionImpl::SessionImpl(ServerImpl* const server,
                             const std::uint64_t sessionId,
                             const udp::endpoint& endpoint,
                             const Logging::Logger::Shared& logger,
                             const ReassemblyConfig& reassemblyConfig)
        : server_(server)
        , sessionId_(sessionId)
        , endpoint_(endpoint)
        , logger_(logger)
        , reassembly_(reassemblyConfig)
    {
        lastSeenMs_.store(NowMs());
    }

    std::uint64_t SessionImpl::SessionId() const
    {
        return sessionId_;
    }

    std::string SessionImpl::RemoteAddress()
    {
        std::lock_guard lock(endpointMutex_);
        return endpoint_.address().to_string();
    }

    std::uint16_t SessionImpl::RemotePort()
    {
        std::lock_guard lock(endpointMutex_);
        return endpoint_.port();
    }

    Logging::Logger::Shared& SessionImpl::Log()
    {
        return logger_;
    }

    void SessionImpl::UpdateEndpoint(const udp::endpoint& endpoint)
    {
        std::lock_guard lock(endpointMutex_);
        endpoint_ = endpoint;
    }

    void SessionImpl::Close()
    {
        closed_.store(true);
    }

    bool SessionImpl::IsClosed() const
    {
        return closed_.load();
    }

    void SessionImpl::TickCleanup(const std::uint32_t nowMs, const std::uint32_t sessionTimeoutMs)
    {
        reassembly_.TickCleanup();

        const std::uint32_t last = lastSeenMs_.load();
        if (nowMs - last > sessionTimeoutMs)
        {
            closed_.store(true);
        }
    }

    bool SessionImpl::IsTimedOut() const
    {
        return closed_.load();
    }

    void SessionImpl::SendInternal(const std::span<const std::uint8_t> payload)
    {
        if (payload.size() > server_->Config().maxMessageSize)
        {
            if (logger_)
            {
                logger_->Warning("Send payload too large: {} bytes (max={})",
                                 payload.size(),
                                 server_->Config().maxMessageSize);
            }
            return;
        }

        const std::size_t mtu = server_->Config().mtuPayload;
        const std::size_t maxPayloadPerPacket =
            mtu > HeaderSize ? (mtu - HeaderSize) : 0;

        if (maxPayloadPerPacket == 0)
        {
            return;
        }

        const std::uint32_t messageId = nextMessageId_.fetch_add(1);

        const std::size_t total = payload.size();
        const std::uint16_t fragmentCount = static_cast<std::uint16_t>(
            (total + maxPayloadPerPacket - 1) / maxPayloadPerPacket);

        for (std::uint16_t index = 0; index < fragmentCount; ++index)
        {
            const std::size_t offset = static_cast<std::size_t>(index) * maxPayloadPerPacket;
            const std::size_t size = std::min<std::size_t>(maxPayloadPerPacket, total - offset);

            PacketHeader header;
            header.type = PacketType::DataFragment;
            header.sessionId = sessionId_;
            header.messageId = messageId;
            header.fragmentIndex = index;
            header.fragmentCount = fragmentCount;
            header.totalSize = static_cast<std::uint32_t>(total);

            const auto packet = BuildPacket(header, payload.subspan(offset, size));

            udp::endpoint endpointCopy;
            {
                std::lock_guard lock(endpointMutex_);
                endpointCopy = endpoint_;
            }

            server_->SendDatagram(endpointCopy, packet);
        }
    }

    void SessionImpl::Send(const std::vector<std::uint8_t>& data)
    {
        SendInternal(data);
    }

    void SessionImpl::Send(const std::string_view text)
    {
        SendInternal(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
    }

    void SessionImpl::Send(const JsonValue& jsonValue)
    {
        const std::string serialized = json::serialize(jsonValue);
        Send(serialized);
    }

    void SessionImpl::OnDatagram(const ParsedPacket& packet)
    {
        lastSeenMs_.store(NowMs());

        if (packet.header.type != PacketType::DataFragment)
        {
            return;
        }

        const bool done = reassembly_.AddFragment(
            packet.header.messageId,
            packet.header.fragmentIndex,
            packet.header.fragmentCount,
            packet.header.totalSize,
            packet.payload.data(),
            packet.payload.size()
        );

        if (done)
        {
            EmitCompletedMessages();
        }
    }

    void SessionImpl::EmitCompletedMessages()
    {
        while (reassembly_.HasCompleted())
        {
            CompletedMessage completed = reassembly_.PopCompleted();

            const Mode mode = server_->GetMode();

            if (mode == Mode::Bytes)
            {
                ServerEvent event;
                event.type = ServerEventType::Bytes;
                event.session = std::static_pointer_cast<Session>(shared_from_this());
                event.bytes = std::move(completed.data);
                server_->EnqueueEvent(event);
            }
            else if (mode == Mode::Text)
            {
                ServerEvent event;
                event.type = ServerEventType::Text;
                event.session = std::static_pointer_cast<Session>(shared_from_this());
                event.text.assign(reinterpret_cast<const char*>(completed.data.data()),
                                  completed.data.size());
                server_->EnqueueEvent(event);
            }
            else
            {
                try
                {
                    const std::string text(reinterpret_cast<const char*>(completed.data.data()),
                                           completed.data.size());

                    ServerEvent event;
                    event.type = ServerEventType::Json;
                    event.session = std::static_pointer_cast<Session>(shared_from_this());
                    event.jsonValue = json::parse(text);
                    server_->EnqueueEvent(event);
                }
                catch (const std::exception& ex)
                {
                    if (logger_)
                    {
                        logger_->Warning("JSON parse error in session {} -> {}",
                                         sessionId_,
                                         ex.what());
                    }
                }
            }
        }
    }

} // namespace Utils::Net::Udp