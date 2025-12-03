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

namespace Utils::Net::WebSocket {

    namespace Logging = Utils::Logging;
    using JsonValue = boost::json::value;

    // ===================== Общие штуки =====================

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

        virtual void OnSessionConnected(const std::shared_ptr<Session>& session) = 0;
        virtual void OnSessionDisconnected(const std::shared_ptr<Session>& session) = 0;

        virtual void OnMessage(const std::shared_ptr<Session>& session,
                               const std::vector<std::uint8_t>& data) = 0;

        virtual void OnMessage(const std::shared_ptr<Session>& session,
                               std::string_view text) = 0;

        virtual void OnMessage(const std::shared_ptr<Session>& session,
                               const JsonValue& json) = 0;
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

        // Логгер с префиксом [CORE][WS][IP]
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

        [[nodiscard]] virtual Mode GetMode() const = 0;

        // logger опционален — по умолчанию Utils::Log()->CreateChild("WS")
        static Shared Create(const ServerConfig& config,
                             const Listener::Shared& listener,
                             const Logging::Logger::Shared& logger = nullptr);
    };

    // ===================== CLIENT API =====================

    class ClientListener
    {
    public:
        using Shared = std::shared_ptr<ClientListener>;

        virtual ~ClientListener() = default;

        virtual void OnConnected() = 0;
        virtual void OnDisconnected() = 0;

        virtual void OnMessage(const std::vector<std::uint8_t>& data) = 0;
        virtual void OnMessage(std::string_view text) = 0;
        virtual void OnMessage(const JsonValue& json) = 0;
    };

    struct ClientConfig
    {
        std::string host { "127.0.0.1" };
        std::uint16_t port { 8080 };
        std::string path { "/" };      // HTTP target для WS, например "/ws"

        Mode mode { Mode::Text };

        // TLS
        bool useTls { false };
        bool tlsVerifyPeer { true };
        std::string tlsServerNameOverride; // если пусто — host
        std::string tlsCaFile;             // если пусто — используем default_verify_paths

        // потоков io_context
        std::size_t ioThreads { 1 };

        // автореконнект
        bool autoReconnect { true };
        std::uint32_t reconnectDelayMs { 1000 };
    };

    class Client
    {
    public:
        using Shared = std::shared_ptr<Client>;

        virtual ~Client() = default;

        // Вызывать из основного потока, чтобы отработали callback-и
        virtual void ProcessTick() = 0;

        virtual void Send(const std::vector<std::uint8_t>& data) = 0;
        virtual void Send(std::string_view text) = 0;
        virtual void Send(const JsonValue& json) = 0;

        virtual void Close() = 0;

        // logger опционален — по умолчанию Utils::Log()->CreateChild("WS-CLIENT")
        static Shared Create(const ClientConfig& config,
                             const ClientListener::Shared& listener,
                             const Logging::Logger::Shared& logger = nullptr);
    };

} // namespace Utils::Net::WebSocket