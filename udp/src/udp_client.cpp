#include "udp_client.hpp"

#include <format>
#include <boost/json.hpp>

namespace Utils::Net::Udp {

    namespace json = boost::json;

    ClientImpl::ClientImpl(const ClientConfig& config,
                           const ClientListener::Shared& listener,
                           const Logging::Logger::Shared& logger)
        : config_(config)
        , listener_(listener)
        , ioContext_()
        , resolver_(ioContext_)
        , socket_(ioContext_)
        , workGuard_(asio::make_work_guard(ioContext_))
        , handshakeTimer_(ioContext_)
        , reconnectTimer_(ioContext_)
        , reassembly_(ReassemblyConfig{
            .maxMessageSize = config_.maxMessageSize,
            .timeoutMs = config_.reassemblyTimeoutMs
        })
    {
        if (logger)
        {
            logger_ = logger->CreateChild("UDP-CLIENT");
        }
        else
        {
            logger_ = Utils::Log()->CreateChild("UDP-CLIENT");
        }
    }

    ClientImpl::~ClientImpl()
    {
        manuallyClosed_.store(true);
        stopRequested_.store(true);

        error_code ec;

        socket_.close(ec);

        try
        {
            handshakeTimer_.cancel();
            reconnectTimer_.cancel();
        }
        catch (...)
        {
        }

        workGuard_.reset();
        ioContext_.stop();
        StopThreads();
    }

    void ClientImpl::Initialise()
    {
        StartThreads();
        Resolve();
    }

    void ClientImpl::StartThreads()
    {
        const std::size_t requested = config_.ioThreads != 0
            ? config_.ioThreads
            : std::size_t{1};

        threads_.reserve(requested);

        for (std::size_t i = 0; i < requested; ++i)
        {
            threads_.emplace_back([this]()
            {
                ioContext_.run();
            });
        }

        logger_->Debug("Client started {} IO threads", threads_.size());
    }

    void ClientImpl::StopThreads()
    {
        threads_.clear();
    }

    void ClientImpl::Resolve()
    {
        if (stopRequested_.load())
        {
            return;
        }

        resolver_.async_resolve(
            udp::v4(),
            config_.host,
            std::to_string(config_.port),
            [self = shared_from_this()](error_code ec, udp::resolver::results_type results)
            {
                self->OnResolve(ec, results);
            });
    }

    void ClientImpl::OnResolve(const error_code ec, const udp::resolver::results_type& results)
    {
        if (ec)
        {
            ClientEvent event;
            event.type = ClientEventType::ConnectionError;
            event.text = "resolve_failed: " + ec.message();
            EnqueueEvent(event);

            ScheduleReconnect();
            return;
        }

        serverEndpoint_ = *results.begin();
        OpenSocket();
        StartReceive();
        StartHandshake();
    }

    void ClientImpl::OpenSocket()
    {
        error_code ec;
        socket_.open(serverEndpoint_.protocol(), ec);
        if (ec)
        {
            ClientEvent event;
            event.type = ClientEventType::ConnectionError;
            event.text = "socket_open_failed: " + ec.message();
            EnqueueEvent(event);
            ScheduleReconnect();
            return;
        }

        socket_.bind(udp::endpoint(udp::v4(), 0), ec);
        if (ec)
        {
            ClientEvent event;
            event.type = ClientEventType::ConnectionError;
            event.text = "socket_bind_failed: " + ec.message();
            EnqueueEvent(event);
            ScheduleReconnect();
            return;
        }
    }

    void ClientImpl::StartReceive()
    {
        socket_.async_receive_from(
            asio::buffer(rxBuffer_),
            remoteEndpoint_,
            [self = shared_from_this()](const error_code ec, const std::size_t bytes)
            {
                self->OnReceive(ec, bytes);
            });
    }

    void ClientImpl::OnReceive(const error_code ec, const std::size_t bytes)
    {
        if (ec)
        {
            if (stopRequested_.load())
            {
                return;
            }

            ClientEvent event;
            event.type = ClientEventType::Disconnected;
            EnqueueEvent(event);

            ScheduleReconnect();
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

        if (packet.header.type == PacketType::HandshakeWelcome)
        {
            OnWelcome(packet);
        }
        else if (packet.header.type == PacketType::DataFragment)
        {
            const std::uint64_t expectedSid = sessionId_.load();
            if (packet.header.sessionId != expectedSid)
            {
                if (logger_)
                {
                    logger_->Warning("Dropped fragment: чужой sessionId. got={} expected={} mid={} idx={}/{} from {}:{}",
                                     packet.header.sessionId,
                                     expectedSid,
                                     packet.header.messageId,
                                     packet.header.fragmentIndex,
                                     packet.header.fragmentCount,
                                     remoteEndpoint_.address().to_string(),
                                     remoteEndpoint_.port());
                }
            }
            else
            {
                const AddFragmentResult res = reassembly_.AddFragment(
                    packet.header.messageId,
                    packet.header.fragmentIndex,
                    packet.header.fragmentCount,
                    packet.header.totalSize,
                    packet.header.fragmentOffset,
                    packet.header.messageCrc,
                    packet.payload.data(),
                    packet.payload.size()
                );

                if (res.outcome == AddFragmentOutcome::Rejected)
                {
                    if (logger_)
                    {
                        logger_->Warning("Dropped fragment: reassembly reject reason={} sid={} mid={} idx={}/{} off={} size={} total={} msgCrc={}",
                                         res.reason,
                                         packet.header.sessionId,
                                         packet.header.messageId,
                                         packet.header.fragmentIndex,
                                         packet.header.fragmentCount,
                                         packet.header.fragmentOffset,
                                         packet.header.payloadSize,
                                         packet.header.totalSize,
                                         packet.header.messageCrc);
                    }
                }
                else if (res.outcome == AddFragmentOutcome::Duplicate)
                {
                    if (logger_)
                    {
                        logger_->Warning("Duplicate fragment ignored: sid={} mid={} idx={}/{} off={} size={} total={}",
                                         packet.header.sessionId,
                                         packet.header.messageId,
                                         packet.header.fragmentIndex,
                                         packet.header.fragmentCount,
                                         packet.header.fragmentOffset,
                                         packet.header.payloadSize,
                                         packet.header.totalSize);
                    }
                }
                else if (res.outcome == AddFragmentOutcome::Completed)
                {
                    EmitCompletedMessages();
                }
            }
        }

        StartReceive();
    }

    void ClientImpl::StartHandshake()
    {
        handshakeComplete_.store(false);
        sessionId_.store(0);
        handshakeStartMs_ = NowMs();

        SendHandshakeHello();

        handshakeTimer_.expires_after(std::chrono::milliseconds(config_.handshakeRetryMs));
        handshakeTimer_.async_wait([self = shared_from_this()](error_code ec)
        {
            self->OnHandshakeTimeout(ec);
        });
    }

    void ClientImpl::SendHandshakeHello()
    {
        PacketHeader header;
        header.type = PacketType::HandshakeHello;

        const auto packet = BuildPacket(header, {});
        socket_.async_send_to(
            asio::buffer(packet),
            serverEndpoint_,
            [](const error_code, const std::size_t) {});
    }

    void ClientImpl::OnHandshakeTimeout(const error_code ec)
    {
        if (ec == asio::error::operation_aborted)
        {
            return;
        }

        if (stopRequested_.load())
        {
            return;
        }

        if (handshakeComplete_.load())
        {
            return;
        }

        const std::uint32_t now = NowMs();
        if (now - handshakeStartMs_ > config_.handshakeTimeoutMs)
        {
            ClientEvent event;
            event.type = ClientEventType::ConnectionError;
            event.text = "handshake_timeout";
            EnqueueEvent(event);

            ScheduleReconnect();
            return;
        }

        SendHandshakeHello();

        handshakeTimer_.expires_after(std::chrono::milliseconds(config_.handshakeRetryMs));
        handshakeTimer_.async_wait([self = shared_from_this()](error_code timerEc)
        {
            self->OnHandshakeTimeout(timerEc);
        });
    }

    void ClientImpl::OnWelcome(const ParsedPacket& packet)
    {
        if (handshakeComplete_.load())
        {
            return;
        }

        sessionId_.store(packet.header.sessionId);
        handshakeComplete_.store(true);

        ClientEvent event;
        event.type = ClientEventType::Connected;
        EnqueueEvent(event);

        if (logger_)
        {
            logger_->Debug("Handshake complete -> sessionId={}", packet.header.sessionId);
        }
    }

    void ClientImpl::ScheduleReconnect()
    {
        if (!config_.autoReconnect)
        {
            return;
        }

        if (stopRequested_.load() || manuallyClosed_.load())
        {
            return;
        }

        error_code ec;
        socket_.close(ec);

        handshakeComplete_.store(false);
        sessionId_.store(0);

        reconnectTimer_.expires_after(std::chrono::milliseconds(config_.reconnectDelayMs));
        reconnectTimer_.async_wait([self = shared_from_this()](error_code timerEc)
        {
            self->DoReconnect(timerEc);
        });
    }

    void ClientImpl::DoReconnect(const error_code ec)
    {
        if (ec == asio::error::operation_aborted)
        {
            return;
        }

        if (stopRequested_.load() || manuallyClosed_.load())
        {
            return;
        }

        Resolve();
    }

    void ClientImpl::EnqueueEvent(const ClientEvent& event)
    {
        std::lock_guard lock(eventsMutex_);
        events_.push_back(event);
    }

    void ClientImpl::ProcessTick()
    {
        // IMPORTANT: cleanup incomplete reassembly
        reassembly_.TickCleanup();

        std::deque<ClientEvent> local;

        {
            std::lock_guard lock(eventsMutex_);
            local.swap(events_);
        }

        if (!listener_)
        {
            return;
        }

        for (const auto& e : local)
        {
            switch (e.type)
            {
                case ClientEventType::Connected:
                    listener_->OnConnected();
                    break;

                case ClientEventType::Disconnected:
                    listener_->OnDisconnected();
                    break;

                case ClientEventType::ConnectionError:
                    listener_->OnConnectionError(e.text, config_.autoReconnect);
                    break;

                case ClientEventType::Bytes:
                    listener_->OnMessage(e.bytes);
                    break;

                case ClientEventType::Text:
                    listener_->OnMessage(e.text);
                    break;

                case ClientEventType::Json:
                    listener_->OnMessage(e.jsonValue);
                    break;
            }
        }
    }

    uint64_t ClientImpl::SessionID()
    {
        return sessionId_.load();
    }

    Logging::Logger::Shared& ClientImpl::Log()
    {
        return logger_;
    }

    void ClientImpl::SendInternal(const std::span<const std::uint8_t> payload)
    {
        if (!handshakeComplete_.load())
        {
            return;
        }

        if (payload.size() > config_.maxMessageSize)
        {
            if (logger_)
            {
                logger_->Warning("Send payload too large: {} bytes (max={})",
                                 payload.size(),
                                 config_.maxMessageSize);
            }
            return;
        }

        const std::size_t mtu = config_.mtuPayload;
        const std::size_t maxPayloadPerPacket = mtu > HeaderSize ? (mtu - HeaderSize) : 0;

        if (maxPayloadPerPacket == 0)
        {
            if (logger_)
            {
                logger_->Warning("Send failed: maxPayloadPerPacket == 0");
            }
            return;
        }

        const std::uint64_t sid = sessionId_.load();
        const std::uint32_t messageId = nextMessageId_.fetch_add(1);

        const std::size_t total = payload.size();
        const std::uint16_t fragmentCount = static_cast<std::uint16_t>(
            (total + maxPayloadPerPacket - 1) / maxPayloadPerPacket);

        const std::uint32_t messageCrc = total == 0 ? 0u : Crc32(payload.data(), payload.size());

        for (std::uint16_t index = 0; index < fragmentCount; ++index)
        {
            const std::size_t offset = static_cast<std::size_t>(index) * maxPayloadPerPacket;
            const std::size_t size = std::min<std::size_t>(maxPayloadPerPacket, total - offset);

            PacketHeader header;
            header.type = PacketType::DataFragment;
            header.sessionId = sid;
            header.messageId = messageId;
            header.fragmentIndex = index;
            header.fragmentCount = fragmentCount;
            header.totalSize = static_cast<std::uint32_t>(total);

            header.fragmentOffset = static_cast<std::uint32_t>(offset); // NEW
            header.messageCrc = messageCrc;                             // NEW

            const auto packet = BuildPacket(header, payload.subspan(offset, size));

            socket_.async_send_to(
                asio::buffer(packet),
                serverEndpoint_,
                [](const error_code, const std::size_t) {});
        }
    }

    void ClientImpl::Send(const std::vector<std::uint8_t>& data)
    {
        SendInternal(data);
    }

    void ClientImpl::Send(const std::string_view text)
    {
        SendInternal(std::span<const std::uint8_t>(
            reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
    }

    void ClientImpl::Send(const JsonValue& jsonValue)
    {
        const std::string serialized = json::serialize(jsonValue);
        Send(serialized);
    }

    void ClientImpl::EmitCompletedMessages()
    {
        while (reassembly_.HasCompleted())
        {
            CompletedMessage completed = reassembly_.PopCompleted();

            const Mode mode = config_.mode;

            if (mode == Mode::Bytes)
            {
                ClientEvent event;
                event.type = ClientEventType::Bytes;
                event.bytes = std::move(completed.data);
                EnqueueEvent(event);
            }
            else if (mode == Mode::Text)
            {
                ClientEvent event;
                event.type = ClientEventType::Text;
                event.text.assign(reinterpret_cast<const char*>(completed.data.data()),
                                  completed.data.size());
                EnqueueEvent(event);
            }
            else
            {
                try
                {
                    const std::string text(reinterpret_cast<const char*>(completed.data.data()),
                                           completed.data.size());

                    ClientEvent event;
                    event.type = ClientEventType::Json;
                    event.jsonValue = json::parse(text);
                    EnqueueEvent(event);
                }
                catch (const std::exception& ex)
                {
                    if (logger_)
                    {
                        logger_->Warning("Client JSON parse error -> {}", ex.what());
                    }
                }
            }
        }
    }

    void ClientImpl::Close()
    {
        manuallyClosed_.store(true);

        error_code ec;
        socket_.close(ec);

        try
        {
            handshakeTimer_.cancel();
            reconnectTimer_.cancel();
        }
        catch (...)
        {
        }
    }

    Client::Shared Client::Create(const ClientConfig& config,
                                  const ClientListener::Shared& listener,
                                  const Logging::Logger::Shared& logger)
    {
        auto client = std::make_shared<ClientImpl>(config, listener, logger);
        client->Initialise();
        return client;
    }

} // namespace Utils::Net::Udp