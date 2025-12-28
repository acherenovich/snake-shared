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

    enum class AddFragmentOutcome : std::uint8_t
    {
        Accepted = 0,
        Completed,
        Duplicate,
        Rejected
    };

    struct AddFragmentResult
    {
        AddFragmentOutcome outcome { AddFragmentOutcome::Rejected };
        const char* reason { "unknown" };
    };

    class ReassemblyBuffer
    {
    public:
        // Public to allow small free helper funcs in .cpp without friend boilerplate.
        struct FragmentInfo
        {
            std::uint32_t offset { 0 };
            std::uint32_t size { 0 };
            bool received { false };
        };

    public:
        explicit ReassemblyBuffer(const ReassemblyConfig& config);

        AddFragmentResult AddFragment(std::uint32_t messageId,
                                      std::uint16_t fragmentIndex,
                                      std::uint16_t fragmentCount,
                                      std::uint32_t totalSize,
                                      std::uint32_t fragmentOffset,
                                      std::uint32_t messageCrc,
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
            std::uint32_t messageCrc { 0 };

            std::vector<std::uint8_t> buffer;
            std::vector<FragmentInfo> fragments;

            std::uint16_t receivedCount { 0 };
        };

        ReassemblyConfig config_;
        std::unordered_map<std::uint32_t, MessageState> messages_;
        std::vector<CompletedMessage> completed_;
    };

} // namespace Utils::Net::Udp