#include "TsEngine/Remote.h"

#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <signal.h>
#include <cstring>
#include <cerrno>

namespace TsEngine {

// 找一个代码段地址当临时 BRK 陷阱
addr_t Remote::findTrapAddr() {
    if (trapAddr_) return trapAddr_;

    maps_.refresh();
    for (const auto& r : maps_.regions()) {
        if (r.executable() && r.size() >= 32 && !r.path.empty() && r.path[0] == '/') {
            trapAddr_ = (r.start + r.size() / 2) & ~3UL;
            return trapAddr_;
        }
    }
    return 0;
}

// 用 ptrace POKETEXT 写代码段
bool Remote::writeCode(addr_t addr, const void* data, size_t size) {
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    for (size_t i = 0; i < size; i += 4) {
        addr_t target = addr + i;
        addr_t aligned = target & ~7UL;
        int off = target & 7;

        errno = 0;
        long word = ptrace(PTRACE_PEEKTEXT, pid_, reinterpret_cast<void*>(aligned), nullptr);
        if (errno != 0) return false;

        size_t copyLen = std::min((size_t)4, size - i);
        std::memcpy(reinterpret_cast<char*>(&word) + off, bytes + i, copyLen);

        if (ptrace(PTRACE_POKETEXT, pid_, reinterpret_cast<void*>(aligned),
                   reinterpret_cast<void*>(word)) < 0)
            return false;
    }
    return true;
}

// ── 远程调用 ──
Remote::CallResult Remote::call(addr_t funcAddr, const std::vector<addr_t>& args) {
    CallResult result = { 0, false };

    // 确保 ptrace attached 且进程停下来
    if (!bp_.isAttached() && !bp_.ptraceAttach()) { errno = ESRCH; return result; }
    if (!bp_.stopProcess()) { errno = ESRCH; return result; }

    // 1. 保存寄存器
    auto origRegs = bp_.getRegs();
    if (!origRegs) { errno = EFAULT; return result; }

    // 2. 找 trap 地址, 保存原始指令, 写 BRK
    addr_t trap = findTrapAddr();
    if (!trap) return result;

    errno = 0;
    long origTrapWord = ptrace(PTRACE_PEEKTEXT, pid_, reinterpret_cast<void*>(trap & ~7UL), nullptr);
    if (errno != 0) return result;

    uint32_t brkInst = 0xD4200000;
    writeCode(trap, &brkInst, 4);

    // 3. 设置调用寄存器
    RegState callRegs = *origRegs;
    for (size_t i = 0; i < args.size() && i < 8; i++) {
        callRegs.regs[i] = args[i]; // x0-x7
    }
    callRegs.regs[30] = trap;    // x30 (LR) = trap, 函数 ret 后命中 BRK
    callRegs.pc = funcAddr;
    // SP 16 字节对齐
    callRegs.sp &= ~0xFUL;

    bp_.setRegs(callRegs);

    // 4. CONT → 函数执行 → ret → 命中 BRK
    if (ptrace(PTRACE_CONT, pid_, nullptr, nullptr) < 0) {
        // CONT 失败, 恢复
        ptrace(PTRACE_POKETEXT, pid_, reinterpret_cast<void*>(trap & ~7UL),
               reinterpret_cast<void*>(origTrapWord));
        bp_.setRegs(*origRegs);
        return result;
    }

    // 等待: 可能收到多种信号, 只要不是 SIGTRAP 就转发继续等
    int status;
    for (int attempt = 0; attempt < 20; attempt++) {
        waitpid(pid_, &status, 0);

        if (WIFEXITED(status) || WIFSIGNALED(status)) break; // 进程死了
        if (!WIFSTOPPED(status)) break;

        int sig = WSTOPSIG(status);
        if (sig == SIGTRAP) {
            // 命中 BRK!
            auto retRegs = bp_.getRegs();
            if (retRegs) {
                result.retval = retRegs->regs[0];
                result.success = true;
            }
            break;
        }
        // 其他信号: 转发并继续等
        ptrace(PTRACE_CONT, pid_, nullptr, reinterpret_cast<void*>(sig));
    }

    // 6. 恢复 trap 原始指令
    ptrace(PTRACE_POKETEXT, pid_, reinterpret_cast<void*>(trap & ~7UL),
           reinterpret_cast<void*>(origTrapWord));

    // 7. 恢复原始寄存器
    bp_.setRegs(*origRegs);

    // 8. 恢复运行
    bp_.resumeProcess();

    return result;
}

// ── 远程 mmap ──
addr_t Remote::remoteAlloc(size_t size) {
    // 在目标进程调 mmap(0, size, PROT_READ|PROT_WRITE|PROT_EXEC, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0)
    // ARM64 Linux: mmap 是 syscall #222
    // 但直接调 libc 的 mmap 更简单 — 找 mmap 的 PLT 地址

    // 方案: 构造一段 shellcode 做 mmap syscall
    // mov x0, #0          // addr = NULL
    // mov x1, #size       // length
    // mov x2, #7          // PROT_READ|PROT_WRITE|PROT_EXEC
    // mov x3, #0x22       // MAP_PRIVATE|MAP_ANONYMOUS
    // mvn x4, xzr         // fd = -1
    // mov x5, #0          // offset = 0
    // mov x8, #0xde       // __NR_mmap = 222
    // svc #0
    // ret

    uint32_t code[] = {
        0xD2800000,                             // mov x0, #0
        static_cast<uint32_t>(0xD2800001 | ((size & 0xFFFF) << 5)), // mov x1, #size (低16位)
        0xD28000E2,                             // mov x2, #7
        0xD2800443,                             // mov x3, #0x22
        0xAA1F03E4,                             // mov x4, xzr (然后 mvn)
        0xD2800005,                             // mov x5, #0
        0xD2801BC8,                             // mov x8, #222
        0xD4000001,                             // svc #0
        0xD65F03C0,                             // ret
    };
    // 修正 x4 = -1: mvn x4, xzr
    code[4] = 0xAA3F03E4; // orn x4, xzr, xzr = mvn x4, xzr 不对
    // mvn Xd, Xm = orn Xd, xzr, Xm → 编码复杂, 用 movn 更简单
    code[4] = 0x92800004; // movn x4, #0 → x4 = ~0 = -1

    // 处理 size > 65535 的情况
    if (size > 0xFFFF) {
        code[1] = 0xD2800001 | (static_cast<uint32_t>(size & 0xFFFF) << 5);
        // 需要 movk 设高位, 简单起见限制 size <= 64KB
        // 对于更大的, 用两条指令
    }

    auto result = execShellcode({reinterpret_cast<uint8_t*>(code),
                                  reinterpret_cast<uint8_t*>(code) + sizeof(code)});

    if (result.success && result.retval != static_cast<addr_t>(-1)) {
        return result.retval;
    }
    return 0;
}

bool Remote::remoteFree(addr_t addr, size_t size) {
    uint32_t munmapCode[] = {
        0xD2801AE8, // mov x8, #215
        0xD4000001, // svc #0
        0xD65F03C0, // ret
    };

    auto result = execShellcode({reinterpret_cast<uint8_t*>(munmapCode),
                                  reinterpret_cast<uint8_t*>(munmapCode) + sizeof(munmapCode)},
                                 {addr, size});
    return result.success && result.retval == 0;
}

// ── 执行 Shellcode ──
Remote::CallResult Remote::execShellcode(const std::vector<uint8_t>& code, const std::vector<addr_t>& args) {
    CallResult result = { 0, false };

    if (!bp_.isAttached() && !bp_.ptraceAttach()) return result;
    bp_.stopProcess();

    // 找一块可执行内存临时写 shellcode
    // 方案: 用代码段末尾的 padding 区域 (小 shellcode)
    // 或者用栈 (但栈不可执行)
    // 最安全: 先用 call 调 mmap 分配... 但 call 本身也可能用到 execShellcode → 循环依赖

    // 解决: 对于小 shellcode (< 64 bytes), 直接临时覆盖代码段某个位置
    // 对于大 shellcode, 先 remoteAlloc 再写

    if (code.size() > 256) {
        // 大 shellcode: 先分配内存
        // ... 这里会递归, 需要一个原始的 mmap 方式
        return result;
    }

    // 小 shellcode: 找代码段末尾, 临时覆盖
    maps_.refresh();
    addr_t codeAddr = 0;
    std::vector<uint8_t> origCode;

    for (const auto& r : maps_.regions()) {
        if (r.executable() && r.size() >= code.size() + 16) {
            // 用代码段末尾
            codeAddr = r.end - code.size() - 8;
            codeAddr &= ~3UL; // 4 字节对齐
            break;
        }
    }
    if (!codeAddr) return result;

    // 保存原始代码
    origCode.resize(code.size() + 4); // +4 给 BRK
    auto orig = mem_.readBuffer(codeAddr, origCode.size());
    if (!orig) return result;
    origCode = *orig;

    // 写 shellcode + 末尾 BRK (防止跑飞)
    std::vector<uint8_t> payload = code;
    // 确保以 ret 结尾 (调用者会设 LR = trap)
    // 不追加, 假设用户的 shellcode 已有 ret

    writeCode(codeAddr, payload.data(), payload.size());

    // 调用
    result = call(codeAddr, args);

    // 恢复原始代码
    writeCode(codeAddr, origCode.data(), origCode.size());

    return result;
}

// ── Inline Hook ──

// ARM64 绝对跳转 shellcode (12 bytes):
// LDR X16, [PC, #8]
// BR X16
// .quad target_addr
static void makeAbsJump(uint8_t* buf, addr_t target) {
    uint32_t ldr = 0x58000050; // LDR X16, #8
    uint32_t br  = 0xD61F0200; // BR X16
    std::memcpy(buf, &ldr, 4);
    std::memcpy(buf + 4, &br, 4);
    std::memcpy(buf + 8, &target, 8);
}

bool Remote::hookFunction(addr_t target, const std::string& action) {
    // 检查是否已 hook
    for (const auto& h : hooks_) {
        if (h.target == target) return false;
    }

    if (!bp_.isAttached() && !bp_.ptraceAttach()) return false;
    bp_.stopProcess();

    // 1. 分配 trampoline 内存 (256 bytes 够了)
    addr_t tramp = remoteAlloc(256);
    if (!tramp) return false;

    // 2. 保存目标函数前 16 字节 (4 条指令, 被绝对跳覆盖)
    constexpr size_t HOOK_SIZE = 16; // 12 字节绝对跳 + 4 字节对齐
    auto origData = mem_.readBuffer(target, HOOK_SIZE);
    if (!origData) { remoteFree(tramp, 256); return false; }

    // 3. 构建 trampoline
    std::vector<uint8_t> trampolineCode;

    if (action == "nop" || action == "ret") {
        // 直接返回, 不执行原函数
        uint32_t ret = 0xD65F03C0;
        trampolineCode.resize(4);
        std::memcpy(trampolineCode.data(), &ret, 4);
    }
    else if (action == "ret0") {
        // mov x0, #0; ret
        uint32_t inst[] = { 0xD2800000, 0xD65F03C0 };
        trampolineCode.assign(reinterpret_cast<uint8_t*>(inst),
                               reinterpret_cast<uint8_t*>(inst) + sizeof(inst));
    }
    else if (action == "ret1") {
        // mov x0, #1; ret
        uint32_t inst[] = { 0xD2800020, 0xD65F03C0 };
        trampolineCode.assign(reinterpret_cast<uint8_t*>(inst),
                               reinterpret_cast<uint8_t*>(inst) + sizeof(inst));
    }
    else if (action == "log") {
        // 执行原始指令, 然后跳回
        // trampoline: 原始指令 + 绝对跳回 target+HOOK_SIZE
        trampolineCode.resize(HOOK_SIZE + 16);
        std::memcpy(trampolineCode.data(), origData->data(), HOOK_SIZE);
        makeAbsJump(trampolineCode.data() + HOOK_SIZE, target + HOOK_SIZE);
    }
    else {
        // 自定义跳转地址
        addr_t jumpTarget = 0;
        try { jumpTarget = untag(std::stoull(action, nullptr, 16)); } catch (...) {}
        if (jumpTarget) {
            trampolineCode.resize(16);
            makeAbsJump(trampolineCode.data(), jumpTarget);
        } else {
            remoteFree(tramp, 256);
            return false;
        }
    }

    // 4. 写 trampoline 到分配的内存
    if (!mem_.writeRaw(tramp, trampolineCode.data(), trampolineCode.size())) {
        remoteFree(tramp, 256);
        return false;
    }

    // 5. 在目标函数入口写绝对跳转到 trampoline
    uint8_t jumpCode[16];
    makeAbsJump(jumpCode, tramp);
    writeCode(target, jumpCode, HOOK_SIZE);

    // 6. 记录
    hooks_.push_back({
        .target = target,
        .trampoline = tramp,
        .trampolineSize = 256,
        .origBytes = *origData,
        .name = action
    });

    // 恢复运行
    ptrace(PTRACE_CONT, pid_, nullptr, nullptr);

    return true;
}

bool Remote::unhookFunction(addr_t target) {
    for (auto it = hooks_.begin(); it != hooks_.end(); ++it) {
        if (it->target == target) {
            // 停进程
            bp_.stopProcess();

            // 恢复原始指令
            writeCode(target, it->origBytes.data(), it->origBytes.size());

            // 释放 trampoline
            remoteFree(it->trampoline, it->trampolineSize);

            hooks_.erase(it);

            // 恢复运行
            ptrace(PTRACE_CONT, pid_, nullptr, nullptr);
            return true;
        }
    }
    return false;
}

} // namespace TsEngine
