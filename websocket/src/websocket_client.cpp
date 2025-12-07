#include "websocket_client.hpp"

#include <format>
#include <memory>

namespace Utils::Net::Websocket {

    using asio::ip::tcp;

    ClientImpl::ClientImpl(const ClientConfig& config,
                           const ClientListener::Shared& listener,
                           const Logging::Logger::Shared& logger)
        : config_(config)
        , listener_(listener)
        , ioContext_()
        , resolver_(ioContext_)
        , workGuard_(asio::make_work_guard(ioContext_))
        , wsPlain_(ioContext_)
        , sslContext_(asio::ssl::context::tls_client)
        , reconnectTimer_(ioContext_)
    {
        if (!logger)
        {
            logger_ = Utils::Log()->CreateChild("WS-CLIENT");
        }
        else
        {
            logger_ = logger->CreateChild("WS-CLIENT");
        }

        if (config_.useTls)
        {
            sslContext_.set_default_verify_paths();

            if (!config_.tlsCaFile.empty())
            {
                sslContext_.load_verify_file(config_.tlsCaFile);
            }

            sslContext_.set_verify_mode(
                config_.tlsVerifyPeer
                    ? asio::ssl::verify_peer
                    : asio::ssl::verify_none);

            wsTls_ = std::make_unique<websocket::stream<asio::ssl::stream<tcp::socket>>>(
                ioContext_, sslContext_);

            logger_->Debug("Client TLS enabled (verify_peer={}, caFile='{}')",
                           config_.tlsVerifyPeer,
                           config_.tlsCaFile);
        }
    }

    ClientImpl::~ClientImpl()
    {
        manuallyClosed_.store(true);
        stopRequested_.store(true);

        error_code ecClose;

        if (config_.useTls && wsTls_)
        {
            wsTls_->close(websocket::close_code::normal, ecClose);
        }
        else
        {
            wsPlain_.close(websocket::close_code::normal, ecClose);
        }

        try
        {
            reconnectTimer_.cancel(); // в новых версиях без error_code, кидает исключение
        }
        catch (const boost::system::system_error&)
        {
            // в деструкторе можно тихо проигнорировать
        }

        workGuard_.reset();
        ioContext_.stop();
        StopThreads();

        if (logger_)
        {
            logger_->Debug("Client destroyed");
        }
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

        for (std::size_t index = 0; index < requested; ++index)
        {
            threads_.emplace_back([this]()
            {
                ioContext_.run();
            });
        }

        if (logger_)
        {
            logger_->Debug("Client started {} IO threads", threads_.size());
        }
    }

    void ClientImpl::StopThreads()
    {
        threads_.clear();
    }

    // ===================== Подключение / реконнект =====================

    void ClientImpl::Resolve()
    {
        if (stopRequested_.load())
        {
            return;
        }

        if (logger_)
        {
            logger_->Debug("Resolving {}:{}", config_.host, config_.port);
        }

        resolver_.async_resolve(
            config_.host,
            std::to_string(config_.port),
            [self = this->shared_from_this()](error_code ec,
                                              const tcp::resolver::results_type& results)
            {
                self->OnResolve(ec, results);
            });
    }

    void ClientImpl::OnResolve(const error_code ec,
                               const tcp::resolver::results_type& results)
    {
        if (ec)
        {
            if (logger_)
            {
                logger_->Error("Resolve error {}:{} -> {}",
                               config_.host, config_.port, ec.message());
            }

            ClientEvent event;
            event.type = ClientEventType::ConnectionError;
            event.text = "resolva_addr_failed: " + ec.message();
            EnqueueEvent(event);

            ScheduleReconnect();
            return;
        }

        if (logger_)
        {
            logger_->Debug("Resolved {}:{} -> {} endpoints",
                           config_.host, config_.port, results.size());
        }

        if (config_.useTls && wsTls_)
        {
            DoAsyncConnect(*wsTls_, results);
        }
        else
        {
            DoAsyncConnect(wsPlain_, results);
        }
    }

    void ClientImpl::DoAsyncConnect(websocket::stream<beast::tcp_stream>& ws,
                                const tcp::resolver::results_type& results)
    {
        asio::async_connect(
            beast::get_lowest_layer(ws).socket(), // tcp_stream::socket()
            results,
            [self = shared_from_this()](const error_code ec,
                                        const tcp::endpoint& endpoint)
            {
                self->OnConnect(ec, endpoint);
            });
    }

    void ClientImpl::DoAsyncConnect(websocket::stream<asio::ssl::stream<tcp::socket>>& ws,
                                    const tcp::resolver::results_type& results)
    {
        asio::async_connect(
            beast::get_lowest_layer(ws), // здесь уже tcp::socket&, без .socket()
            results,
            [self = shared_from_this()](const error_code ec,
                                        const tcp::endpoint& endpoint)
            {
                self->OnConnect(ec, endpoint);
            });
    }

    void ClientImpl::OnConnect(const error_code ec,
                               const tcp::endpoint& endpoint)
    {
        if (ec)
        {
            if (logger_)
            {
                logger_->Error("Connect error -> {}:{} -> {}",
                               endpoint.address().to_string(),
                               endpoint.port(),
                               ec.message());
            }

            ClientEvent event;
            event.type = ClientEventType::ConnectionError;
            event.text = "connection_error: " + ec.message();
            EnqueueEvent(event);

            ScheduleReconnect();
            return;
        }

        if (logger_)
        {
            logger_->Debug("Connected to {}:{}", endpoint.address().to_string(), endpoint.port());
        }

        if (config_.useTls && wsTls_)
        {
            auto& ws = *wsTls_;
            auto& sslStream = ws.next_layer();

            const std::string serverName =
                !config_.tlsServerNameOverride.empty()
                    ? config_.tlsServerNameOverride
                    : config_.host;

            SSL_set_tlsext_host_name(sslStream.native_handle(), serverName.c_str());

            sslStream.async_handshake(
                asio::ssl::stream_base::client,
                [self = this->shared_from_this()](error_code sslEc)
                {
                    self->OnSslHandshake(sslEc);
                });
        }
        else
        {
            wsPlain_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));

            if (config_.mode == Mode::Bytes)
            {
                wsPlain_.binary(true);
            }
            else
            {
                wsPlain_.text(true);
            }

            wsPlain_.async_handshake(
                config_.host,
                config_.path,
                [self = this->shared_from_this()](error_code ecHandshake)
                {
                    self->OnWsHandshake(ecHandshake);
                });
        }
    }

    void ClientImpl::OnSslHandshake(const error_code ec)
    {
        if (ec)
        {
            if (logger_)
            {
                logger_->Error("TLS handshake error -> {}", ec.message());
            }

            ClientEvent event;
            event.type = ClientEventType::ConnectionError;
            event.text = "ssl_handshake_failed: " + ec.message();
            EnqueueEvent(event);

            ScheduleReconnect();
            return;
        }

        if (!wsTls_)
        {
            return;
        }

        auto& ws = *wsTls_;

        ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));

        if (config_.mode == Mode::Bytes)
        {
            ws.binary(true);
        }
        else
        {
            ws.text(true);
        }

        const std::string hostHeader =
            !config_.tlsServerNameOverride.empty()
                ? config_.tlsServerNameOverride
                : config_.host;

        ws.async_handshake(
            hostHeader,
            config_.path,
            [self = this->shared_from_this()](error_code ecHandshake)
            {
                self->OnWsHandshake(ecHandshake);
            });
    }

    void ClientImpl::OnWsHandshake(const error_code ec)
    {
        if (ec)
        {
            if (logger_)
            {
                logger_->Error("WS handshake error -> {}", ec.message());
            }

            ClientEvent event;
            event.type = ClientEventType::ConnectionError;
            event.text = "ws_handhsake_failed: " + ec.message();
            EnqueueEvent(event);

            ScheduleReconnect();
            return;
        }

        if (logger_)
        {
            logger_->Debug("WS handshake completed");
        }

        ClientEvent event;
        event.type = ClientEventType::Connected;
        EnqueueEvent(event);

        DoRead();
    }

    void ClientImpl::ScheduleReconnect()
    {
        if (!config_.autoReconnect)
        {
            return;
        }

        if (manuallyClosed_.load() || stopRequested_.load())
        {
            return;
        }

        const std::uint32_t delayMs = config_.reconnectDelayMs;

        if (logger_)
        {
            logger_->Debug("Scheduling reconnect in {} ms", delayMs);
        }

        reconnectTimer_.expires_after(std::chrono::milliseconds(delayMs));

        reconnectTimer_.async_wait(
            [self = this->shared_from_this()](error_code ec)
            {
                self->DoReconnect(ec);
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

        if (logger_)
        {
            logger_->Debug("Reconnect timer fired, resolving again...");
        }

        Resolve();
    }

    // ===================== Чтение / запись =====================

    void ClientImpl::DoRead()
    {
        if (stopRequested_.load())
        {
            return;
        }

        readBuffer_.consume(readBuffer_.size());

        if (config_.useTls && wsTls_)
        {
            wsTls_->async_read(
                readBuffer_,
                [self = this->shared_from_this()](error_code ec, const std::size_t bytesTransferred)
                {
                    self->OnRead(ec, bytesTransferred);
                });
        }
        else
        {
            wsPlain_.async_read(
                readBuffer_,
                [self = this->shared_from_this()](error_code ec, const std::size_t bytesTransferred)
                {
                    self->OnRead(ec, bytesTransferred);
                });
        }
    }

    void ClientImpl::OnRead(const error_code ec, const std::size_t /*bytesTransferred*/)
    {
        if (ec == websocket::error::closed)
        {
            if (logger_)
            {
                logger_->Debug("Client connection closed by remote");
            }

            ClientEvent event;
            event.type = ClientEventType::Disconnected;
            EnqueueEvent(event);

            ScheduleReconnect();
            return;
        }

        if (ec)
        {
            if (logger_)
            {
                logger_->Error("Client read error -> {}", ec.message());
            }

            ClientEvent event;
            event.type = ClientEventType::Disconnected;
            EnqueueEvent(event);

            ScheduleReconnect();
            return;
        }

        const std::string dataStr = beast::buffers_to_string(readBuffer_.data());

        const Mode mode = config_.mode;

        if (mode == Mode::Bytes)
        {
            ClientEvent event;
            event.type = ClientEventType::Bytes;
            event.bytes.assign(dataStr.begin(), dataStr.end());
            EnqueueEvent(event);
        }
        else if (mode == Mode::Text)
        {
            ClientEvent event;
            event.type = ClientEventType::Text;
            event.text = dataStr;
            EnqueueEvent(event);
        }
        else
        {
            try
            {
                ClientEvent event;
                event.type = ClientEventType::Json;
                event.jsonValue = json::parse(dataStr);
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

        DoRead();
    }

    void ClientImpl::EnqueueEvent(const ClientEvent& event)
    {
        std::lock_guard lock(eventsMutex_);
        events_.push_back(event);
    }

    void ClientImpl::ProcessTick()
    {
        std::deque<ClientEvent> local;

        {
            std::lock_guard lock(eventsMutex_);
            local.swap(events_);
        }

        if (!listener_)
        {
            return;
        }

        for (const ClientEvent& event : local)
        {
            switch (event.type)
            {
                case ClientEventType::Connected:
                    listener_->OnConnected();
                    break;

                case ClientEventType::Disconnected:
                    listener_->OnDisconnected();
                    break;

                case ClientEventType::ConnectionError:
                    listener_->OnConnectionError(event.text, config_.autoReconnect);

                case ClientEventType::Bytes:
                    listener_->OnMessage(event.bytes);
                    break;

                case ClientEventType::Text:
                    listener_->OnMessage(event.text);
                    break;

                case ClientEventType::Json:
                    listener_->OnMessage(event.jsonValue);
                    break;
            }
        }
    }

    Logging::Logger::Shared& ClientImpl::Log()
    {
        return logger_;
    }

    void ClientImpl::EnqueueSendInternal(const std::string& payload)
    {
        {
            std::lock_guard lock(sendMutex_);
            sendQueue_.push_back(payload);
            if (writeInProgress_)
            {
                return;
            }

            writeInProgress_ = true;
        }

        DoWrite();
    }

    void ClientImpl::Send(const std::vector<std::uint8_t>& data)
    {
        const std::string payload(reinterpret_cast<const char*>(data.data()), data.size());
        EnqueueSendInternal(payload);
    }

    void ClientImpl::Send(const std::string_view text)
    {
        EnqueueSendInternal(std::string(text));
    }

    void ClientImpl::Send(const json::value& jsonValue)
    {
        const std::string serialized = json::serialize(jsonValue);
        EnqueueSendInternal(serialized);
    }

    void ClientImpl::DoWrite()
    {
        std::string next;

        {
            std::lock_guard lock(sendMutex_);
            if (sendQueue_.empty())
            {
                writeInProgress_ = false;
                return;
            }

            next = std::move(sendQueue_.front());
            sendQueue_.pop_front();
        }

        if (config_.useTls && wsTls_)
        {
            wsTls_->async_write(
                asio::buffer(next),
                [self = this->shared_from_this()](error_code ec, const std::size_t bytesTransferred)
                {
                    self->OnWrite(ec, bytesTransferred);
                });
        }
        else
        {
            wsPlain_.async_write(
                asio::buffer(next),
                [self = this->shared_from_this()](error_code ec, const std::size_t bytesTransferred)
                {
                    self->OnWrite(ec, bytesTransferred);
                });
        }
    }

    void ClientImpl::OnWrite(const error_code ec, const std::size_t /*bytesTransferred*/)
    {
        if (ec)
        {
            if (logger_)
            {
                logger_->Error("Client write error -> {}", ec.message());
            }

            error_code closeEc;

            if (config_.useTls && wsTls_)
            {
                wsTls_->close(websocket::close_code::normal, closeEc);
            }
            else
            {
                wsPlain_.close(websocket::close_code::normal, closeEc);
            }

            return;
        }

        DoWrite();
    }

    void ClientImpl::Close()
    {
        manuallyClosed_.store(true);

        error_code ec;

        if (config_.useTls && wsTls_)
        {
            wsTls_->close(websocket::close_code::normal, ec);
        }
        else
        {
            wsPlain_.close(websocket::close_code::normal, ec);
        }

        try
        {
            reconnectTimer_.cancel();
        }
        catch (const boost::system::system_error& e)
        {
            if (logger_)
            {
                logger_->Warning("Client reconnect timer cancel error -> {}", e.what());
            }
        }

        if (ec && logger_)
        {
            logger_->Warning("Client close error -> {}", ec.message());
        }
    }

    Client::Shared Client::Create(const ClientConfig& config,
                                  const ClientListener::Shared& listener,
                                  const Logging::Logger::Shared& logger)
    {
        auto client = std::make_shared<ClientImpl>(config, listener, logger);
        client->Initialise(); // здесь уже есть control block => shared_from_this() валиден
        return client;
    }

} // namespace Utils::Net::Websocket