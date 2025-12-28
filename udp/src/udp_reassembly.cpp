#include "udp_reassembly.hpp"

#include <algorithm>
#include <cstring>

namespace Utils::Net::Udp {

    ReassemblyBuffer::ReassemblyBuffer(const ReassemblyConfig& config)
        : config_(config)
    {
    }

    static bool ValidateCoverage(const std::vector<ReassemblyBuffer::FragmentInfo>& frags,
                                 const std::uint32_t totalSize)
    {
        struct Range
        {
            std::uint32_t off;
            std::uint32_t size;
        };

        std::vector<Range> ranges;
        ranges.reserve(frags.size());

        for (const auto& f : frags)
        {
            if (!f.received)
            {
                return false;
            }

            ranges.push_back(Range{ f.offset, f.size });
        }

        std::sort(ranges.begin(), ranges.end(), [](const Range& a, const Range& b)
        {
            return a.off < b.off;
        });

        if (ranges.empty())
        {
            return false;
        }

        if (ranges.front().off != 0)
        {
            return false;
        }

        std::uint64_t end = static_cast<std::uint64_t>(ranges.front().off) +
                            static_cast<std::uint64_t>(ranges.front().size);

        for (std::size_t i = 1; i < ranges.size(); ++i)
        {
            const std::uint64_t expectedOff = end;
            if (static_cast<std::uint64_t>(ranges[i].off) != expectedOff)
            {
                return false; // gap or overlap
            }

            end = static_cast<std::uint64_t>(ranges[i].off) +
                  static_cast<std::uint64_t>(ranges[i].size);
        }

        return end == static_cast<std::uint64_t>(totalSize);
    }

    AddFragmentResult ReassemblyBuffer::AddFragment(const std::uint32_t messageId,
                                                    const std::uint16_t fragmentIndex,
                                                    const std::uint16_t fragmentCount,
                                                    const std::uint32_t totalSize,
                                                    const std::uint32_t fragmentOffset,
                                                    const std::uint32_t messageCrc,
                                                    const std::uint8_t* const payload,
                                                    const std::size_t payloadSize)
    {
        if (fragmentCount == 0 || fragmentIndex >= fragmentCount)
        {
            return { AddFragmentOutcome::Rejected, "bad_fragment_index_or_count" };
        }

        if (totalSize == 0 || totalSize > config_.maxMessageSize)
        {
            return { AddFragmentOutcome::Rejected, "bad_total_size" };
        }

        if (fragmentOffset > totalSize)
        {
            return { AddFragmentOutcome::Rejected, "bad_fragment_offset" };
        }

        if (static_cast<std::uint64_t>(fragmentOffset) + static_cast<std::uint64_t>(payloadSize) >
            static_cast<std::uint64_t>(totalSize))
        {
            return { AddFragmentOutcome::Rejected, "fragment_out_of_bounds" };
        }

        auto& state = messages_[messageId];

        if (state.buffer.empty())
        {
            state.createdAtMs = NowMs();
            state.totalSize = totalSize;
            state.fragmentCount = fragmentCount;
            state.messageCrc = messageCrc;

            state.buffer.resize(totalSize);
            state.fragments.assign(fragmentCount, FragmentInfo{});
        }
        else
        {
            if (state.totalSize != totalSize || state.fragmentCount != fragmentCount || state.messageCrc != messageCrc)
            {
                messages_.erase(messageId);
                return { AddFragmentOutcome::Rejected, "message_params_mismatch" };
            }
        }

        if (state.fragments[fragmentIndex].received)
        {
            return { AddFragmentOutcome::Duplicate, "duplicate_fragment" };
        }

        // Copy payload at exact offset
        std::memcpy(state.buffer.data() + fragmentOffset, payload, payloadSize);

        state.fragments[fragmentIndex].received = true;
        state.fragments[fragmentIndex].offset = fragmentOffset;
        state.fragments[fragmentIndex].size = static_cast<std::uint32_t>(payloadSize);

        state.receivedCount++;

        if (state.receivedCount == state.fragmentCount)
        {
            // Validate coverage (no gaps/overlaps)
            if (!ValidateCoverage(state.fragments, state.totalSize))
            {
                messages_.erase(messageId);
                return { AddFragmentOutcome::Rejected, "coverage_invalid_gap_or_overlap" };
            }

            // Validate whole-message CRC
            const std::uint32_t crc = state.totalSize == 0 ? 0u : Crc32(state.buffer.data(), state.buffer.size());
            if (crc != state.messageCrc)
            {
                messages_.erase(messageId);
                return { AddFragmentOutcome::Rejected, "message_crc_mismatch" };
            }

            CompletedMessage msg;
            msg.messageId = messageId;
            msg.data = std::move(state.buffer);

            completed_.push_back(std::move(msg));
            messages_.erase(messageId);

            return { AddFragmentOutcome::Completed, "ok" };
        }

        return { AddFragmentOutcome::Accepted, "ok" };
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