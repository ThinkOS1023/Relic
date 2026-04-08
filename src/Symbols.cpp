#include "TsEngine/Symbols.h"

#include <elf.h>
#include <cstring>
#include <fstream>
#include <algorithm>

namespace TsEngine {

// C++ 符号 demangle (简化版, 处理常见模式)
static std::string demangle(const std::string& mangled) {
    if (mangled.empty() || mangled[0] != '_' || mangled.size() < 3) return mangled;
    if (mangled[1] != 'Z') return mangled;

    // _Znwm → operator new
    if (mangled == "_Znwm" || mangled == "_Znwj") return "operator new";
    if (mangled == "_ZdlPv" || mangled == "_ZdlPvm") return "operator delete";
    if (mangled == "_Znam" || mangled == "_Znaj") return "operator new[]";
    if (mangled == "_ZdaPv" || mangled == "_ZdaPvm") return "operator delete[]";

    // _Z + 数字 + 名字 → 简单函数名
    // _Z10takeDamageP6Playeri → takeDamage
    size_t i = 2;
    // 跳过可能的 N (nested name)
    if (i < mangled.size() && mangled[i] == 'N') i++;

    std::string result;
    while (i < mangled.size()) {
        if (!isdigit(mangled[i])) break;
        int len = 0;
        while (i < mangled.size() && isdigit(mangled[i])) {
            len = len * 10 + (mangled[i] - '0');
            i++;
        }
        if (len <= 0 || i + len > mangled.size()) break;
        if (!result.empty()) result += "::";
        result += mangled.substr(i, len);
        i += len;
    }

    return result.empty() ? mangled : result;
}

// 去掉 @LIBC 等版本后缀
static std::string stripVersion(const std::string& name) {
    auto at = name.find('@');
    return (at != std::string::npos) ? name.substr(0, at) : name;
}

bool Symbols::load(const std::string& elfPath, addr_t baseAddr) {
    // 不 clear — 允许多次 load 累加符号 (多模块)
    base_ = baseAddr;

    // 处理 "(deleted)" 后缀
    std::string path = elfPath;
    auto del = path.find(" (deleted)");
    if (del != std::string::npos) path = path.substr(0, del);

    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return false;

    // 读 ELF header
    Elf64_Ehdr ehdr;
    f.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr));
    if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) return false;
    if (ehdr.e_ident[EI_CLASS] != ELFCLASS64) return false;

    // 读所有 section headers
    std::vector<Elf64_Shdr> shdrs(ehdr.e_shnum);
    f.seekg(ehdr.e_shoff);
    f.read(reinterpret_cast<char*>(shdrs.data()), ehdr.e_shnum * sizeof(Elf64_Shdr));

    // 读 section name string table
    std::string shstrtab;
    if (ehdr.e_shstrndx < shdrs.size()) {
        shstrtab.resize(shdrs[ehdr.e_shstrndx].sh_size);
        f.seekg(shdrs[ehdr.e_shstrndx].sh_offset);
        f.read(shstrtab.data(), shstrtab.size());
    }

    // 找 .dynsym, .dynstr, .symtab, .strtab, .rela.plt
    Elf64_Shdr *dynsym = nullptr, *dynstr = nullptr;
    Elf64_Shdr *symtab = nullptr, *strtab = nullptr;
    Elf64_Shdr *relaplt = nullptr;

    for (auto& sh : shdrs) {
        std::string name = (sh.sh_name < shstrtab.size()) ? &shstrtab[sh.sh_name] : "";
        if (name == ".dynsym") dynsym = &sh;
        else if (name == ".dynstr") dynstr = &sh;
        else if (name == ".symtab") symtab = &sh;
        else if (name == ".strtab") strtab = &sh;
        else if (name == ".rela.plt") relaplt = &sh;
    }

    // 解析 .dynsym + .dynstr (动态符号)
    std::vector<Elf64_Sym> dynSyms;
    std::string dynStrTab;
    if (dynsym && dynstr) {
        dynStrTab.resize(dynstr->sh_size);
        f.seekg(dynstr->sh_offset);
        f.read(dynStrTab.data(), dynStrTab.size());

        size_t count = dynsym->sh_size / sizeof(Elf64_Sym);
        dynSyms.resize(count);
        f.seekg(dynsym->sh_offset);
        f.read(reinterpret_cast<char*>(dynSyms.data()), dynsym->sh_size);
    }

    // 解析 .rela.plt → PLT 地址 → 符号名
    // PLT 布局: 第一个 entry 是 PLT0 (resolver), 之后每 16 字节一个 stub
    // .rela.plt 的顺序和 PLT stub 顺序一致
    if (relaplt && !dynSyms.empty()) {
        size_t relaCount = relaplt->sh_size / sizeof(Elf64_Rela);
        std::vector<Elf64_Rela> relas(relaCount);
        f.seekg(relaplt->sh_offset);
        f.read(reinterpret_cast<char*>(relas.data()), relaplt->sh_size);

        // 找 .plt section 的地址
        addr_t pltAddr = 0;
        for (auto& sh : shdrs) {
            std::string name = (sh.sh_name < shstrtab.size()) ? &shstrtab[sh.sh_name] : "";
            if (name == ".plt") { pltAddr = sh.sh_addr; break; }
        }

        if (pltAddr > 0) {
            // PLT0 是 32 字节 (或 16), 之后每个 stub 16 字节
            addr_t stubAddr = pltAddr + 0x20; // 跳过 PLT0

            for (size_t i = 0; i < relaCount; i++) {
                uint32_t symIdx = ELF64_R_SYM(relas[i].r_info);
                if (symIdx < dynSyms.size()) {
                    uint32_t nameOff = dynSyms[symIdx].st_name;
                    if (nameOff < dynStrTab.size()) {
                        std::string raw = &dynStrTab[nameOff];
                        std::string name = demangle(stripVersion(raw));
                        addr_t realAddr = baseAddr + stubAddr;
                        symbols_[realAddr] = { name, realAddr, 16 };
                    }
                }
                stubAddr += 0x10; // 每个 PLT stub 16 字节
            }
        }
    }

    // 解析 .symtab + .strtab (本地符号, 有调试信息时才有)
    auto loadSyms = [&](Elf64_Shdr* symSh, Elf64_Shdr* strSh) {
        if (!symSh || !strSh) return;
        std::string strTab(strSh->sh_size, '\0');
        f.seekg(strSh->sh_offset);
        f.read(strTab.data(), strTab.size());

        size_t count = symSh->sh_size / sizeof(Elf64_Sym);
        std::vector<Elf64_Sym> syms(count);
        f.seekg(symSh->sh_offset);
        f.read(reinterpret_cast<char*>(syms.data()), symSh->sh_size);

        for (const auto& sym : syms) {
            if (ELF64_ST_TYPE(sym.st_info) != STT_FUNC) continue;
            if (sym.st_value == 0 || sym.st_size == 0) continue;
            if (sym.st_name >= strTab.size()) continue;

            std::string raw = &strTab[sym.st_name];
            std::string name = demangle(stripVersion(raw));
            addr_t realAddr = baseAddr + sym.st_value;

            // 不覆盖已有的 (PLT 优先)
            if (symbols_.find(realAddr) == symbols_.end()) {
                symbols_[realAddr] = { name, realAddr, sym.st_size };
            }
        }
    };

    loadSyms(symtab, strtab);

    // 也加 .dynsym 中有地址的符号 (导出函数)
    for (const auto& sym : dynSyms) {
        if (ELF64_ST_TYPE(sym.st_info) != STT_FUNC) continue;
        if (sym.st_value == 0) continue;
        if (sym.st_name >= dynStrTab.size()) continue;

        std::string raw = &dynStrTab[sym.st_name];
        std::string name = demangle(stripVersion(raw));
        addr_t realAddr = baseAddr + sym.st_value;

        if (symbols_.find(realAddr) == symbols_.end()) {
            symbols_[realAddr] = { name, realAddr, sym.st_size };
        }
    }

    // 构建排序数组用于范围查找 (每次 load 后重建, 避免重复累加)
    sortedSyms_.clear();
    sortedSyms_.reserve(symbols_.size());
    for (const auto& [_, sym] : symbols_) {
        sortedSyms_.push_back(sym);
    }
    std::sort(sortedSyms_.begin(), sortedSyms_.end(),
              [](const SymInfo& a, const SymInfo& b) { return a.addr < b.addr; });

    return !symbols_.empty();
}

std::string Symbols::resolve(addr_t addr) const {
    auto it = symbols_.find(addr);
    if (it != symbols_.end()) return it->second.name;
    return "";
}

std::string Symbols::resolveWithOffset(addr_t addr) const {
    // 精确匹配
    auto it = symbols_.find(addr);
    if (it != symbols_.end()) return it->second.name;

    // 范围查找: 在 sortedSyms_ 里二分找 addr 属于哪个函数
    if (sortedSyms_.empty()) return "";

    auto lb = std::upper_bound(sortedSyms_.begin(), sortedSyms_.end(), addr,
        [](addr_t a, const SymInfo& s) { return a < s.addr; });

    if (lb == sortedSyms_.begin()) return "";
    --lb;

    if (addr >= lb->addr && addr < lb->addr + lb->size) {
        size_t off = addr - lb->addr;
        if (off == 0) return lb->name;
        char buf[256];
        snprintf(buf, sizeof(buf), "%s+0x%lx", lb->name.c_str(), (unsigned long)off);
        return buf;
    }

    return "";
}

addr_t Symbols::findByName(const std::string& name) const {
    // 精确匹配
    for (const auto& [addr, sym] : symbols_) {
        if (sym.name == name) return addr;
    }
    // 子串匹配 (如 "printf" 匹配 "printf")
    for (const auto& [addr, sym] : symbols_) {
        if (sym.name.find(name) != std::string::npos) return addr;
    }
    // 不区分大小写
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (const auto& [addr, sym] : symbols_) {
        std::string sl = sym.name;
        std::transform(sl.begin(), sl.end(), sl.begin(), ::tolower);
        if (sl.find(lower) != std::string::npos) return addr;
    }
    return 0;
}

} // namespace TsEngine
