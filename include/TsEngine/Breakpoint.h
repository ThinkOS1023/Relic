#pragma once

#include "Core.h"
#include <unordered_map>

namespace TsEngine {

struct BreakpointInfo {
    addr_t address;
    uint32_t originalInst;
    bool enabled;
    uint32_t hitCount;
};

struct WatchpointInfo {
    addr_t address;
    size_t size;
    enum Mode { Write = 1, Read = 2, ReadWrite = 3 } mode;
    bool enabled;
    uint32_t hitCount;
    int slot;
};

struct RegState {
    uint64_t regs[31];
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
};

class Breakpoint {
public:
    explicit Breakpoint(pid_t pid) : pid_(pid) {}
    ~Breakpoint();

    bool ptraceAttach();   // 附加所有线程
    bool ptraceDetach();   // 分离所有线程
    [[nodiscard]] bool isAttached() const { return attached_; }

    bool add(addr_t addr, bool autoContinue = true);
    bool remove(addr_t addr);
    bool enable(addr_t addr);
    bool disable(addr_t addr);
    [[nodiscard]] const std::unordered_map<addr_t, BreakpointInfo>& list() const { return bps_; }

    bool watchAdd(addr_t addr, size_t size, WatchpointInfo::Mode mode);
    bool watchRemove(addr_t addr);
    [[nodiscard]] const std::unordered_map<addr_t, WatchpointInfo>& watchList() const { return wps_; }
    int maxWatchpoints() const;

    std::optional<addr_t> waitHit();
    std::optional<addr_t> pollHit();
    std::optional<RegState> getRegs() const;      // 主线程寄存器
    std::optional<RegState> getRegs(pid_t tid) const; // 指定线程寄存器
    bool setRegs(const RegState& state) const;
    bool continueExec();
    bool stopProcess();
    void resumeProcess(); // CONT + 设 running_
    [[nodiscard]] bool isRunning() const { return running_; }
    [[nodiscard]] const std::vector<pid_t>& threads() const { return tids_; }

private:
    bool writeInst(addr_t addr, uint32_t inst);
    std::optional<uint32_t> readInst(addr_t addr);
    bool singleStep();
    bool applyWatchpoints();       // 对所有线程设置观察点
    bool applyWatchpointsToTid(pid_t tid);
    bool clearHwWatchpoints();     // 清除所有线程的观察点
    bool clearHwWatchpointsForTid(pid_t tid);
    int findFreeWatchSlot();
    std::vector<pid_t> enumThreads();

    pid_t pid_;                    // 进程 PID (= 主线程 TID)
    std::vector<pid_t> tids_;     // 所有已附加的线程 TID
    bool attached_ = false;
    bool running_ = false;
    std::unordered_map<addr_t, BreakpointInfo> bps_;
    std::unordered_map<addr_t, WatchpointInfo> wps_;

    static constexpr uint32_t BRK_INST = 0xD4200000;
};

} // namespace TsEngine
