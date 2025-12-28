#pragma once

#include <cstdint>
#include <vector>
#include <string>
#include <string_view>
#include <chrono>

namespace Utils::Net::Udp {

    constexpr std::uint32_t kUdpMagic = 0x534E4B55; // "S N K U"
    constexpr std::uint16_t kUdpVersion = 2;        // IMPORTANT: bumped (protocol changed)

    enum class PacketType : std::uint8_t
    {
        HandshakeHello = 1,
        HandshakeWelcome = 2,

        DataFragment = 10
    };

    inline std::uint32_t NowMs()
    {
        using namespace std::chrono;
        return static_cast<std::uint32_t>(
            duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
    }

    // Simple CRC32 (software, fast enough for 256KB)
    std::uint32_t Crc32(const void* data, std::size_t size);

} // namespace Utils::Net::Udp