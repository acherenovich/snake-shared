#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <chrono>

#include <boost/json/value.hpp>

#include <logging.hpp>

namespace Utils::Net::Udp {

    namespace Logging = Utils::Logging;
    using JsonValue = boost::json::value;

    enum class Mode
    {
        Bytes,
        Text,
        Json
    };

    class Session;
    class Server;
    class Client;

    // ===================== SERVER LISTENER =====================

    class Listener
    {
    public:
        using Shared = std::shared_ptr<Listener>;
        virtual ~Listener() = default;

        virtual void OnSessionConnected(const std::shared_ptr<Session>& session) {};
        virtual void OnSessionDisconnected(const std::shared_ptr<Session>& session) {};

        virtual void OnMessage(const std::shared_ptr<Session>& session,
                               const std::vector<std::uint8_t>& data) {};

        virtual void OnMessage(const std::shared_ptr<Session>& session,
                               std::string_view text) {};

        virtual void OnMessage(const std::shared_ptr<Session>& session,
                               const JsonValue& json) {};
    };

    // ===================== SERVER CONFIG =====================

    struct ServerConfig
    {
        std::string address { "0.0.0.0" };
        std::uint16_t port { 7777 };

        Mode mode { Mode::Bytes };

        std::size_t ioThreads { 0 }; // 0 -> hardware_concurrency

        // fragmentation / reassembly
        std::size_t mtuPayload { 1200 }; // max UDP payload we will send (incl header)
        std::size_t maxMessageSize { 256 * 1024 }; // 256 KB
        std::uint32_t reassemblyTimeoutMs { 1500 }; // drop incomplete messages
        std::uint32_t sessionTimeoutMs { 10000 }; // if no packets -> disconnect session
    };

    // ===================== SESSION (SERVER SIDE) =====================

    class Session
    {
    public:
        using Shared = std::shared_ptr<Session>;
        virtual ~Session() = default;

        [[nodiscard]] virtual std::uint64_t SessionId() const = 0;

        [[nodiscard]] virtual std::string RemoteAddress() = 0;
        [[nodiscard]] virtual std::uint16_t RemotePort() = 0;

        virtual Logging::Logger::Shared& Log() = 0;

        virtual void Close() = 0;

        virtual void Send(const std::vector<std::uint8_t>& data) = 0;
        virtual void Send(std::string_view text) = 0;
        virtual void Send(const JsonValue& json) = 0;
    };

    // ===================== SERVER =====================

    class Server
    {
    public:
        using Shared = std::shared_ptr<Server>;
        virtual ~Server() = default;

        virtual void ProcessTick() = 0;

        virtual Logging::Logger::Shared& Log() = 0;

        virtual void SetupListener(const Listener::Shared& listener) = 0;

        [[nodiscard]] virtual Mode GetMode() const = 0;

        static Shared Create(const ServerConfig& config,
                             const Listener::Shared& listener = nullptr,
                             const Logging::Logger::Shared& logger = nullptr);
    };

    // ===================== CLIENT API =====================

    class ClientListener
    {
    public:
        using Shared = std::shared_ptr<ClientListener>;
        virtual ~ClientListener() = default;

        virtual void OnConnected() {};
        virtual void OnDisconnected() {};

        virtual void OnConnectionError(const std::string & error, bool reconnect) {};

        virtual void OnMessage(const std::vector<std::uint8_t>& data) {}
        virtual void OnMessage(std::string_view text) {}
        virtual void OnMessage(const JsonValue& json) {}
    };

    struct ClientConfig
    {
        std::string host { "127.0.0.1" };
        std::uint16_t port { 7777 };

        Mode mode { Mode::Bytes };

        std::size_t ioThreads { 1 };

        // fragmentation / reassembly
        std::size_t mtuPayload { 1200 };
        std::size_t maxMessageSize { 256 * 1024 };
        std::uint32_t reassemblyTimeoutMs { 1500 };

        // Handshake / sessionId
        std::uint32_t handshakeRetryMs { 500 };
        std::uint32_t handshakeTimeoutMs { 5000 };

        bool autoReconnect { true };
        std::uint32_t reconnectDelayMs { 1000 };
    };

    class Client
    {
    public:
        using Shared = std::shared_ptr<Client>;
        virtual ~Client() = default;

        virtual void ProcessTick() = 0;

        virtual Logging::Logger::Shared& Log() = 0;

        virtual void Send(const std::vector<std::uint8_t>& data) = 0;
        virtual void Send(std::string_view text) = 0;
        virtual void Send(const JsonValue& json) = 0;

        virtual void Close() = 0;

        static Shared Create(const ClientConfig& config,
                             const ClientListener::Shared& listener = nullptr,
                             const Logging::Logger::Shared& logger = nullptr);
    };

} // namespace Utils::Net::Udp