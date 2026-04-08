#pragma once

#include "Core.h"
#include "Memory.h"
#include <optional>

namespace TsEngine {

struct FuncBounds {
    addr_t start;
    addr_t end;
    [[nodiscard]] size_t size() const { return end - start; }
};

// 从给定地址向前搜函数头 (stp x29,x30 或前一个 ret 之后), 向后搜函数尾 (ret)
// maxBack: 向前最多搜索的指令数, maxForward: 向后最多搜索的指令数
std::optional<FuncBounds> findFunctionBounds(
    const Memory& mem, addr_t addr,
    int maxBack = 512, int maxForward = 1024);

} // namespace TsEngine
