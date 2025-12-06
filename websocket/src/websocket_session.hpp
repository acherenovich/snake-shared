#pragma once

#include <deque>
#include <mutex>
#include <string>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/json.hpp>

#include <websocket.hpp>
#include <logging.hpp>

namespace Utils::Net::Websocket {

    namespace asio      = boost::asio;
    namespace beast     = boost::beast;
    namespace websocket = beast::websocket;
    namespace json      = boost::json;
    using boost::system::error_code;

    class ServerImpl;

    class BasicSessionBase
    {
    public:
        virtual ~BasicSessionBase() = default;
    };

    template<typename NextLayer>
    class BasicSessionImpl final :
        public Session,
        public BasicSessionBase,
        public std::enable_shared_from_this<BasicSessionImpl<NextLayer>>
    {
    public:
        using Shared = std::shared_ptr<BasicSessionImpl<NextLayer>>;

        BasicSessionImpl(ServerImpl* server,
                         websocket::stream<NextLayer> stream,
                         const Logging::Logger::Shared& logger,
                         const std::string& remoteAddress,
                         std::uint16_t remotePort);

        [[nodiscard]] std::string RemoteAddress() const override;
        [[nodiscard]] std::uint16_t RemotePort() const override;

        void Close() override;

        void Send(const std::vector<std::uint8_t>& data) override;
        void Send(std::string_view text) override;
        void Send(const json::value& jsonValue) override;

        void Run();

    private:
        void DoRead();
        void OnRead(error_code ec, std::size_t bytesTransferred);

        void EnqueueSendInternal(const std::string& payload);
        void DoWrite();
        void OnWrite(error_code ec, std::size_t bytesTransferred);
    public:
        Logging::Logger::Shared & Log() override;
    private:
        ServerImpl* server_;
        websocket::stream<NextLayer> stream_;
        Logging::Logger::Shared logger_;

        beast::flat_buffer readBuffer_;

        std::mutex sendMutex_;
        std::deque<std::string> sendQueue_;
        bool writeInProgress_ { false };

        std::string remoteAddress_;
        std::uint16_t remotePort_ { 0 };
    };

    using PlainSession = BasicSessionImpl<beast::tcp_stream>;
    using TlsSession   = BasicSessionImpl<asio::ssl::stream<asio::ip::tcp::socket>>;

} // namespace Utils::Net::Websocket