#pragma once

#include <cstdint>
#include <vector>
#include <string_view>
#include <cstring>
#include <span>
#include <SFML/System/Vector2.hpp>

namespace Utils::Legacy::Game::Net {

    constexpr std::uint16_t kNetVersion = 1;

    enum class MessageType : std::uint16_t
    {
        // client -> server
        ClientInput = 10,
        RequestFullUpdate = 11,

        // server -> client
        FullUpdate = 100,
        PartialUpdate = 101,
    };

    enum class EntityType : std::uint8_t
    {
        Snake = 1,
        Food = 2
    };

    enum class EntityFlags : std::uint8_t
    {
        None   = 0,
        New    = 1 << 0,
        Update = 1 << 1,
        Remove = 1 << 2,
    };

    inline EntityFlags operator|(const EntityFlags a, const EntityFlags b)
    {
        return static_cast<EntityFlags>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
    }

    inline bool HasFlag(const EntityFlags flags, const EntityFlags test)
    {
        return (static_cast<std::uint8_t>(flags) & static_cast<std::uint8_t>(test)) != 0;
    }

#pragma pack(push, 1)

    struct MessageHeader
    {
        std::uint16_t type { 0 };
        std::uint16_t version { kNetVersion };

        std::uint32_t seq { 0 };     // server update seq OR client input seq
        std::uint32_t frame { 0 };   // server frame (for updates)

        std::uint32_t payloadBytes { 0 };
    };

    struct ClientInputPayload
    {
        float destinationX { 0.0f };
        float destinationY { 0.0f };
        std::uint32_t clientFrame { 0 };
    };

    struct EntityEntryHeader
    {
        EntityType type { EntityType::Food };
        EntityFlags flags { EntityFlags::None };
        std::uint32_t entityID { 0 };
    };

    struct Color
    {
        std::uint8_t r { 255 };
        std::uint8_t g { 255 };
        std::uint8_t b { 255 };
        std::uint8_t a { 255 };
    };

    // Food state (small)
    struct FoodState
    {
        float x { 0 };
        float y { 0 };

        std::uint8_t power { 1 };
        Color color {};

        std::uint8_t killed { 0 }; // server removes immediately but keep for future flexibility
    };

    // Snake state (sampled segments)
    struct SnakeState
    {
        float headX { 0 };
        float headY { 0 };
        std::uint32_t experience { 0 };

        std::uint16_t totalSegments { 0 };

        // We send N samples (max 12)
        std::uint8_t sampleCount { 0 };
        // samples follow: sf::Vector2f packed as 2 floats (8 bytes per sample)
    };

#pragma pack(pop)

    // ===================== ByteWriter / ByteReader =====================

    class ByteWriter
    {
        std::vector<std::uint8_t> data_;
    public:
        explicit ByteWriter(const std::size_t reserve = 0)
        {
            if (reserve) data_.reserve(reserve);
        }

        template<typename T>
        void WritePod(const T& v)
        {
            const std::size_t offset = data_.size();
            data_.resize(offset + sizeof(T));
            std::memcpy(data_.data() + offset, &v, sizeof(T));
        }

        void WriteBytes(std::span<const std::uint8_t> bytes)
        {
            const std::size_t offset = data_.size();
            data_.resize(offset + bytes.size());
            std::memcpy(data_.data() + offset, bytes.data(), bytes.size());
        }

        void WriteVector2f(const sf::Vector2f& v)
        {
            WritePod(v.x);
            WritePod(v.y);
        }

        [[nodiscard]] const std::vector<std::uint8_t>& Data() const { return data_; }
        [[nodiscard]] std::vector<std::uint8_t>& Data() { return data_; }
    };

    class ByteReader
    {
        std::span<const std::uint8_t> data_;
        std::size_t offset_ { 0 };
    public:
        explicit ByteReader(std::span<const std::uint8_t> data)
            : data_(data)
        {}

        template<typename T>
        bool ReadPod(T& out)
        {
            if (offset_ + sizeof(T) > data_.size()) return false;
            std::memcpy(&out, data_.data() + offset_, sizeof(T));
            offset_ += sizeof(T);
            return true;
        }

        bool ReadVector2f(sf::Vector2f& out)
        {
            return ReadPod(out.x) && ReadPod(out.y);
        }

        [[nodiscard]] bool End() const { return offset_ >= data_.size(); }
    };

    // ===================== Message helpers =====================

    inline std::vector<std::uint8_t> BuildMessage(const MessageType type,
                                                  const std::uint32_t seq,
                                                  const std::uint32_t frame,
                                                  const std::vector<std::uint8_t>& payload)
    {
        MessageHeader header;
        header.type = static_cast<std::uint16_t>(type);
        header.version = kNetVersion;
        header.seq = seq;
        header.frame = frame;
        header.payloadBytes = static_cast<std::uint32_t>(payload.size());

        std::vector<std::uint8_t> out;
        out.resize(sizeof(MessageHeader) + payload.size());

        std::memcpy(out.data(), &header, sizeof(MessageHeader));

        if (!payload.empty())
        {
            std::memcpy(out.data() + sizeof(MessageHeader), payload.data(), payload.size());
        }

        return out;
    }

    inline bool ParseHeader(std::span<const std::uint8_t> data, MessageHeader& outHeader)
    {
        if (data.size() < sizeof(MessageHeader)) return false;
        std::memcpy(&outHeader, data.data(), sizeof(MessageHeader));

        if (outHeader.version != kNetVersion) return false;
        if (data.size() != sizeof(MessageHeader) + outHeader.payloadBytes) return false;
        return true;
    }

} // namespace Utils::Legacy::Game::Net