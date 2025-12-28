#include "udp_packet.hpp"

#include <cstring>

namespace Utils::Net::Udp {

    static std::uint32_t HeaderCrc(const PacketHeader& header)
    {
        PacketHeader temp = header;
        temp.headerCrc = 0;
        return Crc32(&temp, sizeof(PacketHeader));
    }

    std::vector<std::uint8_t> BuildPacket(const PacketHeader& header,
                                          const std::span<const std::uint8_t> payload)
    {
        PacketHeader h = header;

        h.payloadSize = static_cast<std::uint32_t>(payload.size());
        h.headerCrc = 0;
        h.payloadCrc = payload.empty() ? 0u : Crc32(payload.data(), payload.size());

        h.headerCrc = HeaderCrc(h);

        std::vector<std::uint8_t> out;
        out.resize(sizeof(PacketHeader) + payload.size());

        std::memcpy(out.data(), &h, sizeof(PacketHeader));

        if (!payload.empty())
        {
            std::memcpy(out.data() + sizeof(PacketHeader),
                        payload.data(),
                        payload.size());
        }

        return out;
    }

    ParsedPacket ParsePacket(const std::span<const std::uint8_t> datagram)
    {
        ParsedPacket result;

        if (datagram.size() < sizeof(PacketHeader))
        {
            return result;
        }

        std::memcpy(&result.header, datagram.data(), sizeof(PacketHeader));

        if (result.header.magic != kUdpMagic || result.header.version != kUdpVersion)
        {
            return result;
        }

        const std::size_t payloadOffset = sizeof(PacketHeader);

        if (payloadOffset + result.header.payloadSize > datagram.size())
        {
            return result;
        }

        result.payload = datagram.subspan(payloadOffset, result.header.payloadSize);

        const std::uint32_t headerCrc = result.header.headerCrc;

        if (HeaderCrc(result.header) != headerCrc)
        {
            return result;
        }

        if (result.header.payloadSize > 0)
        {
            const std::uint32_t payloadCrc = Crc32(result.payload.data(), result.payload.size());
            if (payloadCrc != result.header.payloadCrc)
            {
                return result;
            }
        }

        result.ok = true;
        return result;
    }

} // namespace Utils::Net::Udp