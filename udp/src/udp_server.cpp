#include "udp_server.hpp"
#include "udp_session.hpp"

#include <algorithm>
#include <format>

namespace Utils::Net::Udp {

    ServerImpl::ServerImpl(const ServerConfig& config,
                           const Listener::Shared& listener,
                           const Logging::Logger::Shared& logger)
        : config_(config)
        , listener_(listener)
        , ioContext_()
        , socket_(ioContext_)
        , cleanupTimer_(ioContext_)
    {
        if (logger)
        {
            logger_ = logger->CreateChild("UDP-SERVER");
        }
        else
        {
            logger_ = Utils::Log()->CreateChild("UDP-SERVER");
        }

        error_code ec;

        const udp::endpoint endpoint(asio::ip::make_address(config_.address, ec), config_.port);
        if (ec)
        {
            logger_->Fatal("Invalid address {} -> {}", config_.address, ec.message());
        }

        socket_.open(endpoint.protocol(), ec);
        if (ec)
        {
            logger_->Fatal("Socket open failed -> {}", ec.message());
        }

        socket_.set_option(asio::socket_base::reuse_address(true), ec);
        if (ec)
        {
            logger_->Warning("Set reuse_address failed -> {}", ec.message());
        }

        socket_.bind(endpoint, ec);
        if (ec)
        {
            logger_->Fatal("Bind {}:{} failed -> {}", config_.address, config_.port, ec.message());
        }

        logger_->Debug("UDP listening on {}:{}", config_.address, config_.port);

        StartReceive();
        StartThreads();

        cleanupTimer_.expires_after(std::chrono::milliseconds(250));
        cleanupTimer_.async_wait([this](error_code timerEc)
        {
            if (!timerEc)
            {
                TickSessionsCleanup();
            }
        });
    }

    ServerImpl::~ServerImpl()
    {
        stopRequested_.store(true);

        error_code ec;
        socket_.close(ec);

        ioContext_.stop();
        StopThreads();

        if (logger_)
        {
            logger_->Debug("UDP server stopped");
        }
    }

    const ServerConfig& ServerImpl::Config() const
    {
        return config_;
    }

    void ServerImpl::StartThreads()
    {
        const std::size_t requested = config_.ioThreads != 0
            ? config_.ioThreads
            : std::max<std::size_t>(1, std::thread::hardware_concurrency());

        threads_.reserve(requested);

        for (std::size_t i = 0; i < requested; ++i)
        {
            threads_.emplace_back([this]()
            {
                ioContext_.run();
            });
        }

        logger_->Debug("Started {} IO threads", threads_.size());
    }

    void ServerImpl::StopThreads()
    {
        threads_.clear();
    }

    void ServerImpl::StartReceive()
    {
        socket_.async_receive_from(
            asio::buffer(rxBuffer_),
            remoteEndpoint_,
            [this](const error_code ec, const std::size_t bytes)
            {
                OnReceive(ec, bytes);
            });
    }

    void ServerImpl::OnReceive(const error_code ec, const std::size_t bytes)
    {
        if (ec)
        {
            if (logger_)
            {
                logger_->Warning("Receive error -> {}", ec.message());
            }

            if (!stopRequested_.load())
            {
                StartReceive();
            }
            return;
        }

        const auto span = std::span<const std::uint8_t>(rxBuffer_.data(), bytes);
        ParsedPacket packet = ParsePacket(span);

        if (!packet.ok)
        {
            if (logger_)
            {
                logger_->Warning("Dropped datagram: parse failed reason={} bytes={} from {}:{}",
                                 PacketRejectReasonToString(packet.reject),
                                 bytes,
                                 remoteEndpoint_.address().to_string(),
                                 remoteEndpoint_.port());
            }

            StartReceive();
            return;
        }

        if (packet.header.type == PacketType::HandshakeHello)
        {
            HandleHandshakeHello(packet, remoteEndpoint_);
        }
        else if (packet.header.type == PacketType::DataFragment)
        {
            HandleDataFragment(packet, remoteEndpoint_);
        }

        StartReceive();
    }

    std::shared_ptr<SessionImpl> ServerImpl::GetSession(const std::uint64_t sessionId)
    {
        std::lock_guard lock(sessionsMutex_);
        auto it = sessions_.find(sessionId);
        if (it == sessions_.end())
        {
            return nullptr;
        }
        return it->second;
    }

    std::shared_ptr<SessionImpl> ServerImpl::CreateSession(const udp::endpoint& endpoint)
    {
        const std::uint64_t newId = nextSessionId_.fetch_add(1);

        auto sessionLogger = logger_->CreateChild(std::format("SID-{}", newId));

        ReassemblyConfig reassemblyCfg;
        reassemblyCfg.maxMessageSize = config_.maxMessageSize;
        reassemblyCfg.timeoutMs = config_.reassemblyTimeoutMs;

        auto session = std::make_shared<SessionImpl>(this,
                                                     newId,
                                                     endpoint,
                                                     sessionLogger,
                                                     reassemblyCfg);

        {
            std::lock_guard lock(sessionsMutex_);
            sessions_[newId] = session;
        }

        ServerEvent event;
        event.type = ServerEventType::Connected;
        event.session = std::static_pointer_cast<Session>(session);
        EnqueueEvent(event);

        return session;
    }

    void ServerImpl::UpdateSessionEndpoint(const std::uint64_t sessionId, const udp::endpoint& endpoint)
    {
        auto session = GetSession(sessionId);
        if (session)
        {
            session->UpdateEndpoint(endpoint);
        }
    }

    void ServerImpl::CloseSession(const std::uint64_t sessionId)
    {
        std::shared_ptr<SessionImpl> session;

        {
            std::lock_guard lock(sessionsMutex_);
            auto it = sessions_.find(sessionId);
            if (it == sessions_.end())
            {
                return;
            }
            session = it->second;
            sessions_.erase(it);
        }

        if (session)
        {
            ServerEvent event;
            event.type = ServerEventType::Disconnected;
            event.session = std::static_pointer_cast<Session>(session);
            EnqueueEvent(event);
        }
    }

    void ServerImpl::HandleHandshakeHello(const ParsedPacket& /*packet*/, const udp::endpoint& sender)
    {
        // Always create a new session on hello
        auto session = CreateSession(sender);

        PacketHeader header;
        header.type = PacketType::HandshakeWelcome;
        header.sessionId = session->SessionId();

        const auto response = BuildPacket(header, {});
        SendDatagram(sender, response);

        if (logger_)
        {
            logger_->Debug("Handshake welcome sent to {}:{} -> sessionId={}",
                           sender.address().to_string(),
                           sender.port(),
                           session->SessionId());
        }
    }

    void ServerImpl::HandleDataFragment(const ParsedPacket& packet, const udp::endpoint& sender)
    {
        const std::uint64_t sid = packet.header.sessionId;

        auto session = GetSession(sid);
        if (!session)
        {
            if (logger_)
            {
                logger_->Warning("Dropped fragment: unknown sessionId={} from {}:{} mid={} idx={}/{}",
                                 sid,
                                 sender.address().to_string(),
                                 sender.port(),
                                 packet.header.messageId,
                                 packet.header.fragmentIndex,
                                 packet.header.fragmentCount);
            }
            return;
        }

        // Endpoint can change (NAT / reconnect)
        session->UpdateEndpoint(sender);

        session->OnDatagram(packet);
    }

    void ServerImpl::TickSessionsCleanup()
    {
        if (stopRequested_.load())
        {
            return;
        }

        const std::uint32_t now = NowMs();

        std::vector<std::shared_ptr<SessionImpl>> closedSessions;

        {
            std::lock_guard lock(sessionsMutex_);

            for (auto it = sessions_.begin(); it != sessions_.end(); )
            {
                auto& session = it->second;
                session->TickCleanup(now, config_.sessionTimeoutMs);

                if (session->IsTimedOut())
                {
                    closedSessions.push_back(session);
                    it = sessions_.erase(it);
                    continue;
                }

                ++it;
            }
        }

        for (const auto& session : closedSessions)
        {
            if (logger_)
            {
                logger_->Debug("Session {} timed out", session->SessionId());
            }

            ServerEvent event;
            event.type = ServerEventType::Disconnected;
            event.session = std::static_pointer_cast<Session>(session);
            EnqueueEvent(event);
        }

        cleanupTimer_.expires_after(std::chrono::milliseconds(250));
        cleanupTimer_.async_wait([this](error_code timerEc)
        {
            if (!timerEc)
            {
                TickSessionsCleanup();
            }
        });
    }

    void ServerImpl::SendDatagram(const udp::endpoint& endpoint, const std::span<const std::uint8_t> data)
    {
        socket_.async_send_to(
            asio::buffer(data.data(), data.size()),
            endpoint,
            [](const error_code /*ec*/, const std::size_t /*bytes*/) {});
    }

    void ServerImpl::EnqueueEvent(const ServerEvent& event)
    {
        std::lock_guard lock(eventsMutex_);
        events_.push_back(event);
    }

    void ServerImpl::ProcessTick()
    {
        std::deque<ServerEvent> local;

        {
            std::lock_guard lock(eventsMutex_);
            local.swap(events_);
        }

        if (!listener_)
        {
            return;
        }

        for (auto& e : local)
        {
            switch (e.type)
            {
                case ServerEventType::Connected:
                    listener_->OnSessionConnected(e.session);
                    break;

                case ServerEventType::Disconnected:
                    listener_->OnSessionDisconnected(e.session);
                    break;

                case ServerEventType::Bytes:
                    listener_->OnMessage(e.session, e.bytes);
                    break;

                case ServerEventType::Text:
                    listener_->OnMessage(e.session, e.text);
                    break;

                case ServerEventType::Json:
                    listener_->OnMessage(e.session, e.jsonValue);
                    break;
            }
        }
    }

    Logging::Logger::Shared& ServerImpl::Log()
    {
        return logger_;
    }

    void ServerImpl::SetupListener(const Listener::Shared& listener)
    {
        listener_ = listener;
    }

    Mode ServerImpl::GetMode() const
    {
        return config_.mode;
    }

    Server::Shared Server::Create(const ServerConfig& config,
                                  const Listener::Shared& listener,
                                  const Logging::Logger::Shared& logger)
    {
        return std::make_shared<ServerImpl>(config, listener, logger);
    }

} // namespace Utils::Net::Udp