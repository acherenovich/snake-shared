#include "websocket_session.hpp"
#include "websocket_server.hpp"

#include <algorithm>
#include <format>

namespace Utils::Net::WebSocket {

    // ========== BasicSessionImpl ==========

    template<typename NextLayer>
    BasicSessionImpl<NextLayer>::BasicSessionImpl(
        ServerImpl* const server,
        websocket::stream<NextLayer> stream,
        const Logging::Logger::Shared& logger,
        const std::string& remoteAddress,
        const std::uint16_t remotePort
    )
        : server_(server)
        , stream_(std::move(stream))
        , logger_(logger)
        , remoteAddress_(remoteAddress)
        , remotePort_(remotePort)
    {
        if (logger_)
        {
            logger_->Debug("New session from {}:{}", remoteAddress_, remotePort_);
        }
    }

    template<typename NextLayer>
    std::string BasicSessionImpl<NextLayer>::RemoteAddress() const
    {
        return remoteAddress_;
    }

    template<typename NextLayer>
    std::uint16_t BasicSessionImpl<NextLayer>::RemotePort() const
    {
        return remotePort_;
    }

    template<typename NextLayer>
    void BasicSessionImpl<NextLayer>::Run()
    {
        auto self = this->shared_from_this();

        stream_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));

        const Mode mode = server_->GetMode();
        if (mode == Mode::Bytes)
        {
            stream_.binary(true);
        }
        else
        {
            stream_.text(true);
        }

        stream_.async_accept(
            [self](const error_code ec)
            {
                if (ec)
                {
                    if (self->logger_)
                    {
                        self->logger_->Error("WS accept failed for {}:{} -> {}",
                                             self->remoteAddress_, self->remotePort_, ec.message());
                    }

                    Event event;
                    event.type = EventType::Disconnected;
                    event.session = std::static_pointer_cast<Session>(self);
                    self->server_->EnqueueEvent(event);
                    self->server_->RemoveSession(event.session);

                    return;
                }

                if (self->logger_)
                {
                    self->logger_->Debug("WS accepted for {}:{}", self->remoteAddress_, self->remotePort_);
                }

                self->DoRead();
            });
    }

    template<typename NextLayer>
    void BasicSessionImpl<NextLayer>::DoRead()
    {
        auto self = this->shared_from_this();

        readBuffer_.consume(readBuffer_.size());

        stream_.async_read(
            readBuffer_,
            [self](const error_code ec, const std::size_t bytesTransferred)
            {
                self->OnRead(ec, bytesTransferred);
            });
    }

    template<typename NextLayer>
    void BasicSessionImpl<NextLayer>::OnRead(const error_code ec, const std::size_t /*bytesTransferred*/)
    {
        if (ec == websocket::error::closed)
        {
            if (logger_)
            {
                logger_->Debug("Connection closed {}:{}", remoteAddress_, remotePort_);
            }

            Event event;
            event.type = EventType::Disconnected;
            event.session = std::static_pointer_cast<Session>(this->shared_from_this());
            server_->EnqueueEvent(event);
            server_->RemoveSession(event.session);
            return;
        }

        if (ec)
        {
            if (logger_)
            {
                logger_->Error("Read error {}:{} -> {}", remoteAddress_, remotePort_, ec.message());
            }

            Event event;
            event.type = EventType::Disconnected;
            event.session = std::static_pointer_cast<Session>(this->shared_from_this());
            server_->EnqueueEvent(event);
            server_->RemoveSession(event.session);
            return;
        }

        const Mode mode = server_->GetMode();

        const std::string dataStr = beast::buffers_to_string(readBuffer_.data());

        if (mode == Mode::Bytes)
        {
            Event event;
            event.type = EventType::Bytes;
            event.session = std::static_pointer_cast<Session>(this->shared_from_this());
            event.bytes.assign(dataStr.begin(), dataStr.end());
            server_->EnqueueEvent(event);
        }
        else if (mode == Mode::Text)
        {
            Event event;
            event.type = EventType::Text;
            event.session = std::static_pointer_cast<Session>(this->shared_from_this());
            event.text = dataStr;
            server_->EnqueueEvent(event);
        }
        else // Mode::Json
        {
            try
            {
                Event event;
                event.type = EventType::Json;
                event.session = std::static_pointer_cast<Session>(this->shared_from_this());
                event.jsonValue = json::parse(dataStr);
                server_->EnqueueEvent(event);
            }
            catch (const std::exception& ex)
            {
                if (logger_)
                {
                    logger_->Warning("JSON parse error from {}:{} -> {}",
                                     remoteAddress_, remotePort_, ex.what());
                }
            }
        }

        DoRead();
    }

    template<typename NextLayer>
    void BasicSessionImpl<NextLayer>::Close()
    {
        error_code ec;
        stream_.close(websocket::close_code::normal, ec);
        if (ec && logger_)
        {
            logger_->Warning("Close error for {}:{} -> {}", remoteAddress_, remotePort_, ec.message());
        }
    }

    template<typename NextLayer>
    void BasicSessionImpl<NextLayer>::EnqueueSendInternal(const std::string& payload)
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

    template<typename NextLayer>
    void BasicSessionImpl<NextLayer>::Send(const std::vector<std::uint8_t>& data)
    {
        const std::string payload(reinterpret_cast<const char*>(data.data()), data.size());
        EnqueueSendInternal(payload);
    }

    template<typename NextLayer>
    void BasicSessionImpl<NextLayer>::Send(const std::string_view text)
    {
        EnqueueSendInternal(std::string(text));
    }

    template<typename NextLayer>
    void BasicSessionImpl<NextLayer>::Send(const json::value& jsonValue)
    {
        const std::string serialized = json::serialize(jsonValue);
        EnqueueSendInternal(serialized);
    }

    template<typename NextLayer>
    void BasicSessionImpl<NextLayer>::DoWrite()
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

        auto self = this->shared_from_this();

        stream_.async_write(
            asio::buffer(next),
            [self](const error_code ec, const std::size_t bytesTransferred)
            {
                self->OnWrite(ec, bytesTransferred);
            });
    }

    template<typename NextLayer>
    void BasicSessionImpl<NextLayer>::OnWrite(const error_code ec, const std::size_t /*bytesTransferred*/)
    {
        if (ec)
        {
            if (logger_)
            {
                logger_->Error("Write error {}:{} -> {}", remoteAddress_, remotePort_, ec.message());
            }

            error_code closeEc;
            stream_.close(websocket::close_code::normal, closeEc);
            return;
        }

        DoWrite();
    }

    template<typename NextLayer>
    Logging::Logger::Shared & BasicSessionImpl<NextLayer>::Log()
    {
        return logger_;
    }

    // ========= Явные инстанциации шаблонов =========

    template class BasicSessionImpl<beast::tcp_stream>;
    template class BasicSessionImpl<asio::ssl::stream<asio::ip::tcp::socket>>;

} // namespace Utils::Net::WebSocket