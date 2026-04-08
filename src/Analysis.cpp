#include "TsEngine/Analysis.h"

namespace TsEngine {

std::optional<FuncBounds> findFunctionBounds(
    const Memory& mem, addr_t addr, int maxBack, int maxForward)
{
    // 向前搜函数头
    addr_t funcStart = addr;
    for (int i = 1; i <= maxBack; i++) {
        addr_t probe = addr - i * 4;
        auto inst = mem.read<uint32_t>(probe);
        if (!inst) break;
        // stp x29, x30, [sp, #imm] (各种变体: pre-index / signed offset)
        // 掩码 0xFFC07FFF 匹配 opc=10 + V=0 + 不同 imm7 范围的 stp
        if ((*inst & 0xFFC07FFF) == 0xA9007BFD ||
            (*inst & 0xFFC07FFF) == 0xA9807BFD) {
            funcStart = probe;
            break;
        }
        // 前一个 ret 意味着新函数从 ret 之后开始
        if (*inst == 0xD65F03C0) {
            funcStart = probe + 4;
            break;
        }
    }

    // 向后搜函数尾 (第一个 ret)
    addr_t funcEnd = addr;
    for (int i = 0; i <= maxForward; i++) {
        addr_t probe = addr + i * 4;
        auto inst = mem.read<uint32_t>(probe);
        if (!inst) break;
        if (*inst == 0xD65F03C0) {
            funcEnd = probe + 4; // 包含 ret 本身
            break;
        }
    }

    size_t funcSize = funcEnd - funcStart;
    if (funcSize < 4 || funcSize > 1024 * 1024) return std::nullopt;

    return FuncBounds{ funcStart, funcEnd };
}

} // namespace TsEngine
