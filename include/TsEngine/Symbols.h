#pragma once

#include "Core.h"
#include <string>
#include <unordered_map>

namespace TsEngine {

// 从 ELF 文件解析符号表 (PLT + 本地函数)
class Symbols {
public:
    // 从文件加载, baseAddr 是进程中的模块基址 (可多次调用累加)
    bool load(const std::string& elfPath, addr_t baseAddr);

    // 清空
    void clear() { symbols_.clear(); sortedSyms_.clear(); }

    // 地址 → 函数名 (返回空字符串表示未知)
    std::string resolve(addr_t addr) const;

    // 地址 → 函数名 + 偏移 (如 "printf+0x8")
    std::string resolveWithOffset(addr_t addr) const;

    // 函数名 → 地址 (返回 0 表示未找到, 支持模糊匹配)
    addr_t findByName(const std::string& name) const;

    size_t count() const { return symbols_.size(); }

private:
    struct SymInfo {
        std::string name;
        addr_t addr;
        size_t size;
    };

    std::unordered_map<addr_t, SymInfo> symbols_; // 精确地址匹配
    std::vector<SymInfo> sortedSyms_;              // 按地址排序, 用于范围查找
    addr_t base_ = 0;
};

} // namespace TsEngine
