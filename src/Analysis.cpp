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
        if ((*inst & 0xFFC07FFF) == 0xA9007BFD ||
            (*inst & 0xFFC07FFF) == 0xA9807BFD) {
            funcStart = probe;

            // 继续往前看: stp 前面可能还有 sub sp / paciasp 等属于函数入口的指令
            for (int j = 1; j <= 4; j++) {
                addr_t prev = probe - j * 4;
                auto prevInst = mem.read<uint32_t>(prev);
                if (!prevInst) break;

                // sub sp, sp, #imm → 0xD10003FF 掩码 (Rd=sp, Rn=sp, sf=1)
                if ((*prevInst & 0xFF0003FF) == 0xD10003FF) {
                    funcStart = prev; continue;
                }
                // paciasp (hint #25) = 0xD503233F
                if (*prevInst == 0xD503233F) {
                    funcStart = prev; continue;
                }
                // pacibsp (hint #27) = 0xD503237F
                if (*prevInst == 0xD503237F) {
                    funcStart = prev; continue;
                }
                break; // 其他指令, 不是 prologue 的一部分
            }
            break;
        }
        // 前一个 ret 意味着新函数从 ret 之后开始
        if (*inst == 0xD65F03C0) {
            funcStart = probe + 4;
            break;
        }
    }

    // 向后搜函数尾 (第一个 ret 或 retaa/retab)
    addr_t funcEnd = addr;
    for (int i = 0; i <= maxForward; i++) {
        addr_t probe = addr + i * 4;
        auto inst = mem.read<uint32_t>(probe);
        if (!inst) break;
        // ret = 0xD65F03C0, retaa = 0xD65F0BFF, retab = 0xD65F0FFF
        if (*inst == 0xD65F03C0 || *inst == 0xD65F0BFF || *inst == 0xD65F0FFF) {
            funcEnd = probe + 4;
            break;
        }
    }

    size_t funcSize = funcEnd - funcStart;
    if (funcSize < 4 || funcSize > 1024 * 1024) return std::nullopt;

    return FuncBounds{ funcStart, funcEnd };
}

} // namespace TsEngine
