#include "TsEngine/Breakpoint.h"

#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <linux/elf.h>
#include <signal.h>
#include <cstring>
#include <cerrno>
#include <cstddef>

#ifndef NT_ARM_HW_WATCH
#define NT_ARM_HW_WATCH 0x403
#endif

struct hw_debug_state {
    uint32_t dbg_info;
    uint32_t pad;
    struct {
        uint64_t addr;
        uint32_t ctrl;
        uint32_t pad;
    } dbg_regs[16];
};

namespace TsEngine {

Breakpoint::~Breakpoint() {
    if (!attached_) return;
    // 检查进程是否还活着
    if (kill(pid_, 0) != 0) { attached_ = false; return; }

    stopProcess();

    for (auto& [addr, bp] : bps_) {
        if (bp.enabled) writeInst(addr, bp.originalInst);
    }
    if (!wps_.empty()) { wps_.clear(); clearHwWatchpoints(); }
    ptrace(PTRACE_DETACH, pid_, nullptr, nullptr);
    attached_ = false;
}

bool Breakpoint::ptraceAttach() {
    if (attached_) return true;
    if (ptrace(PTRACE_ATTACH, pid_, nullptr, nullptr) < 0) return false;

    int status;
    if (waitpid(pid_, &status, 0) < 0) return false;
    if (!WIFSTOPPED(status)) {
        ptrace(PTRACE_DETACH, pid_, nullptr, nullptr);
        return false;
    }

    attached_ = true;
    running_ = false;
    return true;
}

bool Breakpoint::ptraceDetach() {
    if (!attached_) return true;

    for (auto& [addr, bp] : bps_) {
        if (bp.enabled) writeInst(addr, bp.originalInst);
    }
    bps_.clear();
    if (!wps_.empty()) { wps_.clear(); clearHwWatchpoints(); }

    ptrace(PTRACE_DETACH, pid_, nullptr, nullptr);
    attached_ = false;
    running_ = false;
    return true;
}

// ── 指令读写 ──

bool Breakpoint::writeInst(addr_t addr, uint32_t inst) {
    errno = 0;
    long word = ptrace(PTRACE_PEEKTEXT, pid_, reinterpret_cast<void*>(addr & ~7UL), nullptr);
    if (errno != 0) return false;

    int off = addr & 7;
    std::memcpy(reinterpret_cast<char*>(&word) + off, &inst, 4);

    return ptrace(PTRACE_POKETEXT, pid_, reinterpret_cast<void*>(addr & ~7UL),
                  reinterpret_cast<void*>(word)) >= 0;
}

std::optional<uint32_t> Breakpoint::readInst(addr_t addr) {
    errno = 0;
    long word = ptrace(PTRACE_PEEKTEXT, pid_, reinterpret_cast<void*>(addr & ~7UL), nullptr);
    if (errno != 0) return std::nullopt;

    uint32_t inst;
    std::memcpy(&inst, reinterpret_cast<char*>(&word) + (addr & 7), 4);
    return inst;
}

// ── 软件断点 ──

bool Breakpoint::add(addr_t addr, bool autoContinue) {
    if (bps_.count(addr)) return false;
    if (!attached_ && !ptraceAttach()) return false;

    auto orig = readInst(addr);
    if (!orig) return false;
    if (!writeInst(addr, BRK_INST)) return false;

    bps_[addr] = { .address = addr, .originalInst = *orig, .enabled = true, .hitCount = 0 };

    if (autoContinue) {
        running_ = true;
        ptrace(PTRACE_CONT, pid_, nullptr, nullptr);
    }
    return true;
}

bool Breakpoint::remove(addr_t addr) {
    auto it = bps_.find(addr);
    if (it == bps_.end()) return false;
    if (it->second.enabled) writeInst(addr, it->second.originalInst);
    bps_.erase(it);
    return true;
}

bool Breakpoint::enable(addr_t addr) {
    auto it = bps_.find(addr);
    if (it == bps_.end() || it->second.enabled) return false;
    if (!writeInst(addr, BRK_INST)) return false;
    it->second.enabled = true;
    return true;
}

bool Breakpoint::disable(addr_t addr) {
    auto it = bps_.find(addr);
    if (it == bps_.end() || !it->second.enabled) return false;
    if (!writeInst(addr, it->second.originalInst)) return false;
    it->second.enabled = false;
    return true;
}

// ── 命中等待 ──

std::optional<addr_t> Breakpoint::waitHit() {
    if (!running_) {
        if (ptrace(PTRACE_CONT, pid_, nullptr, nullptr) < 0) return std::nullopt;
    }

    int status;
    if (waitpid(pid_, &status, 0) < 0) return std::nullopt;
    running_ = false;

    if (WIFEXITED(status) || WIFSIGNALED(status)) return std::nullopt;
    if (!WIFSTOPPED(status)) return std::nullopt;

    int sig = WSTOPSIG(status);
    if (sig != SIGTRAP) {
        ptrace(PTRACE_CONT, pid_, nullptr, reinterpret_cast<void*>(sig));
        running_ = true;
        return waitHit();
    }

    auto regs = getRegs();
    if (!regs) return std::nullopt;

    addr_t hitAddr = regs->pc;
    auto bpIt = bps_.find(hitAddr);
    if (bpIt != bps_.end()) bpIt->second.hitCount++;
    for (auto& [_, wp] : wps_) wp.hitCount++;

    return hitAddr;
}

std::optional<addr_t> Breakpoint::pollHit() {
    if (!attached_) return std::nullopt;
    if (!running_) return std::nullopt;  // 进程停着, 不轮询
    if (wps_.empty() && bps_.empty()) return std::nullopt; // 没有监控点

    int status;
    pid_t ret = waitpid(pid_, &status, WNOHANG);
    if (ret <= 0) return std::nullopt;
    running_ = false;

    if (WIFEXITED(status) || WIFSIGNALED(status)) return std::nullopt;
    if (!WIFSTOPPED(status)) return std::nullopt;

    int sig = WSTOPSIG(status);
    if (sig != SIGTRAP) {
        ptrace(PTRACE_CONT, pid_, nullptr, reinterpret_cast<void*>(sig));
        running_ = true;
        return std::nullopt;
    }

    auto regs = getRegs();
    if (!regs) return std::nullopt;

    addr_t hitAddr = regs->pc;
    auto bpIt = bps_.find(hitAddr);
    if (bpIt != bps_.end()) bpIt->second.hitCount++;
    for (auto& [_, wp] : wps_) wp.hitCount++;

    return hitAddr;
}

// ── 寄存器 ──

std::optional<RegState> Breakpoint::getRegs() const {
    if (!attached_) return std::nullopt;

    struct { uint64_t regs[31]; uint64_t sp, pc, pstate; } gpr{};
    struct iovec iov = { &gpr, sizeof(gpr) };
    if (ptrace(PTRACE_GETREGSET, pid_, reinterpret_cast<void*>(NT_PRSTATUS), &iov) < 0)
        return std::nullopt;

    RegState state{};
    std::memcpy(state.regs, gpr.regs, sizeof(state.regs));
    state.sp = gpr.sp; state.pc = gpr.pc; state.pstate = gpr.pstate;
    return state;
}

bool Breakpoint::setRegs(const RegState& state) const {
    struct { uint64_t regs[31]; uint64_t sp, pc, pstate; } gpr{};
    std::memcpy(gpr.regs, state.regs, sizeof(gpr.regs));
    gpr.sp = state.sp; gpr.pc = state.pc; gpr.pstate = state.pstate;
    struct iovec iov = { &gpr, sizeof(gpr) };
    return ptrace(PTRACE_SETREGSET, pid_, reinterpret_cast<void*>(NT_PRSTATUS), &iov) >= 0;
}

// ── 停止 + 单步 + 继续 ──

bool Breakpoint::stopProcess() {
    if (!attached_) return false;
    if (!running_) return true;

    running_ = false;

    if (kill(pid_, SIGSTOP) < 0) return false;

    for (int i = 0; i < 20; i++) {
        int st;
        if (waitpid(pid_, &st, 0) < 0) return false;
        if (WIFEXITED(st) || WIFSIGNALED(st)) return false;
        if (!WIFSTOPPED(st)) continue;

        int sig = WSTOPSIG(st);
        if (sig == SIGSTOP || sig == SIGTRAP) return true;

        ptrace(PTRACE_CONT, pid_, nullptr, reinterpret_cast<void*>(sig));
    }
    return false;
}

void Breakpoint::resumeProcess() {
    if (!attached_ || running_) return;
    ptrace(PTRACE_CONT, pid_, nullptr, nullptr);
    running_ = true;
}

bool Breakpoint::singleStep() {
    if (ptrace(PTRACE_SINGLESTEP, pid_, nullptr, nullptr) < 0) {
        auto r = getRegs();
        if (!r) return false;
        RegState s = *r;
        s.pc += 4;
        return setRegs(s);
    }
    int status;
    if (waitpid(pid_, &status, 0) < 0) return false;
    return WIFSTOPPED(status);
}

bool Breakpoint::continueExec() {
    auto regs = getRegs();
    if (!regs) return false;

    addr_t pc = regs->pc;
    bool hasWp = !wps_.empty();

    // 软件断点: 恢复 → 单步 → 重设
    auto bpIt = bps_.find(pc);
    if (bpIt != bps_.end() && bpIt->second.enabled) {
        if (hasWp) clearHwWatchpoints();
        writeInst(pc, bpIt->second.originalInst);
        singleStep();
        writeInst(pc, BRK_INST);
        if (hasWp) applyWatchpoints();
    }
    // 观察点触发: 禁用 → 单步 → 重启
    else if (hasWp) {
        clearHwWatchpoints();
        singleStep();
        applyWatchpoints();
    }

    running_ = true;
    return ptrace(PTRACE_CONT, pid_, nullptr, nullptr) >= 0;
}

// ── 硬件观察点 ──

int Breakpoint::maxWatchpoints() const {
    if (!attached_) return 0;
    hw_debug_state state{};
    struct iovec iov = { &state, sizeof(state) };
    if (ptrace(PTRACE_GETREGSET, pid_, reinterpret_cast<void*>(NT_ARM_HW_WATCH), &iov) < 0)
        return 0;
    return state.dbg_info & 0xFF;
}

int Breakpoint::findFreeWatchSlot() {
    int max = maxWatchpoints();
    for (int i = 0; i < max; i++) {
        bool used = false;
        for (const auto& [_, wp] : wps_) {
            if (wp.slot == i) { used = true; break; }
        }
        if (!used) return i;
    }
    return -1;
}

bool Breakpoint::clearHwWatchpoints() {
    hw_debug_state state{};
    struct iovec iov = { &state, sizeof(state) };
    if (ptrace(PTRACE_GETREGSET, pid_, reinterpret_cast<void*>(NT_ARM_HW_WATCH), &iov) < 0)
        return false;

    int max = state.dbg_info & 0xFF;
    for (int i = 0; i < max && i < 16; i++) state.dbg_regs[i].ctrl = 0;

    size_t len = offsetof(hw_debug_state, dbg_regs) + max * sizeof(state.dbg_regs[0]);
    struct iovec wIov = { &state, len };
    return ptrace(PTRACE_SETREGSET, pid_, reinterpret_cast<void*>(NT_ARM_HW_WATCH), &wIov) >= 0;
}

bool Breakpoint::applyWatchpoints() {
    if (!attached_) return false;

    hw_debug_state state{};
    struct iovec iov = { &state, sizeof(state) };
    if (ptrace(PTRACE_GETREGSET, pid_, reinterpret_cast<void*>(NT_ARM_HW_WATCH), &iov) < 0)
        return false;

    int max = state.dbg_info & 0xFF;
    if (max <= 0) return false;

    for (int i = 0; i < max && i < 16; i++) {
        state.dbg_regs[i].addr = 0;
        state.dbg_regs[i].ctrl = 0;
    }

    int highestSlot = -1;
    for (const auto& [_, wp] : wps_) {
        if (!wp.enabled || wp.slot < 0 || wp.slot >= max) continue;
        if (wp.slot > highestSlot) highestSlot = wp.slot;

        uint32_t ctrl = 1; // E=1
        switch (wp.mode) {
            case WatchpointInfo::Read:      ctrl |= (1 << 3); break;
            case WatchpointInfo::Write:     ctrl |= (2 << 3); break;
            case WatchpointInfo::ReadWrite: ctrl |= (3 << 3); break;
        }

        int byteOff = wp.address & 7;
        uint8_t bas = ((1 << wp.size) - 1) << byteOff;
        ctrl |= (static_cast<uint32_t>(bas) << 5);

        state.dbg_regs[wp.slot].addr = wp.address & ~7UL;
        state.dbg_regs[wp.slot].ctrl = ctrl;
    }

    int slotsToWrite = highestSlot + 1;
    if (slotsToWrite <= 0) slotsToWrite = 1;

    iov.iov_base = &state;
    iov.iov_len = offsetof(hw_debug_state, dbg_regs) + slotsToWrite * sizeof(state.dbg_regs[0]);
    return ptrace(PTRACE_SETREGSET, pid_, reinterpret_cast<void*>(NT_ARM_HW_WATCH), &iov) >= 0;
}

bool Breakpoint::watchAdd(addr_t addr, size_t size, WatchpointInfo::Mode mode) {
    if (wps_.count(addr)) return false;

    if (!attached_ && !ptraceAttach()) return false;

    // 先停进程! GETREGSET 需要进程在 stopped 状态
    stopProcess();

    int slot = findFreeWatchSlot();
    if (slot < 0) {
        // 没空闲槽位, 恢复运行
        ptrace(PTRACE_CONT, pid_, nullptr, nullptr);
        running_ = true;
        return false;
    }

    if (size != 1 && size != 2 && size != 4 && size != 8) size = 4;

    wps_[addr] = { .address = addr, .size = size, .mode = mode,
                   .enabled = true, .hitCount = 0, .slot = slot };

    if (!applyWatchpoints()) {
        wps_.erase(addr);
        ptrace(PTRACE_CONT, pid_, nullptr, nullptr);
        running_ = true;
        return false;
    }

    running_ = true;
    ptrace(PTRACE_CONT, pid_, nullptr, nullptr);
    return true;
}

bool Breakpoint::watchRemove(addr_t addr) {
    auto it = wps_.find(addr);
    if (it == wps_.end()) return false;
    wps_.erase(it);

    if (attached_) {
        // 先停, 再改硬件寄存器, 最后 CONT
        stopProcess();

        if (wps_.empty()) clearHwWatchpoints();
        else applyWatchpoints();

        // 没有断点/观察点了 → 恢复运行
        if (bps_.empty() && wps_.empty()) {
            ptrace(PTRACE_CONT, pid_, nullptr, nullptr);
            running_ = true;
        }
    }
    return true;
}

} // namespace TsEngine
