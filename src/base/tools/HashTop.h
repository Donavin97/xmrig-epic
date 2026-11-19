/* XMRig
 * Copyright (c) 2026      xmrig-pcoin contributors
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 */

#ifndef XMRIG_HASHTOP_H
#define XMRIG_HASHTOP_H


#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>


namespace xmrig {


// Process-wide top-10 of per-hash achieved difficulty. Achieved difficulty is
// computed the same way as JobResult::actualDiff(): 0xFFFFFFFFFFFFFFFF / top64
// of the hash, where top64 is the native uint64 load at offset 24 of the 32-byte
// digest (little-endian on little-endian hosts). This makes the numbers directly
// comparable with the displayed job difficulty ("diff 338M").
//
// The hot path (CpuWorker) only pays one atomic load and compare per hash; the
// mutex is taken solely when a hash beats the current 10th best entry.
//
// Stats are process-local and are not persisted across restarts.
class HashTop
{
public:
    struct Entry {
        uint64_t diff;
        std::chrono::steady_clock::time_point time;
    };

    static inline void add(uint64_t value)
    {
        instance().addValue(value);
    }

    static inline uint64_t total()
    {
        return instance().m_total.load(std::memory_order_relaxed);
    }

    // Snapshot of the current top-10 (highest difficulty first) and the number of
    // valid entries.
    static inline size_t top(std::array<Entry, 10> &out)
    {
        std::lock_guard<std::mutex> lock(instance().m_mutex);
        out = instance().m_entries;
        return instance().m_count;
    }

private:
    HashTop() : m_entries{} {}

    static inline HashTop &instance()
    {
        static HashTop top;
        return top;
    }

    void addValue(uint64_t value)
    {
        m_total.fetch_add(1, std::memory_order_relaxed);

        const uint64_t diff = value ? (0xFFFFFFFFFFFFFFFFULL / value) : 0xFFFFFFFFFFFFFFFFULL;
        if (diff <= m_threshold.load(std::memory_order_relaxed)) {
            return;
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        if (diff <= m_threshold.load(std::memory_order_relaxed)) {
            return; // lost the race, another thread inserted a better entry
        }

        insertLocked(diff);
    }

    void insertLocked(uint64_t diff)
    {
        const auto now = std::chrono::steady_clock::now();

        if (m_count < m_entries.size()) {
            m_entries[m_count++] = { diff, now };
            sortLocked();
        }
        else if (diff > m_entries.back().diff) {
            m_entries.back() = { diff, now };
            sortLocked();
        }

        m_threshold.store(m_count < m_entries.size() ? 0 : m_entries.back().diff, std::memory_order_relaxed);
    }

    void sortLocked()
    {
        for (size_t i = 1; i < m_count; ++i) {
            const Entry e = m_entries[i];
            size_t j = i;
            while (j > 0 && m_entries[j - 1].diff < e.diff) {
                m_entries[j] = m_entries[j - 1];
                --j;
            }
            m_entries[j] = e;
        }
    }

    std::array<Entry, 10> m_entries;
    std::mutex m_mutex;
    std::atomic<uint64_t> m_threshold{ 0 };
    std::atomic<uint64_t> m_total{ 0 };
    size_t m_count = 0;
};


} // namespace xmrig


#endif // XMRIG_HASHTOP_H
