#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <sys/types.h>

namespace TsEngine {

using addr_t = uintptr_t;
static_assert(sizeof(addr_t) == 8, "TsEngine only supports 64-bit targets");

// ARM64 Tagged Pointer: 去掉顶部标签字节 (Android MTE/HWASan/TBI)
// 0xb400007e4c037010 → 0x0000007e4c037010
inline addr_t untag(addr_t addr) { return addr & 0x00FFFFFFFFFFFFFFUL; }

struct MemRegion {
    addr_t start;
    addr_t end;
    std::string perms;   // "rwxp" / "r--s" etc.
    std::string path;

    [[nodiscard]] size_t size() const { return end - start; }
    [[nodiscard]] bool readable()   const { return perms.size() > 0 && perms[0] == 'r'; }
    [[nodiscard]] bool writable()   const { return perms.size() > 1 && perms[1] == 'w'; }
    [[nodiscard]] bool executable() const { return perms.size() > 2 && perms[2] == 'x'; }
};

struct ProcessInfo {
    pid_t pid;
    std::string name;
};

enum class Error {
    ProcessNotFound,
    AttachFailed,
    ReadFailed,
    WriteFailed,
    PtraceFailed,
    InvalidAddress,
    PermissionDenied,
};

// Result 类型: 用 optional 替代 (编译器不支持 std::expected)
// 错误通过返回 nullopt 表达

} // namespace TsEngine
