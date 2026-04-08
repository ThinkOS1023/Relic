#pragma once

#include "Core.h"
#include "Breakpoint.h"
#include "Memory.h"
#include "Maps.h"
#include <vector>

namespace TsEngine {

class Remote {
public:
    Remote(pid_t pid, Breakpoint& bp, Memory& mem, Maps& maps)
        : pid_(pid), bp_(bp), mem_(mem), maps_(maps) {}

    // 远程调用: 在目标进程中调函数, 返回 x0
    struct CallResult {
        addr_t retval;   // x0
        bool success;
    };
    CallResult call(addr_t funcAddr, const std::vector<addr_t>& args = {});

    // 在目标进程写入字符串, 返回目标地址 (用完需 remoteFree)
    addr_t writeString(const std::string& str);

    // 远程 mmap: 在目标分配 RWX 内存
    addr_t remoteAlloc(size_t size);
    bool remoteFree(addr_t addr, size_t size);

    // 写 shellcode 并执行
    CallResult execShellcode(const std::vector<uint8_t>& code, const std::vector<addr_t>& args = {});

    // Inline hook
    struct HookInfo {
        addr_t target;           // 被 hook 的地址
        addr_t trampoline;       // trampoline 地址
        size_t trampolineSize;
        std::vector<uint8_t> origBytes; // 原始指令 (被覆盖的)
        std::string name;
    };

    bool hookFunction(addr_t target, const std::string& action); // ret0/ret1/nop/自定义地址
    bool unhookFunction(addr_t target);
    const std::vector<HookInfo>& hooks() const { return hooks_; }

private:
    addr_t findTrapAddr();  // 找一个临时 BRK 陷阱点
    bool writeCode(addr_t addr, const void* data, size_t size); // ptrace 写代码段

    pid_t pid_;
    Breakpoint& bp_;
    Memory& mem_;
    Maps& maps_;
    std::vector<HookInfo> hooks_;
    addr_t trapAddr_ = 0;
};

} // namespace TsEngine
