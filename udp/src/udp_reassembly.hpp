#pragma once

#include "udp_common.hpp"

#include <cstdint>
#include <vector>
#include <unordered_map>

namespace Utils::Net::Udp {

    struct ReassemblyConfig
    {
        std::size_t maxMessageSize { 256 * 1024 };
        std::uint32_t timeoutMs { 1500 };
    };

    struct CompletedMessage
    {
        std::uint32_t messageId { 0 };
        std::vector<std::uint8_t> data;
    };

    class ReassemblyBuffer
    {
    public:
        explicit ReassemblyBuffer(const ReassemblyConfig& config);

        // returns true if message completed
        bool AddFragment(std::uint32_t messageId,
                         std::uint16_t fragmentIndex,
                         std::uint16_t fragmentCount,
                         std::uint32_t totalSize,
                         const std::uint8_t* payload,
                         std::size_t payloadSize);

        bool HasCompleted() const;
        CompletedMessage PopCompleted();

        void TickCleanup();

    private:
        struct MessageState
        {
            std::uint32_t createdAtMs { 0 };
            std::uint32_t totalSize { 0 };
            std::uint16_t fragmentCount { 0 };

            std::vector<std::uint8_t> buffer;
            std::vector<bool> received;
            std::size_t receivedBytes { 0 };
            std::uint16_t receivedCount { 0 };
        };

        ReassemblyConfig config_;
        std::unordered_map<std::uint32_t, MessageState> messages_;
        std::vector<CompletedMessage> completed_;
    };

} // namespace Utils::Net::Udp