#pragma once
#include <atomic>
#include <cstdint>

// Workers may run concurrently. Configuration writes require both workers to
// leave their current iteration; HTTP returns busy instead of blocking AsyncTCP.
namespace RuntimeAccess {
inline std::atomic<uint32_t>& state() { static std::atomic<uint32_t> value{0}; return value; }
class Read {
    bool acquired = false;
public:
    Read() {
        auto current = state().load();
        while (!(current & 0x80000000U)) {
            if (state().compare_exchange_weak(current, current + 1)) { acquired = true; break; }
        }
    }
    ~Read() { if (acquired) --state(); }
    explicit operator bool() const { return acquired; }
    Read(const Read&) = delete;
    Read& operator=(const Read&) = delete;
};
class Write {
    bool acquired;
public:
    Write() { uint32_t expected = 0; acquired = state().compare_exchange_strong(expected, 0x80000000U); }
    ~Write() { if (acquired) state() = 0; }
    explicit operator bool() const { return acquired; }
    Write(const Write&) = delete;
    Write& operator=(const Write&) = delete;
};
// Upgrade the OBD worker's existing read guard only when it is the sole
// reader. A failed upgrade defers saving the selected adapter to the next tick.
class Upgrade {
    bool acquired;
public:
    Upgrade() { uint32_t expected = 1; acquired = state().compare_exchange_strong(expected, 0x80000000U); }
    ~Upgrade() { if (acquired) state() = 1; }
    explicit operator bool() const { return acquired; }
    Upgrade(const Upgrade&) = delete;
    Upgrade& operator=(const Upgrade&) = delete;
};
}
