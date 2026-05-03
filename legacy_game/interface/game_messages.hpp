#pragma once

#include <cstdint>
#include <vector>
#include <string_view>
#include <cstring>
#include <span>
#include <SFML/System/Vector2.hpp>

namespace Utils::Legacy::Game::Net {

    // bumped due to protocol changes (CRC + snake points kind + 16-bit pointsCount + snapshot messages + snake color)
    constexpr std::uint16_t kNetVersion = 3;

    enum class MessageType : std::uint16_t
    {
        // client -> server
        ClientInput          = 10,
        RequestFullUpdate    = 11,
        RequestSnakeSnapshot = 12,

        // server -> client
        FullUpdate    = 100,
        PartialUpdate = 101,
        SnakeSnapshot = 102,
    };

    enum class EntityType : std::uint8_t
    {
        Snake = 1,
        Food  = 2
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

    enum class SnakePointsKind : std::uint8_t
    {
        ValidationSamples = 1, // partial updates: radius-based samples for drift validation only
        FullSegments      = 2, // full snapshot: every segment in correct order
    };

#pragma pack(push, 1)
    struct FullUpdateHeader
    {
        std::uint32_t playerEntityID { 0 };
    };

    struct MessageHeader
    {
        std::uint16_t type { 0 };
        std::uint16_t version { kNetVersion };

        std::uint32_t seq { 0 };     // server update seq OR client input seq
        std::uint32_t frame { 0 };   // server frame (for updates)

        std::uint32_t payloadBytes { 0 };
        std::uint32_t payloadCrc32 { 0 }; // CRC32 of payload bytes (end-to-end, after UDP reassembly)
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

    struct FoodState
    {
        float x { 0 };
        float y { 0 };

        std::uint8_t power { 1 };
        Color color {};

        std::uint8_t killed { 0 };
    };

    // Snake state (followed by pointsCount * Vector2f)
    struct SnakeState
    {
        float headX { 0 };
        float headY { 0 };
        std::uint32_t experience { 0 };

        std::uint16_t totalSegments { 0 };

        SnakePointsKind pointsKind { SnakePointsKind::ValidationSamples };
        std::uint16_t pointsCount { 0 };
        Color color {};
        // points follow: x(float), y(float) repeated pointsCount times
    };

    struct RequestFullUpdatePayload
    {
        std::uint8_t flags { 0 };
    };

    static constexpr std::uint8_t RequestFullUpdateFlag_AllSegments = 1 << 0;

    struct RequestSnakeSnapshotPayload
    {
        std::uint32_t entityID { 0 };
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
        [[nodiscard]] std::size_t Remaining() const { return (offset_ <= data_.size()) ? (data_.size() - offset_) : 0; }
    };

    // ===================== CRC32 =====================

    inline std::uint32_t Crc32(std::span<const std::uint8_t> data)
    {
        static std::uint32_t table[256];
        static bool tableInit = false;

        if (!tableInit)
        {
            for (std::uint32_t i = 0; i < 256; ++i)
            {
                std::uint32_t c = i;
                for (std::uint32_t k = 0; k < 8; ++k)
                {
                    if (c & 1u)
                        c = 0xEDB88320u ^ (c >> 1u);
                    else
                        c >>= 1u;
                }
                table[i] = c;
            }
            tableInit = true;
        }

        std::uint32_t crc = 0xFFFFFFFFu;
        for (const auto b : data)
        {
            crc = table[(crc ^ b) & 0xFFu] ^ (crc >> 8u);
        }
        return crc ^ 0xFFFFFFFFu;
    }

    // ===================== Message helpers =====================

    enum class ParseError : std::uint8_t
    {
        Ok = 0,
        TooSmall,
        BadVersion,
        SizeMismatch,
        CrcMismatch,
    };

    inline std::string_view ParseErrorToString(const ParseError err)
    {
        switch (err)
        {
            case ParseError::Ok:          return "Ok";
            case ParseError::TooSmall:    return "TooSmall";
            case ParseError::BadVersion:  return "BadVersion";
            case ParseError::SizeMismatch:return "SizeMismatch";
            case ParseError::CrcMismatch: return "CrcMismatch";
        }
        return "Unknown";
    }

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
        header.payloadCrc32 = payload.empty() ? 0u : Crc32(std::span<const std::uint8_t>(payload.data(), payload.size()));

        std::vector<std::uint8_t> out;
        out.resize(sizeof(MessageHeader) + payload.size());

        std::memcpy(out.data(), &header, sizeof(MessageHeader));

        if (!payload.empty())
        {
            std::memcpy(out.data() + sizeof(MessageHeader), payload.data(), payload.size());
        }

        return out;
    }

    inline ParseError ParseHeaderDetailed(std::span<const std::uint8_t> data, MessageHeader& outHeader)
    {
        if (data.size() < sizeof(MessageHeader))
        {
            return ParseError::TooSmall;
        }

        std::memcpy(&outHeader, data.data(), sizeof(MessageHeader));

        if (outHeader.version != kNetVersion)
        {
            return ParseError::BadVersion;
        }

        const std::size_t expectedSize = sizeof(MessageHeader) + static_cast<std::size_t>(outHeader.payloadBytes);
        if (data.size() != expectedSize)
        {
            return ParseError::SizeMismatch;
        }

        if (outHeader.payloadBytes > 0)
        {
            const auto payload = std::span<const std::uint8_t>(data.data() + sizeof(MessageHeader), outHeader.payloadBytes);
            const auto crc = Crc32(payload);
            if (crc != outHeader.payloadCrc32)
            {
                return ParseError::CrcMismatch;
            }
        }
        else
        {
            if (outHeader.payloadCrc32 != 0u)
            {
                return ParseError::CrcMismatch;
            }
        }

        return ParseError::Ok;
    }

    inline void WriteFullUpdateHeader(ByteWriter& w, const std::uint32_t playerEntityID)
    {
        FullUpdateHeader fh;
        fh.playerEntityID = playerEntityID;
        w.WritePod(fh);
    }

    inline bool ReadFullUpdateHeader(ByteReader& r, FullUpdateHeader& out)
    {
        return r.ReadPod(out);
    }

    inline bool ReadRequestFullUpdatePayload(ByteReader& r, RequestFullUpdatePayload& out)
    {
        // Backward compatible: old clients can send empty payload
        if (r.End())
        {
            out.flags = 0;
            return true;
        }

        return r.ReadPod(out);
    }

    inline bool ReadRequestSnakeSnapshotPayload(ByteReader& r, RequestSnakeSnapshotPayload& out)
    {
        return r.ReadPod(out);
    }

} // namespace Utils::Legacy::Game::Net