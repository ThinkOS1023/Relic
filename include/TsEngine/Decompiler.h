#pragma once

#include "Core.h"
#include "Memory.h"
#include "Symbols.h"
#include <string>
#include <vector>

namespace TsEngine {

struct DecompileLine {
    std::string code;       // 伪 C 语句
    std::string comment;    // 注释 (符号名/字符串等)
    addr_t address;         // 原始指令地址
    bool isTarget;          // 是否为用户指定的目标地址
};

struct DecompileResult {
    std::string funcName;
    addr_t funcStart;
    addr_t funcEnd;
    int stackFrame;
    std::string signature;              // 函数签名
    std::vector<DecompileLine> lines;   // 逐行伪 C
};

// 反编译函数: 将 funcStart~funcEnd 的 ARM64 指令翻译为伪 C
// targetAddr: 用户指定的地址 (用于标记 >>)
// data: funcStart 处开始的机器码字节
DecompileResult decompile(
    const Memory& mem, const Symbols& syms,
    addr_t funcStart, addr_t funcEnd,
    addr_t targetAddr,
    const uint8_t* data, size_t dataSize);

} // namespace TsEngine
