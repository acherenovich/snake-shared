#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <chrono>
#include <functional>

#include <boost/json/array.hpp>
#include <boost/json/object.hpp>
#include <boost/json/value.hpp>

#include "logging.hpp"
#include "coroutine.hpp"

namespace Utils::DB::MySQL {

    namespace Logging = Utils::Logging;
    namespace json    = boost::json;

    using JsonArray  = json::array;
    using JsonObject = json::object;
    using JsonValue  = json::value;

    // ===================== CONFIG =====================

    struct Config
    {
        std::string host { "127.0.0.1" };
        std::uint16_t port { 3306 };

        std::string user;
        std::string password;
        std::string database;

        std::string charset { "utf8mb4" };

        std::size_t poolSize { 1 };

        std::chrono::milliseconds connectTimeout { std::chrono::seconds(5) };
        std::chrono::milliseconds readTimeout    { std::chrono::seconds(30) };
        std::chrono::milliseconds writeTimeout   { std::chrono::seconds(30) };

        bool autoReconnect { true };
    };

    // ===================== ERROR / RESULT =====================

    struct Error
    {
        unsigned int code { 0 };
        std::string message;
        std::string sqlState;

        [[nodiscard]] bool HasError() const
        {
            return code != 0;
        }
    };

    struct QueryResult
    {
        bool success { false };

        JsonArray rows;                  // SELECT / SHOW ...
        std::uint64_t affectedRows { 0 };// INSERT / UPDATE / DELETE
        std::uint64_t insertId { 0 };    // last insert id

        Error error;
    };

    using QueryCallback = std::function<void(const QueryResult &)>;

    // ===================== CLIENT =====================

    class Client
    {
    public:
        using Shared = std::shared_ptr<Client>;

        virtual ~Client() = default;

        [[nodiscard]] virtual bool IsConnected() = 0;

        virtual Logging::Logger::Shared & Log() = 0;

        /// Асинхронный запрос.
        /// sql уже должен быть полностью сформирован и экранирован.
        virtual void Execute(const std::string & sql,
                             const QueryCallback & callback) = 0;

        /// Синхронный запрос (блокирующий текущий поток).
        [[nodiscard]] virtual Task<QueryResult> Execute(const std::string & sql) = 0;

        virtual std::string EscapeString(const std::string& s) = 0;


        virtual void ProcessTick() = 0;


        /// Фабрика клиента.
        static Shared Create(const Config & config,
                             const Logging::Logger::Shared & logger = nullptr);
    };

} // namespace Utils::DB::MySQL