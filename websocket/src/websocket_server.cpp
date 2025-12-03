#include "websocket_server.hpp"
#include "websocket_session.hpp"

#include <algorithm>
#include <format>

namespace Utils::Net::WebSocket {

    using asio::ip::tcp;

    ServerImpl::ServerImpl(const ServerConfig& config,
                           const Listener::Shared& listener,
                           const Logging::Logger::Shared& logger)
        : config_(config)
        , listener_(listener)
        , ioContext_()
        , acceptor_(ioContext_)
        , sslContext_(ssl::context::tls_server)
    {
        // базовый логгер: либо переданный, либо глобальный Utils::Log()
        Logging::Logger::Shared baseLogger;

        if (logger)
        {
            baseLogger = logger;
        }
        else
        {
            baseLogger = Utils::Log();
        }

        // [CORE] [WS] ...
        logger_ = baseLogger->CreateChild("WS");

        if (config_.useTls)
        {
            sslContext_.set_options(
                ssl::context::default_workarounds
                | ssl::context::no_sslv2
                | ssl::context::no_sslv3
                | ssl::context::single_dh_use
            );

            sslContext_.use_certificate_chain_file(config_.tlsCertFile);
            sslContext_.use_private_key_file(config_.tlsKeyFile, ssl::context::pem);

            if (!config_.tlsDhFile.empty())
            {
                sslContext_.use_tmp_dh_file(config_.tlsDhFile);
            }

            logger_->Debug("TLS enabled: cert={}, key={}, dh={}",
                           config_.tlsCertFile,
                           config_.tlsKeyFile,
                           config_.tlsDhFile);
        }

        const tcp::endpoint endpoint(asio::ip::make_address(config_.address), config_.port);

        error_code ec;
        acceptor_.open(endpoint.protocol(), ec);
        if (ec)
        {
            logger_->Fatal("Failed to open acceptor: {}", ec.message());
        }

        acceptor_.set_option(asio::socket_base::reuse_address(true), ec);
        if (ec)
        {
            logger_->Fatal("Failed to set reuse_address: {}", ec.message());
        }

        acceptor_.bind(endpoint, ec);
        if (ec)
        {
            logger_->Fatal("Failed to bind {}:{} -> {}", config_.address, config_.port, ec.message());
        }

        acceptor_.listen(asio::socket_base::max_listen_connections, ec);
        if (ec)
        {
            logger_->Fatal("Failed to listen on {}:{} -> {}", config_.address, config_.port, ec.message());
        }

        logger_->Debug("Listening on {}:{}", config_.address, config_.port);

        AcceptLoop();
        StartThreads();
    }

    ServerImpl::~ServerImpl()
    {
        stopRequested_.store(true);

        error_code ec;
        acceptor_.close(ec);

        ioContext_.stop();
        StopThreads();

        if (logger_)
        {
            logger_->Debug("Server stopped");
        }
    }

    Mode ServerImpl::GetMode() const
    {
        return config_.mode;
    }

    ssl::context& ServerImpl::SslContext()
    {
        return sslContext_;
    }

    void ServerImpl::StartThreads()
    {
        const std::size_t requested = config_.ioThreads != 0
            ? config_.ioThreads
            : std::max<std::size_t>(1, std::thread::hardware_concurrency());

        threads_.reserve(requested);

        for (std::size_t index = 0; index < requested; ++index)
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
        // std::jthread сам join-ится в деструкторе
        threads_.clear();
    }

    void ServerImpl::AcceptLoop()
    {
        acceptor_.async_accept(
            asio::make_strand(ioContext_),
            [this](const error_code ec, tcp::socket socket)
            {
                OnAccept(ec, std::move(socket));
            });
    }

    void ServerImpl::OnAccept(const error_code ec, tcp::socket socket)
    {
        if (ec)
        {
            if (logger_)
            {
                logger_->Warning("Accept error: {}", ec.message());
            }

            if (!stopRequested_.load())
            {
                AcceptLoop();
            }

            return;
        }

        const tcp::endpoint remoteEndpoint = socket.remote_endpoint();
        const std::string ipString = remoteEndpoint.address().to_string();
        const std::uint16_t port = remoteEndpoint.port();

        if (!config_.useTls)
        {
            // plain ws
            beast::tcp_stream tcpStream(std::move(socket));
            websocket::stream<beast::tcp_stream> ws(std::move(tcpStream));

            auto sessionLogger = logger_->CreateChild(ipString); // [CORE][WS][ip]
            auto sessionImpl = std::make_shared<PlainSession>(
                this,
                std::move(ws),
                sessionLogger,
                ipString,
                port
            );

            std::shared_ptr<Session> session = sessionImpl;

            {
                std::lock_guard lock(sessionsMutex_);
                sessions_.push_back(session);
            }

            Event event;
            event.type = EventType::Connected;
            event.session = session;
            EnqueueEvent(event);

            sessionImpl->Run();
        }
        else
        {
            // tls ws (wss)
            auto sslStream = std::make_shared<ssl::stream<tcp::socket>>(std::move(socket), sslContext_);

            auto self = this;

            sslStream->async_handshake(
                ssl::stream_base::server,
                [this, sslStream, ipString, port](const error_code handshakeEc)
                {
                    if (handshakeEc)
                    {
                        if (logger_)
                        {
                            logger_->Error("TLS handshake error {}:{} -> {}",
                                           ipString, port, handshakeEc.message());
                        }
                        return;
                    }

                    websocket::stream<ssl::stream<tcp::socket>> ws(std::move(*sslStream));

                    auto sessionLogger = logger_->CreateChild(ipString);
                    auto sessionImpl = std::make_shared<TlsSession>(
                        this,
                        std::move(ws),
                        sessionLogger,
                        ipString,
                        port
                    );

                    std::shared_ptr<Session> session = sessionImpl;

                    {
                        std::lock_guard lock(sessionsMutex_);
                        sessions_.push_back(session);
                    }

                    Event event;
                    event.type = EventType::Connected;
                    event.session = session;
                    EnqueueEvent(event);

                    sessionImpl->Run();
                });
        }

        AcceptLoop();
    }

    void ServerImpl::EnqueueEvent(const Event& event)
    {
        std::lock_guard lock(eventsMutex_);
        events_.push_back(event);
    }

    void ServerImpl::RemoveSession(const std::shared_ptr<Session>& session)
    {
        std::lock_guard lock(sessionsMutex_);
        std::erase(sessions_, session);
    }

    void ServerImpl::ProcessTick()
    {
        std::deque<Event> localEvents;

        {
            std::lock_guard lock(eventsMutex_);
            localEvents.swap(events_);
        }

        if (!listener_)
        {
            return;
        }

        for (const Event& event : localEvents)
        {
            switch (event.type)
            {
            case EventType::Connected:
                listener_->OnSessionConnected(event.session);
                break;

            case EventType::Disconnected:
                listener_->OnSessionDisconnected(event.session);
                break;

            case EventType::Bytes:
                listener_->OnMessage(event.session, event.bytes);
                break;

            case EventType::Text:
                listener_->OnMessage(event.session, event.text);
                break;

            case EventType::Json:
                listener_->OnMessage(event.session, event.jsonValue);
                break;
            }
        }
    }

    // ======= Фабрика Server::Create =======

    Server::Shared Server::Create(const ServerConfig& config,
                                  const Listener::Shared& listener,
                                  const Logging::Logger::Shared& logger)
    {
        return std::make_shared<ServerImpl>(config, listener, logger);
    }

} // namespace Utils::Net::WebSocket