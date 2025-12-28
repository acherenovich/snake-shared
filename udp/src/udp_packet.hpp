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

        std::uint32_t totalSize { 0 };      // whole message
        std::uint32_t payloadSize { 0 };    // in this packet

        std::uint32_t headerCrc { 0 };      // crc of header except this field
        std::uint32_t payloadCrc { 0 };     // crc of payload
    };

#pragma pack(pop)

    static_assert(sizeof(PacketHeader) == 4 + 2 + 1 + 8 + 4 + 2 + 2 + 4 + 4 + 4 + 4);

    constexpr std::size_t HeaderSize = sizeof(PacketHeader);

    struct ParsedPacket
    {
        PacketHeader header {};
        std::span<const std::uint8_t> payload {};
        bool ok { false };
    };

    std::vector<std::uint8_t> BuildPacket(const PacketHeader& header,
                                          std::span<const std::uint8_t> payload);

    ParsedPacket ParsePacket(std::span<const std::uint8_t> datagram);

} // namespace Utils::Net::Udp