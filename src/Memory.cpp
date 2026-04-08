#include "TsEngine/Memory.h"

#include <sys/uio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <cstdio>
#include <sstream>
#include <iomanip>
#include <thread>
#include <mutex>
#include <atomic>

namespace TsEngine {

bool Memory::readRaw(addr_t addr, void* buf, size_t size) const {
    struct iovec local  = { buf, size };
    struct iovec remote = { reinterpret_cast<void*>(addr), size };
    ssize_t ret = syscall(__NR_process_vm_readv, pid_, &local, 1, &remote, 1, 0);
    return ret == static_cast<ssize_t>(size);
}

bool Memory::writeRaw(addr_t addr, const void* buf, size_t size) const {
    struct iovec local  = { const_cast<void*>(buf), size };
    struct iovec remote = { reinterpret_cast<void*>(addr), size };
    ssize_t ret = syscall(__NR_process_vm_writev, pid_, &local, 1, &remote, 1, 0);
    return ret == static_cast<ssize_t>(size);
}

std::optional<std::vector<uint8_t>> Memory::readBuffer(addr_t addr, size_t size) const {
    std::vector<uint8_t> buf(size);
    if (!readRaw(addr, buf.data(), size)) return std::nullopt;
    return buf;
}

std::optional<std::string> Memory::readString(addr_t addr, size_t maxLen) const {
    std::string result;
    result.reserve(64);
    constexpr size_t CHUNK = 64;
    char chunk[CHUNK];
    size_t totalRead = 0;
    while (totalRead < maxLen) {
        size_t toRead = std::min(CHUNK, maxLen - totalRead);
        if (!readRaw(addr + totalRead, chunk, toRead))
            return result.empty() ? std::nullopt : std::optional(result);
        for (size_t i = 0; i < toRead; i++) {
            if (chunk[i] == '\0') return result;
            result += chunk[i];
        }
        totalRead += toRead;
    }
    return result.empty() ? std::nullopt : std::optional(result);
}

std::string Memory::hexDump(addr_t addr, size_t size) const {
    auto data = readBuffer(addr, size);
    if (!data) return "[读取失败]";
    std::ostringstream oss;
    const auto& buf = *data;
    for (size_t i = 0; i < buf.size(); i += 16) {
        oss << std::hex << std::setw(16) << std::setfill('0') << (addr + i) << "  ";
        for (size_t j = 0; j < 16; j++) {
            if (j == 8) oss << ' ';
            if (i + j < buf.size())
                oss << std::hex << std::setw(2) << std::setfill('0')
                    << static_cast<unsigned>(buf[i + j]) << ' ';
            else oss << "   ";
        }
        oss << " |";
        for (size_t j = 0; j < 16 && (i + j) < buf.size(); j++) {
            char c = static_cast<char>(buf[i + j]);
            oss << (c >= 32 && c < 127 ? c : '.');
        }
        oss << "|\n";
    }
    return oss.str();
}

// ── 多线程指针扫描 ──

static std::vector<MemRegion> filterRegions(const std::vector<MemRegion>& regions) {
    std::vector<MemRegion> out;
    for (const auto& r : regions) {
        if (!r.readable()) continue;
        if (r.size() > 512UL * 1024 * 1024) continue;
        if (r.path.find("[vdso]") != std::string::npos) continue;
        if (r.path.find("[vectors]") != std::string::npos) continue;
        out.push_back(r);
    }
    return out;
}

static void scanWorker(
    const Memory& mem,
    const std::vector<MemRegion>& regions,
    addr_t target,
    size_t maxResults,
    std::vector<Memory::PointerResult>& results,
    std::mutex& mtx,
    std::atomic<size_t>& totalFound)
{
    constexpr size_t BLOCK = 4096 * 16;
    std::vector<uint8_t> buf(BLOCK);
    std::vector<Memory::PointerResult> local;

    for (const auto& r : regions) {
        for (addr_t cur = r.start; cur < r.end; cur += BLOCK) {
            if (totalFound.load() >= maxResults) return;
            size_t chunkSize = std::min(static_cast<size_t>(r.end - cur), BLOCK);
            if (!mem.readRaw(cur, buf.data(), chunkSize)) continue;

            size_t startOff = (cur % 8 != 0) ? (8 - cur % 8) : 0;
            for (size_t off = startOff; off + 8 <= chunkSize; off += 8) {
                addr_t val;
                std::memcpy(&val, buf.data() + off, 8);
                // 匹配: 原始值或去标签后的值
                if (untag(val) == target || val == target) {
                    local.push_back({cur + off, val, r.path});
                    if (local.size() >= 64) {
                        std::lock_guard<std::mutex> lk(mtx);
                        for (auto& p : local) results.push_back(std::move(p));
                        totalFound += local.size();
                        local.clear();
                        if (totalFound.load() >= maxResults) return;
                    }
                }
            }
        }
    }
    if (!local.empty()) {
        std::lock_guard<std::mutex> lk(mtx);
        for (auto& p : local) results.push_back(std::move(p));
        totalFound += local.size();
    }
}

std::vector<Memory::PointerResult> Memory::scanPointers(
    addr_t targetAddr, const std::vector<MemRegion>& regions, size_t maxResults) const
{
    addr_t clean = untag(targetAddr);
    auto filtered = filterRegions(regions);
    if (filtered.empty()) return {};

    unsigned nThreads = std::min(std::max(1u, std::thread::hardware_concurrency()), 8u);
    if (filtered.size() < nThreads) nThreads = filtered.size();

    std::vector<std::vector<MemRegion>> perThread(nThreads);
    for (size_t i = 0; i < filtered.size(); i++) perThread[i % nThreads].push_back(filtered[i]);

    std::vector<PointerResult> results;
    std::mutex mtx;
    std::atomic<size_t> totalFound{0};

    std::vector<std::thread> threads;
    for (unsigned i = 0; i < nThreads; i++)
        threads.emplace_back(scanWorker, std::cref(*this), std::cref(perThread[i]),
            clean, maxResults, std::ref(results), std::ref(mtx), std::ref(totalFound));
    for (auto& t : threads) t.join();

    if (results.size() > maxResults) results.resize(maxResults);
    return results;
}

} // namespace TsEngine
