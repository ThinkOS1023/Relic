#include "TsEngine/UI.h"
#include "TsEngine/Analysis.h"
#include "TsEngine/Decompiler.h"

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <iostream>
#include <fstream>
#include <ctime>
#include <filesystem>
#include <chrono>
#include <thread>
#include <set>
#include <cerrno>
#include <unistd.h>
#include <sys/select.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <signal.h>
#include <capstone/capstone.h>
#include <keystone/keystone.h>

namespace TsEngine {

// ── ANSI ──
#define RST  "\033[0m"
#define BLD  "\033[1m"
#define DIM  "\033[2m"
#define UND  "\033[4m"
#define R    "\033[38;5;203m"   // 红 (柔和)
#define G    "\033[38;5;114m"   // 绿
#define Y    "\033[38;5;222m"   // 黄
#define B    "\033[38;5;111m"   // 蓝
#define C    "\033[38;5;80m"    // 青
#define M    "\033[38;5;176m"   // 紫
#define W    "\033[38;5;252m"   // 白
#define D    "\033[38;5;242m"   // 灰
#define BG_S "\033[48;5;236m"   // 深灰背景

UI::UI() = default;

void UI::printColor(const char* color, const char* fmt, ...) {
    fputs(color, stdout);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fputs(RST, stdout);
}

// record: 只写缓存, 不输出到终端 (用于 dump 导出)
void UI::record(const char* fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n > 0) {
        lastOutput_.append(buf, std::min(n, (int)sizeof(buf) - 1));
    }
}

// 去除 ANSI 转义码 (写文件时用)
static std::string stripAnsi(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool esc = false;
    for (char c : s) {
        if (c == '\033') { esc = true; continue; }
        if (esc) { if (c == 'm') esc = false; continue; }
        out += c;
    }
    return out;
}

// ── 分隔线 ──
static void line(const char* color = D, int len = 44) {
    fputs(color, stdout);
    for (int i = 0; i < len; i++) fputs("─", stdout);
    puts(RST);
}

// ── Banner ──
void UI::printBanner() {
    puts("");
    printf(C "   ______       ____            _\n");
    printf(  "  /_  __/____  / __/___  ____ _(_)___  ___\n");
    printf(  "   / / / ___/ / __/ __ \\/ __ `/ / __ \\/ _ \\\n");
    printf(  "  / / (__  ) / /_ / / / / /_/ / / / / /  __/\n");
    printf(  " /_/ /____/ /___//_/ /_/\\__, /_/_/ /_/\\___/\n");
    printf(  "                       /____/\n" RST);
    puts("");
    printf(D "          Android Memory Debug Engine\n" RST);
    printf(D "                   " W "v0.1\n" RST);
    puts("");
    printf(D "   输入 " W "help" D " 查看命令  " W "guide" D " 查看教程\n" RST);
    puts("");
}

// ── 新手教程 ──
static void printGuide() {
    puts("");
    printf(BLD C "  ┌─ 新手教程 ──────────────────────────────────────────┐\n" RST);
    puts("");

    // Step 1
    printf(BLD Y "  [1]" RST " 找到目标进程\n" RST);
    printf(D "  ·\n" RST);
    printf(D "  ·  " RST "用 " BLD W "ps" RST " 列出进程, 加关键词过滤:\n");
    printf(D "  ·\n" RST);
    printf(D "  ·  " RST BG_S "  " G "ts > " W "ps unity" RST "                               \n");
    printf(D "  ·  " RST BG_S "  " G "ts > " W "ps com.tencent" RST "                          \n");
    printf(D "  ·\n" RST);

    // Step 2
    printf(BLD Y "  [2]" RST " 附加进程\n" RST);
    printf(D "  ·\n" RST);
    printf(D "  ·  " RST "支持包名 (模糊匹配) 或直接输 PID:\n");
    printf(D "  ·\n" RST);
    printf(D "  ·  " RST BG_S "  " G "ts > " W "attach com.xxx.game" RST "                     \n");
    printf(D "  ·  " RST BG_S "  " G "ts > " W "attach 12345" RST "                             \n");
    printf(D "  ·\n" RST);
    printf(D "  ·  " RST "附加后提示符会变成  " G "game(12345) >" RST "\n");
    printf(D "  ·\n" RST);

    // Step 3
    printf(BLD Y "  [3]" RST " 查看内存布局\n" RST);
    printf(D "  ·\n" RST);
    printf(D "  ·  " RST BG_S "  " G "game > " W "maps libil2cpp" RST "                        \n");
    printf(D "  ·\n" RST);
    printf(D "  ·  " RST "输出中第一列就是模块基址, 记下来后面用\n");
    printf(D "  ·\n" RST);

    // Step 4
    printf(BLD Y "  [4]" RST " 读写内存\n" RST);
    printf(D "  ·\n" RST);
    printf(D "  ·  " RST "读取 128 字节的 hex dump:\n");
    printf(D "  ·  " RST BG_S "  " G "game > " W "read 0x7a03e00000 128" RST "                 \n");
    printf(D "  ·\n" RST);
    printf(D "  ·  " RST "写入 (支持连写或空格分隔):\n");
    printf(D "  ·  " RST BG_S "  " G "game > " W "write 0x7a03e00000 90 90 90 90" RST "        \n");
    printf(D "  ·\n" RST);

    // Step 5
    printf(BLD Y "  [5]" RST " 断点调试\n" RST);
    printf(D "  ·\n" RST);
    printf(D "  ·  " RST "直接下断点, " BLD "不需要" RST " 手动暂停游戏:\n");
    printf(D "  ·  " RST BG_S "  " G "game > " W "bp add 0x7a03e12340" RST "                   \n");
    printf(D "  ·\n" RST);
    printf(D "  ·  " RST "自动完成 ptrace→设断点→游戏继续运行\n");
    printf(D "  ·  " RST "目标指令被执行时游戏暂停, 然后查看寄存器:\n");
    printf(D "  ·  " RST BG_S "  " G "game > " W "bp wait" RST "                                \n");
    printf(D "  ·  " RST BG_S "  " G "game > " W "regs" RST "                                   \n");
    printf(D "  ·  " RST BG_S "  " G "game > " W "bp continue" RST "                            \n");
    printf(D "  ·\n" RST);

    // Step 6
    printf(BLD Y "  [6]" RST " 指针扫描 (逆结构)\n" RST);
    printf(D "  ·\n" RST);
    printf(D "  ·  " RST "找谁指向这个地址, 用于逆向分析对象结构:\n");
    printf(D "  ·  " RST BG_S "  " G "game > " W "scan 0x12abc000" RST "                       \n");
    printf(D "  ·\n" RST);
    printf(D "  ·  " RST "结果自动导出到 /storage/emulated/0/TsEngine/\n");
    printf(D "  ·\n" RST);

    // Step 7
    printf(BLD Y "  [7]" RST " Unity il2cpp 分析\n" RST);
    printf(D "  ·\n" RST);
    printf(D "  ·  " RST "查找 libil2cpp.so:\n");
    printf(D "  ·  " RST BG_S "  " G "game > " W "il2cpp find" RST "                           \n");
    printf(D "  ·\n" RST);
    printf(D "  ·  " RST "给一个对象实例地址, 自动解析出类/字段/方法:\n");
    printf(D "  ·  " RST BG_S "  " G "game > " W "il2cpp 0x12abc000" RST "                     \n");
    printf(D "  ·\n" RST);
    printf(D "  ·  " RST "原理: 实例+0x00 → klass指针 → 类名/字段/方法\n");
    printf(D "  ·\n" RST);

    printf(BLD C "  └──────────────────────────────────────────────────────┘\n" RST);
    puts("");
}

// ── Help ──
void UI::printHelp() {
    puts("");

    printf(BLD C "  ┌─ 命令列表 ─────────────────────────────────┐" RST "\n");
    puts("");

    // 进程
    printf(BLD M "  进程" RST "\n");
    printf("    " BLD W "attach" RST " <包名|pid>     附加到目标进程\n");
    printf("    " BLD W "detach" RST "                分离当前进程\n");
    printf("    " BLD W "ps" RST " [关键词]           列出进程 (可过滤)\n");
    printf("    " BLD W "pause" RST "                 暂停进程\n");
    printf("    " BLD W "resume" RST "                恢复进程\n");
    puts("");

    // 内存
    printf(BLD M "  内存" RST "\n");
    printf("    " BLD W "read" RST " <地址> [大小]    hex dump 内存\n");
    printf("    " BLD W "readval" RST " <地址> <类型>  读值 (int/float/long/double)\n");
    printf("    " BLD W "write" RST " <地址> <hex>    写入字节\n");
    printf("    " BLD W "maps" RST " [关键词]         查看内存映射\n");
    printf("    " BLD W "search" RST " <类型> <值>    搜索内存值\n");
    puts("");

    // 指针扫描
    printf(BLD M "  指针扫描" RST "\n");
    printf("    " BLD W "scan" RST " <地址>           找谁指向这个地址 (逆结构)\n");
    puts("");

    // 断点
    printf(BLD M "  断点" RST D "  (自动 ptrace, 不会暂停游戏)" RST "\n");
    printf("    " BLD W "bp add" RST " <地址>         添加断点 (自动继续运行)\n");
    printf("    " BLD W "bp del" RST " <地址>         删除断点\n");
    printf("    " BLD W "bp list" RST "               查看所有断点\n");
    printf("    " BLD W "bp wait" RST "               等待断点命中\n");
    printf("    " BLD W "bp continue" RST "           跨过断点继续执行\n");
    printf("    " BLD W "bp detach" RST "             分离 ptrace\n");
    printf("    " BLD W "regs" RST "                  查看 ARM64 寄存器\n");
    puts("");

    // 观察点
    printf(BLD M "  数据观察点" RST D "  (硬件, 监控谁访问/修改了数据)" RST "\n");
    printf("    " BLD W "watch" RST " <地址> [模式] [大小]  设观察点\n");
    printf(D "            模式: w=写(默认) r=读 rw=读写\n" RST);
    printf(D "            大小: 1/2/4/8 (默认4)  命中自动打印不暂停\n" RST);
    printf("    " BLD W "watch list" RST "            查看所有观察点\n");
    printf("    " BLD W "watch del" RST " <地址>      删除观察点\n");
    puts("");

    // 汇编
    printf(BLD M "  汇编" RST "\n");
    printf("    " BLD W "disasm" RST " <地址> [数量]   反汇编\n");
    printf("    " BLD W "disasm" RST " <地址> func     整个函数\n");
    printf("    " BLD W "disasm" RST " <地址> -10 +20  前后范围\n");
    printf("    " BLD W "dec" RST " <地址>             反编译成伪 C\n");
    printf("    " BLD W "dumpfn" RST " <地址> [文件]   导出函数 bin (电脑用)\n");
    printf("    " BLD W "patch" RST " <地址> nop       写 NOP\n");
    printf("    " BLD W "patch" RST " <地址> ret       写 RET (跳过函数)\n");
    printf("    " BLD W "patch" RST " <地址> ret0      MOV X0,#0 + RET\n");
    printf("    " BLD W "patch" RST " <地址> hex <值>  写原始指令\n");
    printf("    " BLD W "patch" RST " <地址> <汇编>    ARM64 汇编 (keystone)\n");
    printf(D "          如: " W "add w8, w8, #100" D " / " W "mov w0, #999\n" RST);
    puts("");

    // Hook
    printf(BLD M "  Hook & 远程调用" RST "\n");
    printf("    " BLD W "call" RST " <函数名|地址> [参数]  远程调用\n");
    printf("    " BLD W "hook" RST " <函数名|地址> ret0   hook 返回 0\n");
    printf("    " BLD W "hook" RST " <函数名|地址> nop    跳过函数\n");
    printf("    " BLD W "hook" RST " <函数名|地址> log    保留原行为\n");
    printf("    " BLD W "hook list" RST "              查看 hook\n");
    printf("    " BLD W "hook del" RST " <地址>        删除 hook\n");
    puts("");

    // Unity
    printf(BLD M "  Unity il2cpp" RST "\n");
    printf("    " BLD W "il2cpp find" RST "           查找 libil2cpp.so\n");
    printf("    " BLD W "il2cpp" RST " <实例地址>     解析对象元数据\n");
    puts("");

    // 导出 & 状态
    printf(BLD M "  工具" RST "\n");
    printf("    " BLD W "dump" RST " [文件名]         导出上次结果到文件\n");
    printf("    " BLD W "status" RST "                当前状态总览\n");
    printf("    " BLD W "guide" RST "                 新手教程\n");
    printf("    " BLD W "clear" RST "                 清屏\n");
    printf("    " BLD W "exit" RST "                  退出\n");
    puts("");

    printf(D "  别名: a=attach r=read w=write q=exit h=help s=scan" RST "\n");
    puts("");

    printf(BLD C "  └────────────────────────────────────────────┘" RST "\n");
    puts("");
}

bool UI::ensureAttached() {
    if (!proc_.isAttached()) {
        printf("\n  " R "✗" RST " 未附加进程\n");
        printf(D "    先执行: " W "attach <包名>" D " 或 " W "attach <pid>\n" RST);
        puts("");
        return false;
    }
    return true;
}

// ===== 命令实现 =====

void UI::cmdAttach(const std::string& arg) {
    if (arg.empty()) {
        printf("\n  " R "✗" RST " 缺少参数\n");
        printf(D "    用法: " W "attach com.xxx.game" D " 或 " W "attach 12345\n" RST);
        puts("");
        return;
    }

    pid_t pid = -1;
    bool isPid = std::all_of(arg.begin(), arg.end(), ::isdigit);

    if (isPid) {
        try {
            pid = std::stoi(arg);
        } catch (...) {
            printf("\n  " R "✗" RST " PID 超出范围: " W "%s\n" RST, arg.c_str());
            puts("");
            return;
        }
    } else {
        auto found = Process::findPid(arg);
        if (!found) {
            printf("\n  " R "✗" RST " 找不到进程: " W "%s\n" RST, arg.c_str());
            printf(D "    用 " W "ps %s" D " 检查进程是否运行中\n" RST, arg.c_str());
            puts("");
            return;
        }
        pid = *found;
    }

    if (proc_.attach(pid)) {
        // 先清理旧进程的 ptrace / 断点 / 观察点
        if (bp_ && bp_->isAttached()) bp_->ptraceDetach();
        remote_.reset(); bp_.reset(); il2cpp_.reset(); maps_.reset(); mem_.reset();

        mem_    = std::make_unique<Memory>(pid);
        maps_   = std::make_unique<Maps>(pid);
        bp_     = std::make_unique<Breakpoint>(pid);
        il2cpp_ = std::make_unique<Il2cppInspector>(*mem_);
        remote_ = std::make_unique<Remote>(pid, *bp_, *mem_, *maps_);
        maps_->refresh();

        // 加载所有模块符号
        syms_.clear();
        {
            std::set<std::string> loaded;
            for (const auto& r : maps_->regions()) {
                if (!r.executable() || r.path.empty() || r.path[0] != '/') continue;
                std::string path = r.path;
                auto dp = path.find(" (deleted)");
                if (dp != std::string::npos) path = path.substr(0, dp);
                if (loaded.count(path)) continue;
                loaded.insert(path);
                // 模块基址 = 同路径的最低段
                addr_t base = r.start;
                for (const auto& r2 : maps_->regions()) {
                    std::string p2 = r2.path;
                    auto dp2 = p2.find(" (deleted)");
                    if (dp2 != std::string::npos) p2 = p2.substr(0, dp2);
                    if (p2 == path && r2.start < base) base = r2.start;
                }
                syms_.load(r.path, base); // 累加到同一个表
            }
        }

        int sc = syms_.count();
        printf("\n  " G "✓" RST " 已附加 " BLD W "%s" RST D " (PID:%d)", proc_.name().c_str(), pid);
        if (sc > 0) printf(D "  [%d 符号]", sc);
        printf(RST "\n\n");
    } else {
        printf("\n  " R "✗" RST " 附加失败 PID:%d\n", pid);
        printf(D "    可能需要 root 权限, 或进程不存在\n" RST);
        puts("");
    }
}

void UI::cmdDetach() {
    if (bp_ && bp_->isAttached()) bp_->ptraceDetach();
    remote_.reset(); bp_.reset(); il2cpp_.reset(); maps_.reset(); mem_.reset();
    proc_.detach();
    printf("\n  " G "✓" RST " 已分离\n\n");
}

void UI::cmdPause() {
    if (!ensureAttached()) return;
    if (proc_.pause())
        printf("\n  " G "✓" RST " 已暂停 PID:%d\n\n", proc_.pid());
    else
        printf("\n  " R "✗" RST " 暂停失败\n\n");
}

void UI::cmdResume() {
    if (!ensureAttached()) return;
    if (proc_.resume())
        printf("\n  " G "✓" RST " 已恢复 PID:%d\n\n", proc_.pid());
    else
        printf("\n  " R "✗" RST " 恢复失败\n\n");
}

void UI::cmdPs(const std::string& filter) {
    auto list = Process::list();
    if (!filter.empty()) {
        std::erase_if(list, [&](const ProcessInfo& p) {
            return p.name.find(filter) == std::string::npos;
        });
    }

    puts("");
    printf(D "  %-8s" RST "  %s\n", "PID", "进程名");
    line(D);

    int count = 0;
    for (const auto& p : list) {
        if (count >= 50 && filter.empty()) {
            printf(D "\n  ... 共 %zu 个进程, 用 " W "ps <关键词>" D " 过滤\n" RST, list.size());
            break;
        }
        printf("  " C "%-8d" RST "  %s\n", p.pid, p.name.c_str());
        count++;
    }
    printf(D "\n  %zu 个结果" RST "\n\n", list.size());
}

void UI::cmdMaps(const std::string& filter) {
    if (!ensureAttached()) return;

    maps_->refresh();
    auto regions = filter.empty() ? maps_->regions() : maps_->findByName(filter);

    lastOutput_.clear();
    record("maps %s (%zu regions)\n\n", filter.c_str(), regions.size());

    puts("");
    printf(D "  %-18s %-18s %-5s %s\n" RST, "Start", "End", "Perm", "Path");
    line(D, 70);

    int count = 0;
    for (const auto& r : regions) {
        if (count >= 80 && filter.empty()) {
            printf(D "\n  ... 共 %zu 个, 用 " W "maps <关键词>" D " 过滤\n" RST, regions.size());
            break;
        }
        printf("  " C "%016lx" RST " " D "-" RST " " C "%016lx" RST "  " Y "%-4s" RST "  %s\n",
               (unsigned long)r.start, (unsigned long)r.end,
               r.perms.c_str(), r.path.c_str());
        record("%016lx-%016lx %s %s\n",
               (unsigned long)r.start, (unsigned long)r.end,
               r.perms.c_str(), r.path.c_str());
        count++;
    }
    printf(D "\n  %zu 个区域" RST "\n\n", regions.size());
}

void UI::cmdRead(const std::string& arg) {
    if (!ensureAttached()) return;

    std::istringstream iss(arg);
    std::string addrStr;
    size_t size = 256;
    iss >> addrStr;
    if (addrStr.empty()) {
        printf("\n  " R "✗" RST " 缺少地址\n");
        printf(D "    用法: " W "read 0x7f001000 128\n" RST);
        puts("");
        return;
    }
    iss >> size;
    if (size == 0) size = 256;

    try {
        addr_t addr = untag(std::stoull(addrStr, nullptr, 16));
        auto dump = mem_->hexDump(addr, size);
        lastOutput_.clear();
        lastOutput_ = dump;
        puts("");
        printf(G "%s" RST, dump.c_str());
    } catch (...) {
        printf("\n  " R "✗" RST " 地址格式错误, 需要 16 进制\n\n");
    }
}

void UI::cmdWrite(const std::string& arg) {
    if (!ensureAttached()) return;

    std::istringstream iss(arg);
    std::string addrStr, hexPart;
    iss >> addrStr;
    std::getline(iss, hexPart);

    if (addrStr.empty() || hexPart.empty()) {
        printf("\n  " R "✗" RST " 缺少参数\n");
        printf(D "    用法: " W "write 0x7f001000 90909090\n" RST);
        printf(D "    或:   " W "write 0x7f001000 90 90 90 90\n" RST);
        puts("");
        return;
    }

    try {
        addr_t addr = untag(std::stoull(addrStr, nullptr, 16));
        std::string hex;
        for (char c : hexPart) if (c != ' ') hex += c;

        std::vector<uint8_t> bytes;
        for (size_t i = 0; i + 1 < hex.size(); i += 2)
            bytes.push_back(static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16)));

        if (bytes.empty()) {
            printf("\n  " R "✗" RST " 无有效数据\n\n");
            return;
        }

        if (mem_->writeRaw(addr, bytes.data(), bytes.size())) {
            printf("\n  " G "✓" RST " 写入 " W "%zu" RST " bytes @ " C "0x%lx\n" RST "\n",
                   bytes.size(), (unsigned long)addr);
        } else {
            printf("\n  " R "✗" RST " 写入失败 (地址不可写?)\n\n");
        }
    } catch (...) {
        printf("\n  " R "✗" RST " 格式错误\n\n");
    }
}

void UI::cmdBp(const std::string& arg) {
    if (!ensureAttached()) return;

    std::istringstream iss(arg);
    std::string sub;
    iss >> sub;

    if (sub == "add") {
        std::string s;
        iss >> s;
        if (s.empty()) {
            printf("\n  " R "✗" RST " 用法: " W "bp add <函数地址>\n" RST);
            printf(D "    注意: 地址必须是代码段(r-xp), 不能是数据地址\n" RST "\n");
            return;
        }
        try {
            addr_t addr = untag(std::stoull(s, nullptr, 16));

            // 检查地址是否在可执行区域
            maps_->refresh();
            bool isExec = false;
            std::string regionPath;
            for (const auto& r : maps_->regions()) {
                if (addr >= r.start && addr < r.end) {
                    isExec = r.executable();
                    regionPath = r.path;
                    break;
                }
            }

            if (!isExec) {
                printf("\n  " R "✗" RST " 地址 " C "0x%lx" RST " 不在可执行区域\n", (unsigned long)addr);
                if (!regionPath.empty())
                    printf(D "    该地址属于: %s (非代码段)\n" RST, regionPath.c_str());
                printf(D "    断点只能下在函数/代码地址上, 不能下在数据地址\n" RST);
                printf(D "    用 " W "maps" D " 查看哪些区域是 " W "r-xp\n" RST "\n");
                return;
            }

            if (bp_->add(addr, true)) {
                printf("\n  " G "✓" RST " 断点 @ " C "0x%lx" RST "  游戏继续运行中\n", (unsigned long)addr);
                if (!regionPath.empty())
                    printf(D "    区域: %s\n" RST, regionPath.c_str());
                printf(D "    命中时自动暂停, 用 " W "bp wait" D " 等待\n" RST "\n");
            } else {
                printf("\n  " R "✗" RST " 添加失败 (需要 root 权限?)\n\n");
            }
        } catch (...) {
            printf("\n  " R "✗" RST " 地址格式错误\n\n");
        }
    }
    else if (sub == "del" || sub == "rm") {
        std::string s;
        iss >> s;
        try {
            addr_t addr = untag(std::stoull(s, nullptr, 16));
            if (bp_->remove(addr))
                printf("\n  " G "✓" RST " 已删除 @ " C "0x%lx\n" RST "\n", (unsigned long)addr);
            else
                printf("\n  " R "✗" RST " 该地址无断点\n\n");
        } catch (...) {
            printf("\n  " R "✗" RST " 用法: " W "bp del 0x1234\n" RST "\n");
        }
    }
    else if (sub == "list" || sub == "ls") {
        const auto& bpMap = bp_->list();
        if (bpMap.empty()) {
            printf(D "\n  暂无断点, 用 " W "bp add <地址>" D " 添加\n" RST "\n");
            return;
        }
        puts("");
        printf(D "  %-18s  %-6s  %-6s  %s\n" RST, "地址", "状态", "命中", "原始指令");
        line(D, 50);
        for (const auto& [addr, info] : bpMap) {
            printf("  " C "0x%016lx" RST "  %s  " W "%-6u" RST "  " D "0x%08x" RST "\n",
                   (unsigned long)addr,
                   info.enabled ? (G "启用" RST) : (R "禁用" RST),
                   info.hitCount,
                   info.originalInst);
        }
        puts("");
    }
    else if (sub == "wait" || sub == "w") {
        printf(D "\n  等待命中中...\n" RST);
        fflush(stdout);
        auto hit = bp_->waitHit();
        if (hit) {
            printf("  " G "+" RST " 命中 @ " Y "0x%lx\n" RST, (unsigned long)*hit);
            printf(D "    " W "regs" D " 查看  " W "disasm 0x%lx" D " 反汇编  " W "bp continue" D " 继续\n" RST "\n",
                   (unsigned long)*hit);
        } else {
            printf("  " R "x" RST " 等待失败或进程退出\n\n");
        }
    }
    else if (sub == "continue" || sub == "cont" || sub == "c") {
        if (!bp_->continueExec()) {
            printf("\n  " R "✗" RST " 执行失败\n\n");
        } else if (!bp_->watchList().empty()) {
            // 有观察点: 必须保持 waitpid 循环, 否则下次触发时 target 卡死
            printf("\n  " G "✓" RST " 继续运行, 等待下次命中...\n");
            fflush(stdout);
            auto hit = bp_->waitHit();
            if (hit) {
                printf("  " G "✓" RST " 命中 @ " Y "0x%lx\n" RST, (unsigned long)*hit);
                printf(D "    " W "regs" D " 查看  " W "bp continue" D " 继续  " W "watch del" D " 取消监控\n" RST "\n");
            } else {
                printf("  " R "✗" RST " 进程退出或异常\n\n");
            }
        } else {
            printf("\n  " G "✓" RST " 继续执行\n\n");
        }
    }
    else if (sub == "detach") {
        bp_->ptraceDetach();
        printf("\n  " G "✓" RST " ptrace 已分离, 游戏恢复运行\n\n");
    }
    else {
        printf("\n  " R "✗" RST " 未知子命令: " W "%s\n" RST, sub.c_str());
        puts("");
        printf(D "  可用: " W "add" D " | " W "del" D " | " W "list" D " | "
               W "wait" D " | " W "continue" D " | " W "detach\n" RST);
        puts("");
    }
}

void UI::cmdRegs() {
    if (!ensureAttached() || !bp_) return;

    auto regs = bp_->getRegs();
    if (!regs) {
        printf("\n  " R "✗" RST " 读取失败\n");
        printf(D "    需要先 " W "bp add <地址>" D " 设断点, 断点命中后才能读\n" RST "\n");
        return;
    }

    puts("");
    printf(BLD M "  ARM64 寄存器" RST "\n");
    line(D, 50);
    for (int i = 0; i < 31; i++) {
        printf("  " D "X%-2d " RST C "0x%016lx" RST, i, (unsigned long)regs->regs[i]);
        if (i % 2 == 1) puts(""); else printf("   ");
    }
    puts("");
    printf("  " D "SP  " RST G "0x%016lx" RST "   ", (unsigned long)regs->sp);
    printf(D "PC  " RST Y "0x%016lx" RST "\n", (unsigned long)regs->pc);

    // 自动反汇编 PC 附近指令
    addr_t pc = regs->pc;
    if (mem_) {
        // PC 前 4 条 + PC 当前 + PC 后 7 条 = 共 12 条
        addr_t start = (pc >= 16) ? pc - 16 : 0;
        int totalInst = 12;
        size_t dataSize = totalInst * 4;
        auto data = mem_->readBuffer(start, dataSize);
        if (data) {
            csh handle;
            if (cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &handle) == CS_ERR_OK) {
                cs_insn* insn;
                size_t n = cs_disasm(handle, data->data(), data->size(), start, totalInst, &insn);
                if (n > 0) {
                    puts("");
                    printf(BLD M "  反汇编" RST D " (PC ±)\n" RST);
                    line(D, 55);
                    for (size_t i = 0; i < n; i++) {
                        uint32_t raw = 0;
                        if (insn[i].size == 4) std::memcpy(&raw, insn[i].bytes, 4);

                        bool isCurrent = (insn[i].address == pc);
                        if (isCurrent)
                            printf(Y " ►" RST);
                        else
                            printf("  ");

                        printf(" " C "0x%lx" RST "  " D "%08x" RST "  ",
                               (unsigned long)insn[i].address, raw);

                        if (isCurrent)
                            printf(BLD Y "%-7s %s" RST "\n", insn[i].mnemonic, insn[i].op_str);
                        else
                            printf(G "%-7s" RST " " W "%s\n" RST, insn[i].mnemonic, insn[i].op_str);
                    }
                    cs_free(insn, n);
                }
                cs_close(&handle);
            }
        }
    }
    puts("");
}

void UI::cmdIl2cpp(const std::string& arg) {
    if (!ensureAttached()) return;

    if (arg == "find") {
        maps_->refresh();
        auto mod = maps_->findModule("libil2cpp.so");
        if (mod) {
            puts("");
            printf("  " G "✓" RST " " BLD W "libil2cpp.so\n" RST);
            printf("    基址  " C "0x%lx\n" RST, (unsigned long)mod->start);
            printf("    大小  " W "%zu" RST " bytes\n", mod->size());
            printf("    权限  " Y "%s\n" RST, mod->perms.c_str());
            printf("    路径  " D "%s\n" RST, mod->path.c_str());
            puts("");
        } else {
            printf("\n  " R "✗" RST " 未找到 libil2cpp.so\n");
            printf(D "    目标进程可能不是 Unity il2cpp 游戏\n" RST "\n");
        }
        return;
    }

    if (arg.empty()) {
        printf("\n  " R "✗" RST " 缺少地址\n");
        printf(D "    用法: " W "il2cpp 0x12abc000" D "  (实例地址 或 字段地址)\n" RST);
        printf(D "    或:   " W "il2cpp find" D "        (查找 libil2cpp.so)\n" RST);
        printf(D "\n    支持字段地址反推: 自动向前扫描找到对象头\n" RST);
        puts("");
        return;
    }

    try {
        addr_t addr = untag(std::stoull(arg, nullptr, 16));

        // 先尝试作为实例地址解析
        auto info = il2cpp_->inspectObject(addr);
        int32_t highlightOffset = -1; // 需要高亮的字段偏移

        if (!info) {
            // 尝试作为字段数据地址反推
            auto lookup = il2cpp_->findObjectByFieldAddr(addr);
            if (!lookup) {
                printf("\n  " R "✗" RST " 解析失败 @ " C "0x%lx\n" RST, (unsigned long)addr);
                printf(D "    地址不是有效的 il2cpp 实例或字段\n" RST "\n");
                return;
            }
            info = std::move(lookup->classInfo);
            highlightOffset = lookup->matchedFieldOffset;

            puts("");
            printf(BLD M "  类信息" RST D " (从字段地址反推)\n" RST);
            line(D, 50);
            printf("    实例      " C "0x%lx\n" RST, (unsigned long)lookup->instanceAddr);
            if (!lookup->matchedFieldName.empty()) {
                printf("    匹配字段  " BLD Y "%s" RST D " @ offset " Y "0x%x\n" RST,
                       lookup->matchedFieldName.c_str(), lookup->matchedFieldOffset);
            } else {
                printf("    偏移      " Y "0x%x" RST D " (未匹配到具体字段)\n" RST,
                       lookup->matchedFieldOffset);
            }
        } else {
            puts("");
            printf(BLD M "  类信息" RST "\n");
            line(D, 50);
        }

        printf("    名称      " BLD G "%s%s%s" RST "\n",
               info->nameSpace.empty() ? "" : info->nameSpace.c_str(),
               info->nameSpace.empty() ? "" : ".",
               info->name.c_str());
        if (!info->parentName.empty())
            printf("    父类      " W "%s\n" RST, info->parentName.c_str());
        printf("    实例大小  " W "%u\n" RST, info->instanceSize);
        printf("    Token     " C "0x%x\n" RST, info->token);
        printf("    Klass     " C "0x%lx\n" RST, (unsigned long)info->klassAddr);

        // ── 字段 ──
        puts("");
        printf(BLD M "  字段" RST D " (%zu)\n" RST, info->fields.size());
        line(D, 50);
        printf(D "    %-8s %-14s %s\n" RST, "Offset", "Type", "Name");

        for (const auto& f : info->fields) {
            bool match = (f.offset == highlightOffset);
            if (match)
                printf("  " Y ">" RST " " Y "0x%-6x" RST " " Y "%-14s" RST " " BLD Y "%s" RST "\n",
                       f.offset, f.typeName.c_str(), f.name.c_str());
            else
                printf("    " Y "0x%-6x" RST " " D "%-14s" RST " " W "%s\n" RST,
                       f.offset, f.typeName.c_str(), f.name.c_str());
        }

        // ── 方法 ──
        puts("");
        printf(BLD M "  方法" RST D " (%zu)\n" RST, info->methods.size());
        line(D, 50);
        printf(D "    %-14s %-10s %-4s %s\n" RST, "Address", "Return", "Args", "Name");

        for (const auto& m : info->methods) {
            printf("    " C "0x%-12lx" RST " " D "%-10s" RST " " W "%-4d" RST " " G "%s\n" RST,
                   (unsigned long)m.methodPointer,
                   m.returnTypeName.c_str(),
                   m.paramCount,
                   m.name.c_str());
        }
        puts("");

    } catch (...) {
        printf("\n  " R "✗" RST " 地址格式错误, 需要 16 进制如 0x12abc000\n\n");
    }
}

void UI::cmdWatch(const std::string& arg) {
    if (!ensureAttached()) return;

    std::istringstream iss(arg);
    std::string sub;
    iss >> sub;

    if (sub.empty()) {
        printf("\n  " R "✗" RST " 用法:\n");
        printf(D "    " W "watch 0x1234" D "          监控写入 (4 bytes)\n" RST);
        printf(D "    " W "watch 0x1234 rw" D "       监控读写\n" RST);
        printf(D "    " W "watch 0x1234 w 8" D "      监控写入 (8 bytes)\n" RST);
        printf(D "    " W "watch list" D "            查看观察点\n" RST);
        printf(D "    " W "watch del 0x1234" D "      删除\n" RST "\n");
        return;
    }

    if (sub == "list" || sub == "ls") {
        const auto& wps = bp_->watchList();
        if (wps.empty()) {
            if (!bp_->isAttached()) bp_->ptraceAttach();
            int maxSlots = bp_->maxWatchpoints();
            printf(D "\n  暂无观察点 (硬件槽位: %d)\n" RST, maxSlots);
            printf(D "  用 " W "watch <地址>" D " 添加\n" RST "\n");
            return;
        }
        int maxSlots = bp_->maxWatchpoints();
        puts("");
        printf(BLD M "  观察点" RST D " (硬件槽位: %zu/%d)\n" RST, wps.size(), maxSlots);
        line(D, 55);
        printf(D "  %-18s  %-6s  %-4s  %-6s  %s\n" RST, "地址", "大小", "模式", "命中", "状态");
        for (const auto& [addr, wp] : wps) {
            const char* modeStr = wp.mode == WatchpointInfo::Write ? "W" :
                                  wp.mode == WatchpointInfo::Read  ? "R" : "RW";
            printf("  " C "0x%016lx" RST "  %-6zu  %-4s  " W "%-6u" RST "  %s\n",
                   (unsigned long)addr, wp.size, modeStr, wp.hitCount,
                   wp.enabled ? (G "启用" RST) : (R "禁用" RST));
        }
        puts("");
        return;
    }

    if (sub == "del" || sub == "rm") {
        std::string s;
        iss >> s;
        try {
            addr_t addr = untag(std::stoull(s, nullptr, 16));
            if (bp_->watchRemove(addr))
                printf("\n  " G "✓" RST " 观察点已删除 @ " C "0x%lx\n" RST "\n", (unsigned long)addr);
            else
                printf("\n  " R "✗" RST " 该地址无观察点\n\n");
        } catch (...) {
            printf("\n  " R "✗" RST " 用法: " W "watch del 0x1234\n" RST "\n");
        }
        return;
    }

    // watch <地址> [模式] [大小]
    addr_t addr;
    try {
        addr = untag(std::stoull(sub, nullptr, 16));
    } catch (...) {
        printf("\n  " R "✗" RST " 地址格式错误\n\n");
        return;
    }

    // watch <地址> [模式] [大小]
    std::string arg1, arg2;
    iss >> arg1 >> arg2;

    auto mode = WatchpointInfo::Write;
    size_t size = 4;

    if (arg1 == "r" || arg1 == "read")       mode = WatchpointInfo::Read;
    else if (arg1 == "rw" || arg1 == "both")  mode = WatchpointInfo::ReadWrite;
    else if (arg1 == "w" || arg1 == "write")  mode = WatchpointInfo::Write;

    if (!arg2.empty()) {
        try { size = std::stoull(arg2); } catch (...) {}
    }

    const char* modeLabel = mode == WatchpointInfo::Write ? "写入" :
                            mode == WatchpointInfo::Read  ? "读取" : "读写";

    if (bp_->watchAdd(addr, size, mode)) {
        printf("\n  " G "✓" RST " 观察点 @ " C "0x%lx" RST "  监控%s (%zu bytes)\n",
               (unsigned long)addr, modeLabel, size);
        printf(D "    命中自动打印, 不暂停目标进程\n" RST);
        printf(D "    用 " W "watch del 0x%lx" D " 取消\n" RST "\n", (unsigned long)addr);
    } else {
        int e = errno;
        if (!bp_->isAttached()) bp_->ptraceAttach();
        int maxSlots = bp_->maxWatchpoints();
        printf("\n  " R "✗" RST " 添加失败");
        if (e) printf(D " (errno=%d: %s)" RST, e, strerror(e));
        puts("");
        if (maxSlots <= 0)
            printf(D "    硬件不支持观察点, 或 ptrace 权限不足\n" RST "\n");
        else
            printf(D "    槽位: %zu/%d  地址: 0x%lx  大小: %zu\n" RST "\n",
                   bp_->watchList().size(), maxSlots, (unsigned long)addr, size);
    }
}

void UI::cmdDisasm(const std::string& arg) {
    if (!ensureAttached()) return;

    std::istringstream iss(arg);
    std::string addrStr, countStr;
    iss >> addrStr >> countStr;

    if (addrStr.empty()) {
        printf("\n  " R "x" RST " 用法:\n");
        printf("    " W "disasm <地址>" RST "            往后 16 条\n");
        printf("    " W "disasm <地址> 30" RST "         往后 30 条\n");
        printf("    " W "disasm <地址> -10 +20" RST "    前10条 后20条\n");
        printf("    " W "disasm <地址> func" RST "       整个函数\n");
        puts("");
        return;
    }

    addr_t addr;
    try { addr = untag(std::stoull(addrStr, nullptr, 16)); } catch (...) {
        printf("\n  " R "x" RST " 地址格式错误\n\n"); return;
    }

    // func 模式: 自动检测函数边界
    if (countStr == "func" || countStr == "fn" || countStr == "f") {
        auto bounds = findFunctionBounds(*mem_, addr);
        if (!bounds) {
            printf("\n  " R "x" RST " 函数范围异常\n\n");
            return;
        }
        addr_t funcStart = bounds->start;
        addr_t funcEnd = bounds->end;

        int total = static_cast<int>(bounds->size() / 4);
        if (total > 1024) total = 1024;

        size_t dataSize = total * 4;
        auto data = mem_->readBuffer(funcStart, dataSize);
        if (!data) {
            printf("\n  " R "x" RST " 读取失败\n\n");
            return;
        }

        csh handle;
        if (cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &handle) != CS_ERR_OK) {
            printf("\n  " R "x" RST " capstone 失败\n\n");
            return;
        }

        cs_insn* insn;
        size_t n = cs_disasm(handle, data->data(), data->size(), funcStart, total, &insn);

        lastOutput_.clear();
        puts("");
        printf(BLD M "  函数" RST D " 0x%lx ~ 0x%lx (%zu 条)\n" RST,
               (unsigned long)funcStart, (unsigned long)funcEnd, n);
        line(D, 60);

        if (n > 0) {
            for (size_t i = 0; i < n; i++) {
                uint32_t raw = 0;
                if (insn[i].size == 4) std::memcpy(&raw, insn[i].bytes, 4);

                bool isTarget = (insn[i].address == addr);
                std::string mn = insn[i].mnemonic;

                if (isTarget) printf(Y " >>" RST);
                else printf("  ");

                printf(" " C "0x%lx" RST "  " D "%08x" RST "  ", (unsigned long)insn[i].address, raw);

                if (mn == "ret")
                    printf(R "%-7s %s" RST, insn[i].mnemonic, insn[i].op_str);
                else if (mn == "b" || mn == "bl" || mn == "br" || mn == "blr" ||
                         mn == "cbz" || mn == "cbnz" || mn == "tbz" || mn == "tbnz" ||
                         mn.substr(0, 2) == "b.")
                    printf(Y "%-7s %s" RST, insn[i].mnemonic, insn[i].op_str);
                else if (isTarget)
                    printf(BLD W "%-7s %s" RST, insn[i].mnemonic, insn[i].op_str);
                else
                    printf(G "%-7s" RST " " W "%s" RST, insn[i].mnemonic, insn[i].op_str);

                printf("\n");
                record("0x%lx  %08x  %-7s %s\n",
                       (unsigned long)insn[i].address, raw, insn[i].mnemonic, insn[i].op_str);
            }
            cs_free(insn, n);
        }
        cs_close(&handle);
        puts("");
        return;
    }

    // 解析 countStr 和可能的第二个参数
    // 格式: 30 | -10 | -10 +20 | +20
    std::string countStr2;
    iss >> countStr2;

    int before = 0, after = 0;
    bool hasSpec = false;

    for (const auto& s : {countStr, countStr2}) {
        if (s.empty()) continue;
        try {
            if (s[0] == '-') {
                before = std::abs(std::stoi(s));
                hasSpec = true;
            } else if (s[0] == '+') {
                after = std::stoi(s.substr(1));
                hasSpec = true;
            } else {
                int v = std::stoi(s);
                if (v < 0) { before = -v; hasSpec = true; }
                else { after = v; hasSpec = true; }
            }
        } catch (...) {}
    }

    if (!hasSpec) { before = 0; after = 16; }
    if (hasSpec && after == 0 && before > 0) after = 10; // -20 默认后面 10 条
    if (before > 200) before = 200;
    if (after < 1 && before == 0) after = 16;
    if (after > 500) after = 500;

    int count = before + after;
    addr_t startAddr = addr - before * 4;

    size_t dataSize = count * 4;
    auto data = mem_->readBuffer(startAddr, dataSize);
    if (!data) {
        printf("\n  " R "x" RST " 读取失败 @ 0x%lx\n\n", (unsigned long)startAddr);
        return;
    }

    csh handle;
    if (cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &handle) != CS_ERR_OK) {
        printf("\n  " R "x" RST " capstone 初始化失败\n\n");
        return;
    }

    cs_insn* insn;
    size_t n = cs_disasm(handle, data->data(), data->size(), startAddr, count, &insn);

    lastOutput_.clear();
    puts("");
    printf(BLD M "  反汇编" RST D " @ 0x%lx (%zu 条)\n" RST, (unsigned long)startAddr, n);
    line(D, 60);

    if (n > 0) {
        for (size_t i = 0; i < n; i++) {
            uint32_t raw = 0;
            if (insn[i].size == 4) std::memcpy(&raw, insn[i].bytes, 4);

            bool isTarget = (insn[i].address == addr && before > 0);
            std::string mn = insn[i].mnemonic;

            // 函数边界标记
            bool isFuncStart = (mn == "stp" && std::string(insn[i].op_str).find("x29") != std::string::npos
                                && std::string(insn[i].op_str).find("x30") != std::string::npos);
            bool isFuncEnd = (mn == "ret");

            if (isFuncStart && i > 0) {
                printf(D "  ---- function ----\n" RST);
            }

            if (isTarget)
                printf(Y " >>" RST);
            else
                printf("  ");

            printf(" " C "0x%lx" RST "  " D "%08x" RST "  ",
                   (unsigned long)insn[i].address, raw);

            // 着色: 分支=黄, ret=红, 普通=绿
            if (mn == "b" || mn == "bl" || mn == "br" || mn == "blr" ||
                mn == "cbz" || mn == "cbnz" || mn == "tbz" || mn == "tbnz" ||
                mn.substr(0, 2) == "b.") {
                printf(Y "%-7s %s" RST, insn[i].mnemonic, insn[i].op_str);
                // bl 附加符号名
                if (mn == "bl") {
                    addr_t ct = 0;
                    std::string op = insn[i].op_str;
                    try { ct = untag(std::stoull(op.substr(op[0] == '#' ? 1 : 0), nullptr, 16)); } catch (...) {}
                    if (ct) {
                        auto sn = syms_.resolve(ct);
                        if (!sn.empty()) printf(D "  // %s" RST, sn.c_str());
                    }
                }
            }
            else if (isFuncEnd)
                printf(R "%-7s %s" RST, insn[i].mnemonic, insn[i].op_str);
            else if (isTarget)
                printf(BLD W "%-7s %s" RST, insn[i].mnemonic, insn[i].op_str);
            else
                printf(G "%-7s" RST " " W "%s" RST, insn[i].mnemonic, insn[i].op_str);

            printf("\n");

            record("0x%lx  %08x  %-7s %s\n",
                   (unsigned long)insn[i].address, raw, insn[i].mnemonic, insn[i].op_str);

            if (isFuncEnd && i < n - 1) {
                printf(D "  ---- end ----\n" RST);
            }
        }
        cs_free(insn, n);
    } else {
        printf("  " R "x" RST " 反汇编失败: %s\n", cs_strerror(cs_errno(handle)));
    }

    cs_close(&handle);
    puts("");
}

void UI::cmdPatch(const std::string& arg) {
    if (!ensureAttached()) return;

    std::istringstream iss(arg);
    std::string addrStr, what;
    iss >> addrStr;
    std::getline(iss, what);

    if (addrStr.empty()) {
        printf("\n  " R "x" RST " 用法:\n");
        printf("    " W "patch <地址> nop" RST "            写入 NOP\n");
        printf("    " W "patch <地址> nop 5" RST "          写入 5 个 NOP\n");
        printf("    " W "patch <地址> ret" RST "            写入 RET (函数直接返回)\n");
        printf("    " W "patch <地址> ret0" RST "           MOV X0,#0 + RET\n");
        printf("    " W "patch <地址> ret1" RST "           MOV X0,#1 + RET\n");
        printf("    " W "patch <地址> mov0" RST "           MOV W0,#0 (结果归零)\n");
        printf("    " W "patch <地址> hex D503201F" RST "   写入原始指令 hex\n");
        puts("");
        return;
    }

    addr_t addr;
    try { addr = untag(std::stoull(addrStr, nullptr, 16)); } catch (...) {
        printf("\n  " R "x" RST " 地址格式错误\n\n"); return;
    }

    // 去前导空格
    size_t ws = what.find_first_not_of(" \t");
    if (ws == std::string::npos) {
        printf("\n  " R "x" RST " 缺少指令, 输入 " W "patch" RST " 查看用法\n\n");
        return;
    }
    what = what.substr(ws);

    // 先显示原始指令
    auto origData = mem_->readBuffer(addr, 4);
    uint32_t origInst = 0;
    if (origData) std::memcpy(&origInst, origData->data(), 4);

    std::vector<uint32_t> instructions;

    // 解析指令
    std::string cmd = what;
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    // 提取第一个词
    std::string firstWord;
    std::istringstream wiss(cmd);
    wiss >> firstWord;

    if (firstWord == "nop") {
        int count = 1;
        std::string countStr;
        wiss >> countStr;
        if (!countStr.empty()) {
            try { count = std::stoi(countStr); } catch (...) {}
        }
        if (count < 1) count = 1;
        if (count > 64) count = 64;
        for (int i = 0; i < count; i++)
            instructions.push_back(0xD503201F); // NOP
    }
    else if (firstWord == "ret") {
        instructions.push_back(0xD65F03C0); // RET
    }
    else if (firstWord == "ret0") {
        instructions.push_back(0xD2800000); // MOV X0, #0
        instructions.push_back(0xD65F03C0); // RET
    }
    else if (firstWord == "ret1") {
        instructions.push_back(0xD2800020); // MOV X0, #1
        instructions.push_back(0xD65F03C0); // RET
    }
    else if (firstWord == "mov0") {
        instructions.push_back(0x52800000); // MOV W0, #0
    }
    else if (firstWord == "mov1") {
        instructions.push_back(0x52800020); // MOV W0, #1
    }
    else if (firstWord == "brk") {
        instructions.push_back(0xD4200000); // BRK #0
    }
    else if (firstWord == "hex") {
        // 原始 hex: patch 0x1234 hex D503201F D503201F
        std::string hexStr;
        while (wiss >> hexStr) {
            try {
                uint32_t val = static_cast<uint32_t>(std::stoul(hexStr, nullptr, 16));
                instructions.push_back(val);
            } catch (...) {
                printf("\n  " R "x" RST " 无效 hex: %s\n\n", hexStr.c_str());
                return;
            }
        }
    }
    else {
        // 尝试用 keystone 汇编
        ks_engine* ks;
        if (ks_open(KS_ARCH_ARM64, KS_MODE_LITTLE_ENDIAN, &ks) != KS_ERR_OK) {
            printf("\n  " R "x" RST " 汇编器初始化失败\n\n");
            return;
        }

        unsigned char* encode = nullptr;
        size_t encSize = 0, statCount = 0;
        // what 是完整的汇编文本 (可能多条, 用 ; 分隔)
        int rc = ks_asm(ks, what.c_str(), addr, &encode, &encSize, &statCount);

        if (rc != 0 || encSize == 0) {
            printf("\n  " R "x" RST " 汇编失败: %s\n", ks_strerror(ks_errno(ks)));
            printf(D "    输入: %s\n" RST, what.c_str());
            printf(D "    快捷: nop ret ret0 ret1 mov0 mov1 brk\n" RST);
            printf(D "    示例: " W "patch <地址> add w8, w8, #100\n" RST);
            printf(D "          " W "patch <地址> mov w0, #999\n" RST);
            printf(D "          " W "patch <地址> sub w8, w8, w10\n" RST "\n");
            ks_close(ks);
            return;
        }

        // 每 4 字节一条指令
        for (size_t i = 0; i + 3 < encSize; i += 4) {
            uint32_t inst;
            std::memcpy(&inst, encode + i, 4);
            instructions.push_back(inst);
        }

        ks_free(encode);
        ks_close(ks);
    }

    if (instructions.empty()) {
        printf("\n  " R "x" RST " 无有效指令\n\n");
        return;
    }

    // 显示修改前
    puts("");
    printf(D "  修改前:\n" RST);
    auto beforeData = mem_->readBuffer(addr, instructions.size() * 4);
    if (beforeData) {
        csh cs;
        if (cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &cs) == CS_ERR_OK) {
            cs_insn* insn;
            size_t n = cs_disasm(cs, beforeData->data(), beforeData->size(), addr, 0, &insn);
            for (size_t i = 0; i < n; i++) {
                uint32_t raw = 0;
                if (insn[i].size == 4) std::memcpy(&raw, insn[i].bytes, 4);
                printf("    " D "0x%lx  %08x  %s %s\n" RST,
                       (unsigned long)insn[i].address, raw, insn[i].mnemonic, insn[i].op_str);
            }
            if (n) cs_free(insn, n);
            cs_close(&cs);
        }
    }

    // 写入: 代码段 (r-xp) 不能用 process_vm_writev, 必须走 ptrace
    bool ok = true;

    // 先试 process_vm_writev (数据段能成功)
    for (size_t i = 0; i < instructions.size(); i++) {
        if (!mem_->writeRaw(addr + i * 4, &instructions[i], 4)) {
            ok = false;
            break;
        }
    }

    if (!ok) {
        // 代码段写不了, 用 ptrace POKETEXT
        ok = true;
        bool wasAttached = bp_->isAttached();

        // ptrace attach (如果还没有)
        if (!wasAttached && !bp_->ptraceAttach()) {
            printf("  " R "x" RST " 需要 ptrace 权限才能修改代码段\n\n");
            return;
        }

        // 通过 Breakpoint 管理停止/恢复
        bp_->stopProcess();

        for (size_t i = 0; i < instructions.size(); i++) {
            addr_t target = addr + i * 4;
            addr_t aligned = target & ~7UL;
            int off = target & 7;

            errno = 0;
            long word = ptrace(PTRACE_PEEKTEXT, proc_.pid(),
                               reinterpret_cast<void*>(aligned), nullptr);
            if (errno != 0) { ok = false; break; }

            std::memcpy(reinterpret_cast<char*>(&word) + off, &instructions[i], 4);

            if (ptrace(PTRACE_POKETEXT, proc_.pid(),
                       reinterpret_cast<void*>(aligned),
                       reinterpret_cast<void*>(word)) < 0) {
                ok = false;
                break;
            }
        }

        // 恢复运行
        bp_->resumeProcess();

        // 如果之前未 attach, 恢复原状
        if (!wasAttached) bp_->ptraceDetach();
    }

    if (!ok) {
        printf("  " R "x" RST " 写入失败\n\n");
        return;
    }

    // 显示修改后
    printf(D "  修改后:\n" RST);
    auto afterData = mem_->readBuffer(addr, instructions.size() * 4);
    if (afterData) {
        csh cs;
        if (cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &cs) == CS_ERR_OK) {
            cs_insn* insn;
            size_t n = cs_disasm(cs, afterData->data(), afterData->size(), addr, 0, &insn);
            for (size_t i = 0; i < n; i++) {
                uint32_t raw = 0;
                if (insn[i].size == 4) std::memcpy(&raw, insn[i].bytes, 4);
                printf("    " G "0x%lx  %08x  %s %s\n" RST,
                       (unsigned long)insn[i].address, raw, insn[i].mnemonic, insn[i].op_str);
            }
            if (n) cs_free(insn, n);
            cs_close(&cs);
        }
    }

    printf("\n  " G "+" RST " 已修改 %zu 条指令 @ " C "0x%lx\n" RST "\n",
           instructions.size(), (unsigned long)addr);
}

void UI::cmdDumpFn(const std::string& arg) {
    if (!ensureAttached()) return;

    std::istringstream iss(arg);
    std::string addrStr, filename;
    iss >> addrStr >> filename;

    if (addrStr.empty()) {
        printf("\n  " R "x" RST " 用法: " W "dumpfn <函数内地址> [文件名]\n" RST);
        printf(D "    导出整个函数的 bin + asm, 可在电脑用 Ghidra/IDA 打开\n" RST "\n");
        return;
    }

    addr_t addr;
    try { addr = untag(std::stoull(addrStr, nullptr, 16)); } catch (...) {
        printf("\n  " R "x" RST " 地址格式错误\n\n"); return;
    }

    auto bounds = findFunctionBounds(*mem_, addr);
    if (!bounds) {
        printf("\n  " R "x" RST " 函数范围异常\n\n");
        return;
    }
    addr_t funcStart = bounds->start;
    addr_t funcEnd = bounds->end;
    size_t funcSize = bounds->size();

    auto data = mem_->readBuffer(funcStart, funcSize);
    if (!data) {
        printf("\n  " R "x" RST " 读取失败\n\n");
        return;
    }

    // 默认文件名
    if (filename.empty()) {
        char buf[128];
        snprintf(buf, sizeof(buf), "/storage/emulated/0/TsEngine/func_0x%lx", (unsigned long)funcStart);
        filename = buf;
    }

    // 写 .bin (原始机器码, Ghidra/IDA 可直接加载)
    std::string binFile = filename + ".bin";
    {
        std::ofstream f(binFile, std::ios::binary);
        f.write(reinterpret_cast<const char*>(data->data()), data->size());
    }

    // 写 .asm (反汇编文本)
    std::string asmFile = filename + ".asm";
    {
        std::ofstream f(asmFile);
        f << "; TsEngine function dump\n";
        f << "; Process: " << proc_.name() << " (PID:" << proc_.pid() << ")\n";
        f << "; Range: 0x" << std::hex << funcStart << " - 0x" << funcEnd << "\n";
        f << "; Size: " << std::dec << funcSize << " bytes (" << funcSize / 4 << " instructions)\n";
        f << "; Load in Ghidra: File > Import > Raw Binary > ARM:AARCH64:v8A > Base 0x"
          << std::hex << funcStart << "\n\n";

        csh handle;
        if (cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &handle) == CS_ERR_OK) {
            cs_insn* insn;
            size_t n = cs_disasm(handle, data->data(), data->size(), funcStart, 0, &insn);
            for (size_t i = 0; i < n; i++) {
                uint32_t raw = 0;
                if (insn[i].size == 4) std::memcpy(&raw, insn[i].bytes, 4);
                f << std::hex << "0x" << insn[i].address << "  "
                  << std::setw(8) << std::setfill('0') << raw << "  "
                  << insn[i].mnemonic << " " << insn[i].op_str << "\n";
            }
            if (n) cs_free(insn, n);
            cs_close(&handle);
        }
    }

    puts("");
    printf("  " G "+" RST " 函数 " C "0x%lx" RST " ~ " C "0x%lx" RST " (%zu bytes, %zu inst)\n",
           (unsigned long)funcStart, (unsigned long)funcEnd, funcSize, funcSize / 4);
    printf("  " D "bin: " RST W "%s\n" RST, binFile.c_str());
    printf("  " D "asm: " RST W "%s\n" RST, asmFile.c_str());
    printf(D "\n  Ghidra 加载:\n" RST);
    printf(D "    File > Import > Raw Binary\n" RST);
    printf(D "    Language: AARCH64:LE:64:v8A\n" RST);
    printf(D "    Base Address: 0x%lx\n" RST "\n", (unsigned long)funcStart);
}

// ── 伪 C 反编译器 (已提取到 Decompiler.h/cpp) ──


void UI::cmdDecompile(const std::string& arg) {
    if (!ensureAttached()) return;

    if (arg.empty()) {
        printf("\n  " R "x" RST " 用法: " W "dec <函数内地址>\n" RST "\n");
        return;
    }

    addr_t addr;
    try { addr = untag(std::stoull(arg, nullptr, 16)); } catch (...) {
        printf("\n  " R "x" RST " 地址格式错误\n\n"); return;
    }

    // 找函数头尾
    auto bounds = findFunctionBounds(*mem_, addr);
    if (!bounds) {
        printf("\n  " R "x" RST " 函数范围异常\n\n"); return;
    }
    addr_t funcStart = bounds->start;
    addr_t funcEnd = bounds->end;
    size_t funcSize = bounds->size();

    auto data = mem_->readBuffer(funcStart, funcSize);
    if (!data) { printf("\n  " R "x" RST " 读取失败\n\n"); return; }

    auto result = decompile(*mem_, syms_, funcStart, funcEnd, addr, data->data(), data->size());
    if (result.lines.empty()) {
        printf("\n  " R "x" RST " 反编译失败\n\n"); return;
    }

    lastOutput_.clear();

    // 打印函数签名
    puts("");
    printf(BLD M "  伪 C" RST D " @ 0x%lx (%zu inst, stack: 0x%x)\n" RST,
           (unsigned long)funcStart, result.lines.size(), result.stackFrame);
    line(D, 60);

    printf("  " D "// 0x%lx - 0x%lx\n" RST, (unsigned long)funcStart, (unsigned long)funcEnd);
    printf("  " C "%s" RST C " {" RST "\n", result.signature.c_str());
    record("// 0x%lx - 0x%lx\n%s {\n", (unsigned long)funcStart, (unsigned long)funcEnd, result.signature.c_str());

    for (const auto& dl : result.lines) {
        if (dl.isTarget)
            printf("  " Y ">>  " RST BLD W "%s" RST, dl.code.c_str());
        else if (dl.code.find("//") == 0)
            printf("      " D "%s" RST, dl.code.c_str());
        else if (dl.code.find("return") == 0)
            printf("      " R "%s" RST, dl.code.c_str());
        else if (dl.code.find("if ") == 0 || dl.code.find("goto ") == 0)
            printf("      " Y "%s" RST, dl.code.c_str());
        else if (dl.code.find("();") != std::string::npos)
            printf("      " C "%s" RST, dl.code.c_str());
        else
            printf("      " W "%s" RST, dl.code.c_str());

        if (!dl.comment.empty())
            printf(D "  // " RST M "%s" RST D "  %lx\n" RST, dl.comment.c_str(), (unsigned long)dl.address);
        else
            printf(D "  // %lx\n" RST, (unsigned long)dl.address);
        record("    %s  // %s %lx\n", dl.code.c_str(), dl.comment.c_str(), (unsigned long)dl.address);
    }

    printf("  " C "}" RST "\n");
    record("}\n");
    puts("");
}

void UI::cmdCall(const std::string& arg) {
    if (!ensureAttached() || !remote_) return;

    if (arg.empty()) {
        printf("\n  " R "x" RST " 用法:\n");
        printf("    " W "call <函数名|地址> [参数...]\n" RST);
        puts("");
        printf(D "  参数类型:\n" RST);
        printf(D "    数字        " W "42" D " 或 " W "0x2a" D " (十进制/十六进制)\n" RST);
        printf(D "    字符串      " W "\"hello %%d\"" D " (自动写入目标内存)\n" RST);
        printf(D "    地址        " W "0x7f001000\n" RST);
        puts("");
        printf(D "  示例:\n" RST);
        printf("    " W "call getpid\n" RST);
        printf("    " W "call printf \"hello world\\n\"\n" RST);
        printf("    " W "call printf \"HP = %%d\\n\" 1000\n" RST);
        printf("    " W "call takeDamage 0x7832024060 75\n" RST);
        puts("");
        return;
    }

    // 解析: 函数名/地址 + 参数列表 (支持 "字符串" 参数)
    // 手动解析而非 istringstream, 因为需要处理引号
    std::string rest = arg;
    size_t sp = rest.find(' ');
    std::string addrStr = (sp != std::string::npos) ? rest.substr(0, sp) : rest;
    std::string argsPart = (sp != std::string::npos) ? rest.substr(sp + 1) : "";

    // 解析函数地址
    addr_t funcAddr = 0;
    std::string funcName;
    bool isAddr = (addrStr.size() > 2 && addrStr[0] == '0' && (addrStr[1] == 'x' || addrStr[1] == 'X'));
    if (isAddr) {
        try { funcAddr = untag(std::stoull(addrStr, nullptr, 16)); } catch (...) {
            printf("\n  " R "x" RST " 地址格式错误\n\n"); return;
        }
        funcName = syms_.resolve(funcAddr);
    } else {
        funcAddr = syms_.findByName(addrStr);
        if (!funcAddr) {
            printf("\n  " R "x" RST " 找不到函数: " W "%s\n" RST, addrStr.c_str());
            printf(D "    用 disasm 或 maps 查看可用函数\n" RST "\n");
            return;
        }
        funcName = syms_.resolve(funcAddr);
        if (funcName.empty()) funcName = addrStr;
    }

    // 解析参数: 支持 "字符串" 和数字
    std::vector<addr_t> args;
    std::vector<addr_t> strAddrs; // 分配的字符串地址, 调用后释放
    std::vector<std::string> argDisplay; // 用于打印

    size_t pos = 0;
    while (pos < argsPart.size()) {
        // 跳过空格
        while (pos < argsPart.size() && argsPart[pos] == ' ') pos++;
        if (pos >= argsPart.size()) break;

        if (argsPart[pos] == '"') {
            // 字符串参数: 提取到闭合引号
            pos++; // skip opening "
            std::string str;
            while (pos < argsPart.size() && argsPart[pos] != '"') {
                if (argsPart[pos] == '\\' && pos + 1 < argsPart.size()) {
                    pos++;
                    switch (argsPart[pos]) {
                        case 'n': str += '\n'; break;
                        case 't': str += '\t'; break;
                        case '\\': str += '\\'; break;
                        case '"': str += '"'; break;
                        case '0': str += '\0'; break;
                        default: str += '\\'; str += argsPart[pos]; break;
                    }
                } else {
                    str += argsPart[pos];
                }
                pos++;
            }
            if (pos < argsPart.size()) pos++; // skip closing "

            // 写字符串到目标进程
            addr_t strAddr = remote_->writeString(str);
            if (!strAddr) {
                printf("\n  " R "x" RST " 字符串写入失败\n\n");
                for (auto sa : strAddrs) remote_->remoteFree(sa, 0x1000);
                return;
            }
            args.push_back(strAddr);
            strAddrs.push_back(strAddr);
            argDisplay.push_back("\"" + str + "\"");
        } else {
            // 数字参数
            std::string tok;
            while (pos < argsPart.size() && argsPart[pos] != ' ') {
                tok += argsPart[pos++];
            }
            try {
                args.push_back(std::stoull(tok, nullptr, 0));
                argDisplay.push_back(tok);
            } catch (...) {
                printf("\n  " R "x" RST " 参数格式错误: %s\n\n", tok.c_str());
                for (auto sa : strAddrs) remote_->remoteFree(sa, 0x1000);
                return;
            }
        }
    }

    // 打印调用信息
    auto fname = syms_.resolve(funcAddr);
    if (fname.empty()) fname = funcName;
    printf(D "\n  调用 " RST C "0x%lx" RST, (unsigned long)funcAddr);
    if (!fname.empty()) printf(D " (%s)" RST, fname.c_str());
    printf("(");
    for (size_t i = 0; i < argDisplay.size(); i++) {
        if (i) printf(", ");
        if (argDisplay[i][0] == '"')
            printf(Y "%s" RST, argDisplay[i].c_str());
        else
            printf(W "%s" RST, argDisplay[i].c_str());
    }
    printf(")\n");
    fflush(stdout);

    auto result = remote_->call(funcAddr, args);

    // 释放分配的字符串内存
    for (auto sa : strAddrs) remote_->remoteFree(sa, 0x1000);

    if (result.success) {
        printf("  " G "+" RST " 返回: " C "0x%lx" RST " (" W "%ld" RST ")\n\n",
               (unsigned long)result.retval, (long)result.retval);
    } else {
        int e = errno;
        printf("  " R "x" RST " 调用失败");
        if (e) printf(D " (errno=%d: %s)" RST, e, strerror(e));
        printf("\n\n");
    }
}

void UI::cmdHook(const std::string& arg) {
    if (!ensureAttached() || !remote_) return;

    std::istringstream iss(arg);
    std::string sub;
    iss >> sub;

    if (sub.empty()) {
        printf("\n  " R "x" RST " 用法:\n");
        printf("    " W "hook <地址> ret0" RST "     hook 后返回 0 (跳过函数)\n");
        printf("    " W "hook <地址> ret1" RST "     hook 后返回 1\n");
        printf("    " W "hook <地址> nop" RST "      hook 后直接 ret\n");
        printf("    " W "hook <地址> log" RST "      执行原函数 (保留行为)\n");
        printf("    " W "hook list" RST "            查看 hook 列表\n");
        printf("    " W "hook del <地址>" RST "      删除 hook\n");
        puts("");
        return;
    }

    if (sub == "list" || sub == "ls") {
        const auto& hooks = remote_->hooks();
        if (hooks.empty()) {
            printf(D "\n  无 hook\n" RST "\n");
            return;
        }
        puts("");
        printf(BLD M "  Hook 列表\n" RST);
        line(D, 55);
        for (size_t i = 0; i < hooks.size(); i++) {
            const auto& h = hooks[i];
            auto fname = syms_.resolve(h.target);
            printf("  " W "#%zu" RST "  " C "0x%lx" RST, i + 1, (unsigned long)h.target);
            if (!fname.empty()) printf(" " D "(%s)" RST, fname.c_str());
            printf("  -> " Y "%s" RST "  tramp:" D "0x%lx\n" RST,
                   h.name.c_str(), (unsigned long)h.trampoline);
        }
        puts("");
        return;
    }

    if (sub == "del" || sub == "rm") {
        std::string s;
        iss >> s;
        try {
            addr_t addr = untag(std::stoull(s, nullptr, 16));
            if (remote_->unhookFunction(addr)) {
                printf("\n  " G "+" RST " 已恢复 @ " C "0x%lx\n" RST "\n", (unsigned long)addr);
            } else {
                printf("\n  " R "x" RST " 该地址无 hook\n\n");
            }
        } catch (...) {
            printf("\n  " R "x" RST " 用法: " W "hook del <地址>\n" RST "\n");
        }
        return;
    }

    // hook <地址|函数名> <动作>
    addr_t target = 0;
    bool isSub = (sub.size() > 2 && sub[0] == '0' && (sub[1] == 'x' || sub[1] == 'X'));
    if (isSub) {
        try { target = untag(std::stoull(sub, nullptr, 16)); } catch (...) {
            printf("\n  " R "x" RST " 地址格式错误\n\n"); return;
        }
    } else {
        target = syms_.findByName(sub);
        if (!target) {
            printf("\n  " R "x" RST " 找不到函数: " W "%s\n" RST "\n", sub.c_str()); return;
        }
    }

    std::string action;
    iss >> action;
    if (action.empty()) action = "ret0"; // 默认 ret0

    auto fname = syms_.resolve(target);
    printf(D "\n  hook " RST C "0x%lx" RST, (unsigned long)target);
    if (!fname.empty()) printf(D " (%s)" RST, fname.c_str());
    printf(D " -> " RST Y "%s\n" RST, action.c_str());
    fflush(stdout);

    if (remote_->hookFunction(target, action)) {
        printf("  " G "+" RST " hook 已设置\n");
        printf(D "    删除: " W "hook del 0x%lx\n" RST "\n", (unsigned long)target);
    } else {
        printf("  " R "x" RST " hook 失败 (mmap 或写入失败)\n\n");
    }
}

void UI::cmdScan(const std::string& arg) {
    if (!ensureAttached()) return;

    if (arg.empty()) {
        printf("\n  " R "x" RST " 用法: " W "scan <地址>\n" RST);
        printf(D "    扫描谁指向这个地址 (自动去标签, 自动导出)\n" RST "\n");
        return;
    }

    addr_t target;
    try {
        target = untag(std::stoull(arg, nullptr, 16));
    } catch (...) {
        printf("\n  " R "x" RST " 地址格式错误\n\n");
        return;
    }

    maps_->refresh();
    const auto& regions = maps_->regions();
    lastOutput_.clear();

    printf(D "\n  扫描指向 0x%lx 的指针...\n" RST, (unsigned long)target);
    fflush(stdout);

    auto startTime = std::chrono::steady_clock::now();
    auto results = mem_->scanPointers(target, regions, 10000);
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime).count();

    if (results.empty()) {
        printf("  " R "x" RST " 未找到 (%.1fs)\n\n", elapsed / 1000.0);
        return;
    }

    // 导出
    time_t t = time(nullptr);
    char fname[128];
    strftime(fname, sizeof(fname), "/storage/emulated/0/TsEngine/scan_%Y%m%d_%H%M%S.txt", localtime(&t));

    std::ofstream out(fname);
    if (out.is_open()) {
        out << "# TsEngine 指针扫描\n";
        out << "# 进程: " << proc_.name() << " (PID:" << proc_.pid() << ")\n";
        out << "# target=0x" << std::hex << target << "\n";
        out << std::dec << "# " << results.size() << " 条  " << elapsed << "ms\n\n";
        for (const auto& r : results)
            out << "0x" << std::hex << r.foundAt << "  " << r.region << "\n";
        out.close();
    }

    // 屏幕显示前 20 条
    puts("");
    printf(BLD M "  指针扫描" RST D " (%zu 个, %.1fs)\n" RST, results.size(), elapsed / 1000.0);
    line(D, 55);
    printf(D "  %-18s  %s\n" RST, "地址", "区域");

    size_t show = std::min(results.size(), (size_t)20);
    for (size_t i = 0; i < show; i++) {
        printf("  " C "0x%016lx" RST "  " D "%s\n" RST,
               (unsigned long)results[i].foundAt, results[i].region.c_str());
    }
    if (results.size() > 20)
        printf(D "  ... 共 %zu 个\n" RST, results.size());

    printf(D "\n  已导出: " RST W "%s\n" RST "\n", fname);
}

// ── dump 导出 ──
void UI::cmdDump(const std::string& arg) {
    if (lastOutput_.empty()) {
        printf("\n  " R "✗" RST " 没有可导出的结果\n");
        printf(D "    先执行 scan / il2cpp / maps / read 等命令\n" RST "\n");
        return;
    }

    // 默认文件名: tsengine_<时间>.txt
    std::string filename = arg;
    if (filename.empty()) {
        time_t t = time(nullptr);
        struct tm* tm = localtime(&t);
        char buf[64];
        strftime(buf, sizeof(buf), "/storage/emulated/0/TsEngine/dump_%Y%m%d_%H%M%S.txt", tm);
        filename = buf;
    }

    std::ofstream f(filename);
    if (!f.is_open()) {
        printf("\n  " R "✗" RST " 无法创建文件: %s\n\n", filename.c_str());
        return;
    }

    // 写入头部信息
    f << "# TsEngine dump\n";
    {
        time_t t = time(nullptr);
        char buf[64];
        strftime(buf, sizeof(buf), "# %Y-%m-%d %H:%M:%S", localtime(&t));
        f << buf << "\n";
    }
    if (proc_.isAttached()) {
        f << "# Process: " << proc_.name() << " (PID:" << proc_.pid() << ")\n";
    }
    f << "#\n\n";

    // 写入内容 (去掉 ANSI 颜色)
    f << stripAnsi(lastOutput_);
    f.close();

    printf("\n  " G "✓" RST " 已导出到 " W "%s" RST D " (%zu bytes)\n" RST "\n",
           filename.c_str(), lastOutput_.size());
}

// ── readval 读值 ──
void UI::cmdReadval(const std::string& arg) {
    if (!ensureAttached()) return;

    std::istringstream iss(arg);
    std::string addrStr, typeStr;
    iss >> addrStr >> typeStr;

    if (addrStr.empty()) {
        printf("\n  " R "✗" RST " 用法: " W "readval <地址> [类型]\n" RST);
        printf(D "    类型: int  uint  long  float  double  short  byte  bool\n" RST);
        printf(D "    默认: long (8 bytes)\n" RST "\n");
        return;
    }

    addr_t addr;
    try { addr = untag(std::stoull(addrStr, nullptr, 16)); } catch (...) {
        printf("\n  " R "✗" RST " 地址格式错误\n\n"); return;
    }

    if (typeStr.empty()) typeStr = "long";

    puts("");
    printf("  " D "地址" RST "  " C "0x%lx\n" RST, (unsigned long)addr);

    if (typeStr == "int" || typeStr == "i32") {
        auto v = mem_->read<int32_t>(addr);
        if (v) printf("  " D "int  " RST W " %d" RST D "  (0x%x)\n" RST, *v, *v);
        else printf("  " R "读取失败\n" RST);
    } else if (typeStr == "uint" || typeStr == "u32") {
        auto v = mem_->read<uint32_t>(addr);
        if (v) printf("  " D "uint " RST W " %u" RST D "  (0x%x)\n" RST, *v, *v);
        else printf("  " R "读取失败\n" RST);
    } else if (typeStr == "long" || typeStr == "i64") {
        auto v = mem_->read<int64_t>(addr);
        if (v) printf("  " D "long " RST W " %ld" RST D "  (0x%lx)\n" RST, *v, (unsigned long)*v);
        else printf("  " R "读取失败\n" RST);
    } else if (typeStr == "float" || typeStr == "f32") {
        auto v = mem_->read<float>(addr);
        if (v) printf("  " D "float" RST W " %f\n" RST, *v);
        else printf("  " R "读取失败\n" RST);
    } else if (typeStr == "double" || typeStr == "f64") {
        auto v = mem_->read<double>(addr);
        if (v) printf("  " D "dbl  " RST W " %lf\n" RST, *v);
        else printf("  " R "读取失败\n" RST);
    } else if (typeStr == "short" || typeStr == "i16") {
        auto v = mem_->read<int16_t>(addr);
        if (v) printf("  " D "short" RST W " %d" RST D "  (0x%x)\n" RST, *v, (unsigned short)*v);
        else printf("  " R "读取失败\n" RST);
    } else if (typeStr == "byte" || typeStr == "u8") {
        auto v = mem_->read<uint8_t>(addr);
        if (v) printf("  " D "byte " RST W " %u" RST D "  (0x%02x)\n" RST, *v, *v);
        else printf("  " R "读取失败\n" RST);
    } else if (typeStr == "bool") {
        auto v = mem_->read<uint8_t>(addr);
        if (v) printf("  " D "bool " RST W " %s" RST D "  (%u)\n" RST, *v ? "true" : "false", *v);
        else printf("  " R "读取失败\n" RST);
    } else if (typeStr == "str" || typeStr == "string") {
        auto v = mem_->readString(addr, 512);
        if (v) printf("  " D "str  " RST W " \"%s\"\n" RST, v->c_str());
        else printf("  " R "读取失败\n" RST);
    } else if (typeStr == "ptr") {
        auto v = mem_->read<addr_t>(addr);
        if (v) printf("  " D "ptr  " RST C " 0x%lx\n" RST, (unsigned long)*v);
        else printf("  " R "读取失败\n" RST);
    } else {
        printf("  " R "✗" RST " 未知类型: %s\n", typeStr.c_str());
        printf(D "    可用: int uint long float double short byte bool str ptr\n" RST);
    }
    puts("");
}

// ── search 搜值 ──
void UI::cmdSearch(const std::string& arg) {
    if (!ensureAttached()) return;

    std::istringstream iss(arg);
    std::string typeStr, valueStr;
    iss >> typeStr >> valueStr;

    if (typeStr.empty() || valueStr.empty()) {
        printf("\n  " R "✗" RST " 用法: " W "search <类型> <值>\n" RST);
        printf(D "    search int 99999\n" RST);
        printf(D "    search float 100.0\n" RST);
        printf(D "    search long 0x12345678\n" RST);
        puts("");
        return;
    }

    maps_->refresh();
    const auto& regions = maps_->regions();

    // 准备搜索字节
    std::vector<uint8_t> needle;
    std::string typeLabel;

    try {
        if (typeStr == "int" || typeStr == "i32") {
            int32_t v = static_cast<int32_t>(std::stol(valueStr, nullptr, 0));
            needle.resize(4);
            std::memcpy(needle.data(), &v, 4);
            typeLabel = "int32";
        } else if (typeStr == "long" || typeStr == "i64") {
            int64_t v = std::stoll(valueStr, nullptr, 0);
            needle.resize(8);
            std::memcpy(needle.data(), &v, 8);
            typeLabel = "int64";
        } else if (typeStr == "float" || typeStr == "f32") {
            float v = std::stof(valueStr);
            needle.resize(4);
            std::memcpy(needle.data(), &v, 4);
            typeLabel = "float";
        } else if (typeStr == "double" || typeStr == "f64") {
            double v = std::stod(valueStr);
            needle.resize(8);
            std::memcpy(needle.data(), &v, 8);
            typeLabel = "double";
        } else if (typeStr == "short" || typeStr == "i16") {
            int16_t v = static_cast<int16_t>(std::stoi(valueStr, nullptr, 0));
            needle.resize(2);
            std::memcpy(needle.data(), &v, 2);
            typeLabel = "int16";
        } else {
            printf("\n  " R "✗" RST " 不支持的类型: %s\n", typeStr.c_str());
            printf(D "    可用: int long float double short\n" RST "\n");
            return;
        }
    } catch (...) {
        printf("\n  " R "✗" RST " 值格式错误\n\n");
        return;
    }

    printf(D "\n  搜索 %s = %s (%zu bytes)...\n" RST,
           typeLabel.c_str(), valueStr.c_str(), needle.size());
    fflush(stdout);

    lastOutput_.clear();
    record("搜索: %s = %s\n\n", typeLabel.c_str(), valueStr.c_str());

    constexpr size_t BLOCK = 4096 * 16;
    std::vector<uint8_t> buf(BLOCK);
    std::vector<addr_t> found;
    size_t maxResults = 200;

    for (const auto& r : regions) {
        if (!r.readable() || !r.writable()) continue;  // 搜可写区域 (堆/栈)
        if (r.size() > 512UL * 1024 * 1024) continue;
        if (r.path.find("[vdso]") != std::string::npos) continue;

        for (addr_t cur = r.start; cur < r.end && found.size() < maxResults; cur += BLOCK) {
            size_t sz = std::min(static_cast<size_t>(r.end - cur), BLOCK);
            if (!mem_->readRaw(cur, buf.data(), sz)) continue;

            size_t align = needle.size();
            for (size_t off = 0; off + needle.size() <= sz; off += align) {
                if (std::memcmp(buf.data() + off, needle.data(), needle.size()) == 0) {
                    found.push_back(cur + off);
                    if (found.size() >= maxResults) break;
                }
            }
        }
    }

    if (found.empty()) {
        printf("  " R "✗" RST " 未找到\n\n");
        return;
    }

    puts("");
    printf(BLD M "  搜索结果" RST D " (%zu)\n" RST, found.size());
    line(D, 50);

    for (const auto& addr : found) {
        printf("  " C "0x%016lx\n" RST, (unsigned long)addr);
        record("0x%016lx\n", (unsigned long)addr);
    }
    if (found.size() >= maxResults) {
        printf(D "  ... 结果过多, 仅显示前 %zu 个\n" RST, maxResults);
    }
    puts("");
    printf(D "  用 " W "dump" D " 导出, " W "readval <地址> %s" D " 验证\n" RST "\n", typeStr.c_str());
}

// ── status 状态总览 ──
void UI::cmdStatus() {
    puts("");
    printf(BLD M "  状态总览\n" RST);
    line(D, 40);

    if (proc_.isAttached()) {
        printf("  进程    " G "✓" RST " " W "%s" RST D " (PID:%d)\n" RST, proc_.name().c_str(), proc_.pid());
        printf("  存活    %s\n", proc_.isAlive() ? (G "是" RST) : (R "否" RST));
    } else {
        printf("  进程    " D "未附加\n" RST);
    }

    if (bp_) {
        printf("  ptrace  %s\n", bp_->isAttached() ? (G "已附加" RST) : (D "未附加" RST));
        printf("  断点    " W "%zu" RST " 个\n", bp_->list().size());
    }

    if (maps_) {
        printf("  maps    " W "%zu" RST " 个区域\n", maps_->regions().size());
        auto il = maps_->findModule("libil2cpp.so");
        if (il)
            printf("  il2cpp  " G "✓" RST " 0x%lx\n", (unsigned long)il->start);
        else
            printf("  il2cpp  " D "未找到\n" RST);
    }

    if (!lastOutput_.empty()) {
        printf("  缓存    " W "%zu" RST " bytes " D "(可 dump 导出)\n" RST, lastOutput_.size());
    }

    puts("");
}

// ===== 命令路由 =====

void UI::handleCommand(const std::string& line) {
    size_t start = line.find_first_not_of(" \t");
    if (start == std::string::npos) return;
    size_t end = line.find_last_not_of(" \t");
    std::string trimmed = line.substr(start, end - start + 1);
    if (trimmed.empty()) return;

    std::string cmd, arg;
    auto sp = trimmed.find(' ');
    if (sp != std::string::npos) {
        cmd = trimmed.substr(0, sp);
        auto argStart = trimmed.find_first_not_of(" \t", sp);
        if (argStart != std::string::npos) arg = trimmed.substr(argStart);
    } else {
        cmd = trimmed;
    }

    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    if      (cmd == "help"    || cmd == "h" || cmd == "?") printHelp();
    else if (cmd == "guide")                               printGuide();
    else if (cmd == "attach"  || cmd == "a")               cmdAttach(arg);
    else if (cmd == "detach")                              cmdDetach();
    else if (cmd == "ps")                                  cmdPs(arg);
    else if (cmd == "pause")                               cmdPause();
    else if (cmd == "resume")                              cmdResume();
    else if (cmd == "maps"    || cmd == "map")             cmdMaps(arg);
    else if (cmd == "read"    || cmd == "r")               cmdRead(arg);
    else if (cmd == "readval" || cmd == "rv")              cmdReadval(arg);
    else if (cmd == "write"   || cmd == "w")               cmdWrite(arg);
    else if (cmd == "search")                              cmdSearch(arg);
    else if (cmd == "bp")                                  cmdBp(arg);
    else if (cmd == "regs")                                cmdRegs();
    else if (cmd == "scan"    || cmd == "s")               cmdScan(arg);
    else if (cmd == "watch")                              cmdWatch(arg);
    else if (cmd == "disasm"  || cmd == "dis" || cmd == "d") cmdDisasm(arg);
    else if (cmd == "patch"   || cmd == "p")               cmdPatch(arg);
    else if (cmd == "dumpfn")                              cmdDumpFn(arg);
    else if (cmd == "call")                                cmdCall(arg);
    else if (cmd == "hook")                                cmdHook(arg);
    else if (cmd == "decompile" || cmd == "dec")           cmdDecompile(arg);
    else if (cmd == "il2cpp"  || cmd == "il2")             cmdIl2cpp(arg);
    else if (cmd == "dump")                                cmdDump(arg);
    else if (cmd == "status")                              cmdStatus();
    else if (cmd == "clear"   || cmd == "cls") {
        fflush(stdout);
        ::write(STDOUT_FILENO, "\033[3J\033[2J\033[H", 12);
    }
    else if (cmd == "exit"    || cmd == "quit" || cmd == "q") running_ = false;
    else {
        printf("\n  " R "✗" RST " 未知命令: " W "%s\n" RST, cmd.c_str());
        printf(D "    输入 " W "help" D " 查看命令列表\n" RST "\n");
    }
}

// ===== 主循环 =====

// 处理后台命中: poll 检查, 自动 continue, 打印日志
void UI::pollWatchHits() {
    if (!bp_ || !bp_->isAttached()) return;

    while (true) {
        auto hit = bp_->pollHit();
        if (!hit) break;

        auto regs = bp_->getRegs();
        addr_t pc = regs ? regs->pc : 0;

        // 打印命中信息
        uint32_t inst = 0;
        if (mem_) {
            auto v = mem_->read<uint32_t>(pc);
            if (v) inst = *v;
        }

        // 反汇编当前指令
        std::string asmStr;
        if (inst && mem_) {
            csh cs;
            if (cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &cs) == CS_ERR_OK) {
                uint8_t code[4];
                std::memcpy(code, &inst, 4);
                cs_insn* insn;
                if (cs_disasm(cs, code, 4, pc, 1, &insn) == 1) {
                    asmStr = std::string(insn[0].mnemonic) + " " + insn[0].op_str;
                    cs_free(insn, 1);
                }
                cs_close(&cs);
            }
        }

        // 清行 + 打印命中信息, 不干扰提示符
        printf("\033[2K\r  " Y "[hit]" RST " " C "0x%lx" RST, (unsigned long)pc);
        if (!asmStr.empty())
            printf("  " G "%s" RST, asmStr.c_str());
        printf(RST "\n");
        fflush(stdout);

        record("0x%lx  %s\n", (unsigned long)pc, asmStr.c_str());

        // 自动跳过并继续, target 几乎不暂停
        bp_->continueExec();
    }
}

static constexpr const char* OUTPUT_DIR = "/storage/emulated/0/TsEngine";

void UI::run() {
    std::filesystem::create_directories(OUTPUT_DIR);
    printBanner();

    char buf[1024];
    while (running_) {
        // 先处理后台命中 (非阻塞)
        pollWatchHits();

        // 打印提示符
        if (proc_.isAttached()) {
            printf(G "%s" RST D "(%d)" RST, proc_.name().c_str(), proc_.pid());
        } else {
            printf(B "ts" RST);
        }
        printf(BLD " > " RST);
        fflush(stdout);

        // 用 select 同时等 stdin 输入和子进程事件
        while (true) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(STDIN_FILENO, &fds);

            struct timeval tv = { 0, 50000 }; // 50ms
            int ret = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);

            if (ret > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
                // 有输入
                break;
            }

            // 超时: 检查后台命中
            pollWatchHits();
        }

        if (!fgets(buf, sizeof(buf), stdin)) break;

        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';

        handleCommand(buf);
    }

    printf(D "\n  bye.\n\n" RST);
}

} // namespace TsEngine
