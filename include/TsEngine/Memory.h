#pragma once

#include "Core.h"
#include <cstring>
#include <vector>

namespace TsEngine {

class Memory {
public:
    explicit Memory(pid_t pid) : pid_(pid) {}

    [[nodiscard]] pid_t pid() const { return pid_; }

    bool readRaw(addr_t addr, void* buf, size_t size) const;
    bool writeRaw(addr_t addr, const void* buf, size_t size) const;

    template<typename T>
    std::optional<T> read(addr_t addr) const {
        T val{};
        if (!readRaw(addr, &val, sizeof(T))) return std::nullopt;
        return val;
    }

    template<typename T>
    bool write(addr_t addr, const T& val) const {
        return writeRaw(addr, &val, sizeof(T));
    }

    std::optional<std::vector<uint8_t>> readBuffer(addr_t addr, size_t size) const;
    std::optional<std::string> readString(addr_t addr, size_t maxLen = 256) const;
    std::string hexDump(addr_t addr, size_t size) const;

    // 指针扫描: 找内存中所有值 == targetAddr 的位置
    struct PointerResult {
        addr_t foundAt;
        addr_t pointsTo;
        std::string region;
    };

    std::vector<PointerResult> scanPointers(
        addr_t targetAddr,
        const std::vector<MemRegion>& regions,
        size_t maxResults = 10000) const;

private:
    pid_t pid_;
};

} // namespace TsEngine
