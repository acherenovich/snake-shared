#include "udp_reassembly.hpp"

#include <algorithm>
#include <cstring>

namespace Utils::Net::Udp {

    ReassemblyBuffer::ReassemblyBuffer(const ReassemblyConfig& config)
        : config_(config)
    {
    }

    bool ReassemblyBuffer::AddFragment(const std::uint32_t messageId,
                                       const std::uint16_t fragmentIndex,
                                       const std::uint16_t fragmentCount,
                                       const std::uint32_t totalSize,
                                       const std::uint8_t* const payload,
                                       const std::size_t payloadSize)
    {
        if (fragmentCount == 0 || fragmentIndex >= fragmentCount)
        {
            return false;
        }

        if (totalSize == 0 || totalSize > config_.maxMessageSize)
        {
            return false;
        }

        auto& state = messages_[messageId];

        if (state.buffer.empty())
        {
            state.createdAtMs = NowMs();
            state.totalSize = totalSize;
            state.fragmentCount = fragmentCount;
            state.buffer.resize(totalSize);
            state.received.assign(fragmentCount, false);
        }
        else
        {
            if (state.totalSize != totalSize || state.fragmentCount != fragmentCount)
            {
                messages_.erase(messageId);
                return false;
            }
        }

        if (state.received[fragmentIndex])
        {
            return false;
        }

        const std::size_t fragmentMax = (config_.maxMessageSize / fragmentCount) + 2048;
        (void)fragmentMax;

        const std::size_t chunkSize = (totalSize + fragmentCount - 1) / fragmentCount;
        const std::size_t offset = static_cast<std::size_t>(fragmentIndex) * chunkSize;

        if (offset >= state.buffer.size())
        {
            return false;
        }

        const std::size_t writable = std::min<std::size_t>(payloadSize,
                                                           state.buffer.size() - offset);

        std::memcpy(state.buffer.data() + offset, payload, writable);

        state.received[fragmentIndex] = true;
        state.receivedCount++;
        state.receivedBytes += writable;

        if (state.receivedCount == state.fragmentCount)
        {
            CompletedMessage msg;
            msg.messageId = messageId;
            msg.data = std::move(state.buffer);

            completed_.push_back(std::move(msg));
            messages_.erase(messageId);
            return true;
        }

        return false;
    }

    bool ReassemblyBuffer::HasCompleted() const
    {
        return !completed_.empty();
    }

    CompletedMessage ReassemblyBuffer::PopCompleted()
    {
        CompletedMessage msg = std::move(completed_.front());
        completed_.erase(completed_.begin());
        return msg;
    }

    void ReassemblyBuffer::TickCleanup()
    {
        const std::uint32_t now = NowMs();

        for (auto it = messages_.begin(); it != messages_.end(); )
        {
            const auto& state = it->second;
            if ((now - state.createdAtMs) > config_.timeoutMs)
            {
                it = messages_.erase(it);
                continue;
            }
            ++it;
        }
    }

} // namespace Utils::Net::Udp