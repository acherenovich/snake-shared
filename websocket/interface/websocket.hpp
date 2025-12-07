#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <chrono>

#include <boost/json/value.hpp>

// наш интерфейс логгера
#include <logging.hpp>

namespace Utils::Net::Websocket {

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
        std::uint16_t port { 8080 };

        Mode mode { Mode::Text };

        bool useTls { false };
        std::string tlsCertFile;
        std::string tlsKeyFile;
        std::string tlsDhFile;

        std::size_t ioThreads { 0 }; // 0 -> hardware_concurrency
    };

    // ===================== SESSION (SERVER SIDE) =====================

    class Session
    {
    public:
        using Shared = std::shared_ptr<Session>;

        virtual ~Session() = default;

        [[nodiscard]] virtual std::string RemoteAddress() const = 0;
        [[nodiscard]] virtual std::uint16_t RemotePort() const = 0;

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
        std::uint16_t port { 8080 };
        std::string path { "/" };

        Mode mode { Mode::Text };

        // TLS
        bool useTls { false };
        bool tlsVerifyPeer { true };
        std::string tlsServerNameOverride; // если пусто — host
        std::string tlsCaFile;             // если пусто — используем default_verify_paths

        // потоков io_context
        std::size_t ioThreads { 1 };

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

} // namespace Utils::Net::Websocket