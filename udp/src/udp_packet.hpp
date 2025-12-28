#pragma once

#include "udp_common.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>
#include <span>

namespace Utils::Net::Udp {

#pragma pack(push, 1)

    struct PacketHeader
    {
        std::uint32_t magic { kUdpMagic };
        std::uint16_t version { kUdpVersion };
        PacketType type { PacketType::DataFragment };

        std::uint64_t sessionId { 0 };

        std::uint32_t messageId { 0 };

        std::uint16_t fragmentIndex { 0 };
        std::uint16_t fragmentCount { 0 };

        std::uint32_t totalSize { 0 };      // whole message size (bytes)
        std::uint32_t payloadSize { 0 };    // in THIS datagram

        // NEW (v2): exact offset in whole message where this payload chunk starts
        std::uint32_t fragmentOffset { 0 };

        // NEW (v2): CRC32 of the WHOLE message payload (same for every fragment)
        std::uint32_t messageCrc { 0 };

        std::uint32_t headerCrc { 0 };      // crc of header except this field
        std::uint32_t payloadCrc { 0 };     // crc of THIS fragment payload
    };

#pragma pack(pop)

    static_assert(sizeof(PacketHeader) ==
        4 + 2 + 1 +
        8 +
        4 +
        2 + 2 +
        4 + 4 +
        4 + 4 +
        4 + 4);

    constexpr std::size_t HeaderSize = sizeof(PacketHeader);

    enum class PacketRejectReason : std::uint8_t
    {
        None = 0,
        TooSmall,
        BadMagicVersion,
        SizeMismatch,
        HeaderCrcMismatch,
        PayloadCrcMismatch,
        InvalidFragmentFields
    };

    inline const char* PacketRejectReasonToString(const PacketRejectReason r)
    {
        switch (r)
        {
            case PacketRejectReason::None:                return "none";
            case PacketRejectReason::TooSmall:            return "too_small";
            case PacketRejectReason::BadMagicVersion:     return "bad_magic_or_version";
            case PacketRejectReason::SizeMismatch:        return "payload_size_mismatch";
            case PacketRejectReason::HeaderCrcMismatch:   return "header_crc_mismatch";
            case PacketRejectReason::PayloadCrcMismatch:  return "payload_crc_mismatch";
            case PacketRejectReason::InvalidFragmentFields:return "invalid_fragment_fields";
        }
        return "unknown";
    }

    struct ParsedPacket
    {
        PacketHeader header {};
        std::span<const std::uint8_t> payload {};
        bool ok { false };

        PacketRejectReason reject { PacketRejectReason::None };
    };

    std::vector<std::uint8_t> BuildPacket(const PacketHeader& header,
                                          std::span<const std::uint8_t> payload);

    ParsedPacket ParsePacket(std::span<const std::uint8_t> datagram);

} // namespace Utils::Net::Udp