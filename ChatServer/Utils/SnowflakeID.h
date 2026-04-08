#pragma once
#include <cstdint>
#include <chrono>
#include <mutex>
#include <iostream>

// ── Snowflake ID Generator ─────────────────────────────────────────────────
// 64-bit layout (Twitter style):
//
//   [ sign 1b | timestamp 41b (ms since epoch) | machine 10b | sequence 12b ]
//
// Capacity  : 4096 IDs/ms per node, 1024 nodes, ~69 years before overflow
// Custom epoch: 2024-01-01 00:00:00 UTC = 1704067200000 ms
//
// Usage:
//   // Once at startup (read machine_id from config):
//   SnowflakeID::GetInstance()->Init(machine_id);
//
//   // Generate an ID (thread-safe):
//   int64_t id = NewSnowflakeID();
// ──────────────────────────────────────────────────────────────────────────

class SnowflakeID {
public:
    static constexpr int64_t kEpoch          = 1704067200000LL; // 2024-01-01 UTC ms
    static constexpr int     kSeqBits        = 12;
    static constexpr int     kMachineBits    = 10;
    static constexpr int64_t kMaxSeq         = (1LL << kSeqBits)     - 1;   // 4095
    static constexpr int64_t kMaxMachineID   = (1LL << kMachineBits) - 1;   // 1023
    static constexpr int     kMachineShift   = kSeqBits;                     // 12
    static constexpr int     kTimestampShift = kMachineBits + kSeqBits;      // 22

    // Meyers singleton — same pattern as the rest of the codebase.
    static SnowflakeID* GetInstance() {
        static SnowflakeID inst;
        return &inst;
    }

    // Call once at startup (e.g. from main.cpp) with the node/worker ID (0–1023).
    // Out-of-range values are clamped to 1 with a warning.
    void Init(int64_t machine_id) {
        if (machine_id < 0 || machine_id > kMaxMachineID) {
            std::cerr << "[Snowflake] machine_id " << machine_id
                      << " out of range [0, 1023], falling back to 1\n";
            machine_id = 1;
        }
        _machine_id = machine_id;
        std::cout << "[Snowflake] Initialized with machine_id=" << machine_id << "\n";
    }

    // Generate the next unique 64-bit ID. Thread-safe.
    int64_t Next() {
        std::lock_guard<std::mutex> lk(_mtx);

        int64_t now = NowMs();

        // Guard against the (rare) case of clock moving backwards.
        if (now < _last_ms) now = _last_ms;

        if (now == _last_ms) {
            _seq = (_seq + 1) & kMaxSeq;
            if (_seq == 0) {
                // Sequence exhausted within this millisecond — spin until next ms.
                do { now = NowMs(); } while (now <= _last_ms);
            }
        } else {
            _seq = 0;
        }

        _last_ms = now;

        return ((now - kEpoch) << kTimestampShift)
             | (_machine_id   << kMachineShift)
             | _seq;
    }

    // Decode the UTC timestamp (ms) embedded in a snowflake ID.
    static int64_t ExtractTimestampMs(int64_t id) {
        return (id >> kTimestampShift) + kEpoch;
    }

private:
    SnowflakeID() = default;

    static int64_t NowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    }

    int64_t    _machine_id{1};
    int64_t    _last_ms{0};
    int64_t    _seq{0};
    std::mutex _mtx;
};

// Convenience free function — use this everywhere instead of the full call chain.
inline int64_t NewSnowflakeID() { return SnowflakeID::GetInstance()->Next(); }
