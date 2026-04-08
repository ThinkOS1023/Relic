#include "TsEngine/Decompiler.h"

#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <capstone/capstone.h>

namespace TsEngine {

// ── 辅助函数 ──

static std::vector<std::string> splitOps(const char* ops) {
    std::vector<std::string> r;
    std::string s = ops;
    int bracket = 0;
    size_t start = 0;
    for (size_t p = 0; p <= s.size(); p++) {
        if (p < s.size() && s[p] == '[') bracket++;
        if (p < s.size() && s[p] == ']') bracket--;
        if ((p == s.size() || (s[p] == ',' && bracket == 0))) {
            auto b = s.find_first_not_of(" ", start);
            auto e = s.find_last_not_of(" ", p > 0 ? p - 1 : 0);
            if (b != std::string::npos && b <= e)
                r.push_back(s.substr(b, e - b + 1));
            start = p + 1;
        }
    }
    return r;
}

static std::string regName(const std::string& r) {
    if (r.empty()) return r;
    if (r == "wzr" || r == "xzr") return "0";
    if (r == "sp") return "sp";
    if ((r[0] == 'w' || r[0] == 'x') && r.size() <= 3) {
        int n = std::atoi(r.c_str() + 1);
        if (n <= 7) return "arg" + std::to_string(n);
        return "v" + std::to_string(n);
    }
    return r;
}

static std::string memExpr(const std::string& mem) {
    auto b = mem.find('[');
    auto e = mem.find(']');
    if (b == std::string::npos) return mem;
    std::string inner = mem.substr(b + 1, e - b - 1);

    auto comma = inner.find(',');
    if (comma == std::string::npos) {
        return "*" + regName(inner);
    }
    std::string base = inner.substr(0, comma);
    std::string off = inner.substr(comma + 1);
    while (!base.empty() && base.back() == ' ') base.pop_back();
    while (!off.empty() && off.front() == ' ') off = off.substr(1);
    if (!off.empty() && off[0] == '#') off = off.substr(1);

    std::string bn = regName(base);
    if (off == "0" || off == "0x0") return "*" + bn;
    return "*(" + bn + " + " + off + ")";
}

// ── 条件码 → 运算符 ──

static std::string condToOp(const std::string& cond) {
    if (cond == "eq") return "==";
    if (cond == "ne") return "!=";
    if (cond == "le") return "<=";
    if (cond == "lt") return "<";
    if (cond == "ge") return ">=";
    if (cond == "gt") return ">";
    if (cond == "hi") return "> (u)";
    if (cond == "ls") return "<= (u)";
    if (cond == "hs" || cond == "cs") return ">= (u)";
    if (cond == "lo" || cond == "cc") return "< (u)";
    return cond;
}

// 从 goto 行提取目标地址
static addr_t parseGotoAddr(const std::string& code) {
    // "goto #0x1234;" 或 "if (...) goto #0x1234;"
    auto pos = code.find("goto ");
    if (pos == std::string::npos) return 0;
    pos += 5; // skip "goto "
    if (pos < code.size() && code[pos] == '*') return 0; // indirect
    std::string addrStr = code.substr(pos);
    if (!addrStr.empty() && addrStr.back() == ';') addrStr.pop_back();
    if (addrStr.empty()) return 0;
    if (addrStr[0] == '#') addrStr = addrStr.substr(1);
    try { return std::stoull(addrStr, nullptr, 16); } catch (...) { return 0; }
}

// 从 "subs" 行提取变量名和立即数: "VN = VN - IMM;  // sets flags"
struct SubsInfo { std::string var; std::string imm; };
static bool parseSubsLine(const std::string& code, SubsInfo& out) {
    auto sf = code.find("// sets flags");
    if (sf == std::string::npos) return false;

    // "if (X - Y)" 形式 (dst=wzr/xzr 时)
    if (code.find("if (") == 0) {
        auto dash = code.find(" - ");
        if (dash == std::string::npos) return false;
        out.var = code.substr(4, dash - 4);
        auto close = code.find(')', dash);
        if (close == std::string::npos) return false;
        out.imm = code.substr(dash + 3, close - dash - 3);
        return true;
    }

    // "VN = VN - IMM;  // sets flags" 形式
    auto eq = code.find(" = ");
    auto dash = code.find(" - ");
    if (eq == std::string::npos || dash == std::string::npos) return false;
    out.var = code.substr(0, eq);
    auto semi = code.find(';', dash);
    if (semi == std::string::npos) return false;
    out.imm = code.substr(dash + 3, semi - dash - 3);
    return true;
}

// 从 "if (X cmp Y)" 行提取操作数
struct CmpInfo { std::string lhs; std::string rhs; };
static bool parseCmpLine(const std::string& code, CmpInfo& out) {
    if (code.find("if (") != 0) return false;
    auto cmpPos = code.find(" cmp ");
    if (cmpPos == std::string::npos) return false;
    out.lhs = code.substr(4, cmpPos - 4);
    auto close = code.find(')', cmpPos);
    if (close == std::string::npos) return false;
    out.rhs = code.substr(cmpPos + 5, close - cmpPos - 5);
    return true;
}

// 从 "if (COND) goto ADDR;" 行提取条件码和目标
struct CondBrInfo { std::string cond; std::string target; };
static bool parseCondBranch(const std::string& code, CondBrInfo& out) {
    if (code.find("if (") != 0) return false;
    auto close = code.find(") goto ");
    if (close == std::string::npos) return false;
    out.cond = code.substr(4, close - 4);
    auto semi = code.find(';', close);
    if (semi == std::string::npos) return false;
    out.target = code.substr(close + 7, semi - close - 7);
    return true;
}

// ── Peephole 优化 ──

// 检查 code 是否是 "REG = #HEXADDR;  // addr" 模式, 提取 REG 和 ADDR
struct AdrpInfo { std::string reg; addr_t addr; };
static bool parseAdrpLine(const std::string& code, const std::string& comment, AdrpInfo& out) {
    if (comment.find("addr") == std::string::npos && code.find("// addr") == std::string::npos) return false;
    auto eq = code.find(" = ");
    if (eq == std::string::npos) return false;
    out.reg = code.substr(0, eq);
    std::string rhs = code.substr(eq + 3);
    if (!rhs.empty() && rhs.back() == ';') rhs.pop_back();
    // 去掉 "// addr" 后缀
    auto cm = rhs.find("  //");
    if (cm != std::string::npos) rhs = rhs.substr(0, cm);
    while (!rhs.empty() && rhs.back() == ' ') rhs.pop_back();
    if (rhs.empty() || rhs[0] != '#') return false;
    try { out.addr = std::stoull(rhs.substr(1), nullptr, 16); return true; } catch (...) { return false; }
}

// 检查 "REG += OFFSET;" 模式
struct AddInfo { std::string reg; addr_t offset; };
static bool parseAddAssign(const std::string& code, AddInfo& out) {
    auto pos = code.find(" += ");
    if (pos == std::string::npos) return false;
    out.reg = code.substr(0, pos);
    std::string rhs = code.substr(pos + 4);
    if (!rhs.empty() && rhs.back() == ';') rhs.pop_back();
    while (!rhs.empty() && rhs.back() == ' ') rhs.pop_back();
    try { out.offset = std::stoull(rhs, nullptr, 0); return true; } catch (...) { return false; }
}

// 检查函数调用行: "funcname();" 且 comment 含字符串
static bool isCallLine(const std::string& code) {
    return code.find("();") != std::string::npos && code.find("goto") == std::string::npos;
}

// 从 switch 候选行提取: "if (VAR == N) goto ADDR;"
struct SwitchCase { std::string var; std::string val; std::string target; };
static bool parseSwitchCase(const std::string& code, SwitchCase& out) {
    if (code.find("if (") != 0) return false;
    auto eqPos = code.find(" == ");
    if (eqPos == std::string::npos) return false;
    auto closeP = code.find(") goto ");
    if (closeP == std::string::npos) return false;
    out.var = code.substr(4, eqPos - 4);
    out.val = code.substr(eqPos + 4, closeP - eqPos - 4);
    auto semi = code.find(';', closeP);
    if (semi == std::string::npos) return false;
    out.target = code.substr(closeP + 7, semi - closeP - 7);
    return true;
}

static void optimizeLines(std::vector<DecompileLine>& lines) {
    // ── Pass 1: 基础合并 (在 lines 上就地修改, 标记删除) ──
    std::vector<bool> deleted(lines.size(), false);

    for (size_t i = 0; i < lines.size(); i++) {
        if (deleted[i]) continue;
        auto& cur = lines[i];
        bool hasNext = (i + 1 < lines.size() && !deleted[i + 1]);

        // ─ Pattern 1: 去除 fallthrough goto ─
        if (hasNext && cur.code.find("goto ") == 0 && cur.code.find("goto *") != 0) {
            addr_t target = parseGotoAddr(cur.code);
            if (target != 0 && target == lines[i + 1].address) {
                deleted[i] = true; continue;
            }
        }

        // ─ Pattern 2: subs + 条件分支 → if (var OP imm) goto ─
        if (hasNext) {
            SubsInfo si;
            CondBrInfo bi;
            if (parseSubsLine(cur.code, si) && parseCondBranch(lines[i + 1].code, bi)) {
                std::string op = condToOp(bi.cond);
                lines[i + 1].code = "if (" + si.var + " " + op + " " + si.imm + ") goto " + bi.target + ";";
                if (cur.isTarget) lines[i + 1].isTarget = true;
                deleted[i] = true; continue;
            }
        }

        // ─ Pattern 3: cmp + 条件分支 → if (X OP Y) goto ─
        if (hasNext) {
            CmpInfo ci;
            CondBrInfo bi;
            if (parseCmpLine(cur.code, ci) && parseCondBranch(lines[i + 1].code, bi)) {
                std::string op = condToOp(bi.cond);
                lines[i + 1].code = "if (" + ci.lhs + " " + op + " " + ci.rhs + ") goto " + bi.target + ";";
                if (cur.isTarget) lines[i + 1].isTarget = true;
                deleted[i] = true; continue;
            }
        }

        // ─ Pattern 4: 标注后向跳转为 loop ─
        if (cur.code.find("goto ") == 0 && cur.code.find("goto *") != 0) {
            addr_t target = parseGotoAddr(cur.code);
            if (target != 0 && target < cur.address) {
                cur.comment = "loop";
            }
        }

        // ─ Pattern 5: adrp+add 合并 ─
        // "reg = #ADDR;  // addr" + "reg += OFFSET;" → "reg = #(ADDR+OFFSET);  // addr"
        if (hasNext) {
            AdrpInfo ai;
            AddInfo di;
            if (parseAdrpLine(cur.code, cur.comment, ai) && parseAddAssign(lines[i + 1].code, di) && ai.reg == di.reg) {
                char buf[64]; snprintf(buf, sizeof(buf), "%s = #0x%lx;", ai.reg.c_str(), (unsigned long)(ai.addr + di.offset));
                lines[i + 1].code = buf;
                lines[i + 1].comment = "addr";
                if (cur.isTarget) lines[i + 1].isTarget = true;
                deleted[i] = true; continue;
            }
        }
    }

    // ── Pass 2: 函数调用内联字符串参数 ──
    // "arg0 = #ADDR;  // addr" + "func();  // "string"" → "func("string");"
    for (size_t i = 0; i < lines.size(); i++) {
        if (deleted[i]) continue;
        // 找 call 行 (有字符串注释)
        if (!isCallLine(lines[i].code)) continue;
        if (lines[i].comment.empty() || lines[i].comment[0] != '"') continue;

        // 往前找 arg0 设置行
        for (size_t j = i; j > 0; j--) {
            if (deleted[j - 1]) continue;
            auto& prev = lines[j - 1];
            // "arg0 = #ADDR;  // addr" 或 "arg0 = #ADDR;"
            if (prev.code.find("arg0 = #") == 0 &&
                (prev.comment.find("addr") != std::string::npos || prev.code.find("// addr") != std::string::npos)) {
                deleted[j - 1] = true;
                // 把字符串嵌入函数调用
                auto paren = lines[i].code.find("();");
                if (paren != std::string::npos) {
                    std::string fname = lines[i].code.substr(0, paren);
                    lines[i].code = fname + "(" + lines[i].comment + ");";
                    lines[i].comment.clear();
                }
                break;
            }
            // 跳过其他 arg 设置
            if (prev.code.find("arg") == 0 && prev.code.find(" = ") != std::string::npos) continue;
            break; // 不是参数设置, 停止回溯
        }
    }

    // ── Pass 3: switch 检测 ──
    // 连续 3+ 个 "if (SAME_VAR == N) goto ADDR;" → switch
    for (size_t i = 0; i < lines.size(); i++) {
        if (deleted[i]) continue;
        SwitchCase first;
        if (!parseSwitchCase(lines[i].code, first)) continue;

        // 往前看: 可能有 "v8 = *(sp + 0x14);" 等 (重新加载同一变量)
        // 统计连续匹配行
        size_t count = 1;
        for (size_t j = i + 1; j < lines.size() && !deleted[j]; j++) {
            SwitchCase sc;
            // 跳过中间的 "v8 = *(addr);" 重载行 (switch 编译产物)
            if (lines[j].code.find(first.var + " = ") == 0 && lines[j].code.find("// sets flags") == std::string::npos) {
                continue;
            }
            if (parseSwitchCase(lines[j].code, sc) && sc.var == first.var) {
                count++;
                continue;
            }
            break;
        }

        if (count >= 3) {
            // 替换第一行为 switch 头
            lines[i].code = "switch (" + first.var + ") {";
            lines[i].comment = "case " + first.val + ": goto " + first.target;

            // 后续行替换为 case
            size_t replaced = 1;
            for (size_t j = i + 1; j < lines.size() && !deleted[j] && replaced < count; j++) {
                // 删除重载行
                if (lines[j].code.find(first.var + " = ") == 0 && lines[j].code.find("// sets flags") == std::string::npos) {
                    deleted[j] = true;
                    continue;
                }
                SwitchCase sc;
                if (parseSwitchCase(lines[j].code, sc) && sc.var == first.var) {
                    lines[j].code = "  case " + sc.val + ": goto " + sc.target + ";";
                    lines[j].comment.clear();
                    replaced++;
                }
            }
        }
    }

    // ── Pass 4: if (ptr != 0) 改为 if (ptr) ──
    for (size_t i = 0; i < lines.size(); i++) {
        if (deleted[i]) continue;
        auto& code = lines[i].code;
        // "if (VAR == 0) goto" → "if (!VAR) goto"
        auto pos = code.find(" == 0) goto ");
        if (pos != std::string::npos && code.find("if (") == 0) {
            std::string var = code.substr(4, pos - 4);
            code = "if (!" + var + ") goto " + code.substr(pos + 12);
        }
        // "if (VAR != 0) goto" → "if (VAR) goto"  (less common but check)
        pos = code.find(" != 0) goto ");
        if (pos != std::string::npos && code.find("if (") == 0) {
            std::string var = code.substr(4, pos - 4);
            code = "if (" + var + ") goto " + code.substr(pos + 12);
        }
    }

    // ── 输出: 过滤 deleted ──
    std::vector<DecompileLine> out;
    out.reserve(lines.size());
    for (size_t i = 0; i < lines.size(); i++) {
        if (!deleted[i]) out.push_back(std::move(lines[i]));
    }
    lines = std::move(out);
}

// ── 反编译主逻辑 ──

DecompileResult decompile(
    const Memory& mem, const Symbols& syms,
    addr_t funcStart, addr_t funcEnd,
    addr_t targetAddr,
    const uint8_t* data, size_t dataSize)
{
    DecompileResult result;
    result.funcStart = funcStart;
    result.funcEnd = funcEnd;
    result.stackFrame = 0;

    csh cs;
    if (cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &cs) != CS_ERR_OK) return result;
    cs_option(cs, CS_OPT_DETAIL, CS_OPT_ON);

    cs_insn* insn;
    size_t n = cs_disasm(cs, data, dataSize, funcStart, 0, &insn);
    if (n == 0) { cs_close(&cs); return result; }

    // 分析参数寄存器使用 (只标记"先读后写"的寄存器为输入参数)
    bool usesArg[8] = {};
    bool defined[8] = {};  // 已被写入 (不再是输入参数)
    for (size_t i = 0; i < n; i++) {
        std::string mn = insn[i].mnemonic;
        std::string ops = insn[i].op_str;

        // 遇到函数调用: x0-x7 全部被破坏, 停止分析
        if (mn == "bl" || mn == "blr") break;

        // 检测栈帧大小
        if (mn == "sub" && ops.find("sp, sp") != std::string::npos) {
            auto h = ops.find("#0x");
            if (h != std::string::npos) {
                try { result.stackFrame = std::stoi(ops.substr(h + 1), nullptr, 16); } catch (...) {}
            }
        }

        auto parts = splitOps(insn[i].op_str);
        if (parts.empty()) continue;

        // 判断指令类型: str/stp 第一个操作数是源 (读), 其他指令第一个是目的 (写)
        bool firstIsSource = (mn == "str" || mn == "strb" || mn == "strh" ||
                              mn == "stp" || mn == "stur" || mn == "sturb" || mn == "sturh" ||
                              mn == "cmp" || mn == "tst" || mn == "fcmp" ||
                              mn == "cbz" || mn == "cbnz" || mn == "tbz" || mn == "tbnz");

        for (size_t p = 0; p < parts.size(); p++) {
            const auto& op = parts[p];
            if (op.size() > 3 || op.empty()) continue;
            if (op[0] != 'w' && op[0] != 'x') continue;
            int regNum = -1;
            try { regNum = std::stoi(op.substr(1)); } catch (...) { continue; }
            if (regNum < 0 || regNum > 7) continue;

            bool isRead = (p > 0) || firstIsSource;
            bool isWrite = (p == 0) && !firstIsSource;

            if (isRead && !defined[regNum]) {
                usesArg[regNum] = true;
            }
            if (isWrite) {
                defined[regNum] = true;
            }
        }
    }

    // 函数名
    result.funcName = syms.resolve(funcStart);
    if (result.funcName.empty()) {
        char buf[32]; snprintf(buf, sizeof(buf), "sub_%lx", (unsigned long)funcStart);
        result.funcName = buf;
    }

    // 签名
    result.signature = "int64_t " + result.funcName + "(";
    bool first = true;
    for (int a = 0; a < 8; a++) {
        if (!usesArg[a]) continue;
        if (!first) result.signature += ", ";
        result.signature += "int64_t arg" + std::to_string(a);
        first = false;
    }
    if (first) result.signature += "void";
    result.signature += ")";

    // 寄存器值跟踪
    std::unordered_map<std::string, addr_t> regTrack;

    // 翻译每条指令
    for (size_t i = 0; i < n; i++) {
        std::string mn = insn[i].mnemonic;
        std::string ops = insn[i].op_str;
        auto parts = splitOps(insn[i].op_str);
        bool isTarget = (insn[i].address == targetAddr);

        std::string line;
        std::string comment;

        // 寄存器跟踪: adrp/adr/add/mov
        if (mn == "adrp" && parts.size() == 2) {
            addr_t val = 0;
            try { val = untag(std::stoull(parts[1].substr(parts[1][0] == '#' ? 1 : 0), nullptr, 16)); } catch (...) {}
            if (val) regTrack[parts[0]] = val;
        }
        if (mn == "adr" && parts.size() == 2) {
            addr_t val = 0;
            try { val = untag(std::stoull(parts[1].substr(parts[1][0] == '#' ? 1 : 0), nullptr, 16)); } catch (...) {}
            if (val) regTrack[parts[0]] = val;
        }
        if (mn == "add" && parts.size() == 3 && parts[0] == parts[1]) {
            auto it = regTrack.find(parts[0]);
            if (it != regTrack.end()) {
                std::string imm = parts[2];
                if (!imm.empty() && imm[0] == '#') imm = imm.substr(1);
                try { it->second += std::stoull(imm, nullptr, 16); } catch (...) {}
            }
        }
        if ((mn == "mov" || mn == "movz") && parts.size() == 2) {
            std::string imm = parts[1];
            if (!imm.empty() && imm[0] == '#') {
                try { regTrack[parts[0]] = std::stoull(imm.substr(1), nullptr, 0); } catch (...) {}
            }
        }

        // bl 前检查 x0 字符串
        if (mn == "bl") {
            auto it = regTrack.find("x0");
            if (it != regTrack.end() && it->second > 0x1000) {
                auto s = mem.readString(it->second, 80);
                if (s && !s->empty() && s->size() > 1) {
                    std::string str = *s;
                    if (str.size() > 50) str = str.substr(0, 50) + "...";
                    for (auto& c : str) { if (c == '\n') c = ' '; if (c == '\r') c = ' '; }
                    comment = "\"" + str + "\"";
                }
            }
            regTrack.erase("x0");
        }
        if (mn == "bl" || mn == "blr") {
            for (int a = 0; a <= 18; a++) {
                char r[4]; snprintf(r, 4, "x%d", a);
                regTrack.erase(r);
            }
        }

        if (mn == "nop") continue;

        // 指令翻译
        if (mn == "stp" && ops.find("x29") != std::string::npos) { line = "// prologue"; }
        else if (mn == "ldp" && ops.find("x29") != std::string::npos) { line = "// epilogue"; }
        else if ((mn == "sub" || mn == "add") && parts.size() == 3 && parts[0] == "sp" && parts[1] == "sp") {
            line = "// stack " + mn + " " + parts[2];
        }
        else if (mn == "mov" && parts.size() == 2 && parts[0] == "x29") { line = "// frame setup"; }
        else if (mn == "ret") { line = "return arg0;"; }
        else if ((mn == "mov" || mn == "movz") && parts.size() == 2) {
            line = regName(parts[0]) + " = " + regName(parts[1]) + ";";
        }
        else if (mn == "movk" && parts.size() == 2) {
            line = regName(parts[0]) + " |= " + parts[1] + ";";
        }
        else if ((mn == "add" || mn == "sub") && parts.size() == 3) {
            std::string p2 = parts[2]; if (!p2.empty() && p2[0] == '#') p2 = p2.substr(1);
            std::string op = (mn == "sub") ? " - " : " + ";
            if (parts[0] == parts[1])
                line = regName(parts[0]) + (mn == "sub" ? " -= " : " += ") + p2 + ";";
            else
                line = regName(parts[0]) + " = " + regName(parts[1]) + op + p2 + ";";
        }
        else if (mn == "subs" && parts.size() == 3) {
            std::string p2 = parts[2]; if (!p2.empty() && p2[0] == '#') p2 = p2.substr(1);
            if (parts[0] == "wzr" || parts[0] == "xzr")
                line = "if (" + regName(parts[1]) + " - " + p2 + ")";
            else
                line = regName(parts[0]) + " = " + regName(parts[1]) + " - " + p2 + ";  // sets flags";
        }
        else if ((mn == "ldr" || mn == "ldrsw" || mn == "ldrb" || mn == "ldrh" ||
                  mn == "ldur" || mn == "ldursw" || mn == "ldurb" || mn == "ldurh") && parts.size() == 2) {
            std::string ty = (mn == "ldrb" || mn == "ldurb") ? "(uint8_t)" :
                             (mn == "ldrh" || mn == "ldurh") ? "(uint16_t)" : "";
            line = regName(parts[0]) + " = " + ty + memExpr(parts[1]) + ";";

            auto bracket = parts[1].find('[');
            auto comma = parts[1].find(',', bracket);
            if (bracket != std::string::npos) {
                auto closeBracket = parts[1].find(']');
                std::string baseReg = (comma != std::string::npos)
                    ? parts[1].substr(bracket + 1, comma - bracket - 1)
                    : parts[1].substr(bracket + 1, closeBracket - bracket - 1);
                while (!baseReg.empty() && baseReg.back() == ' ') baseReg.pop_back();
                while (!baseReg.empty() && baseReg.front() == ' ') baseReg = baseReg.substr(1);

                auto it = regTrack.find(baseReg);
                if (it != regTrack.end()) {
                    addr_t memAddr = it->second;
                    if (comma != std::string::npos) {
                        std::string offStr = parts[1].substr(comma + 1, closeBracket - comma - 1);
                        while (!offStr.empty() && offStr.front() == ' ') offStr = offStr.substr(1);
                        if (!offStr.empty() && offStr[0] == '#') offStr = offStr.substr(1);
                        try {
                            if (!offStr.empty() && offStr[0] == '-') memAddr -= std::stoull(offStr.substr(1), nullptr, 0);
                            else memAddr += std::stoull(offStr, nullptr, 0);
                        } catch (...) {}
                    }
                    auto symName = syms.resolve(memAddr);
                    if (!symName.empty()) {
                        comment = symName;
                    } else {
                        auto pv = mem.read<addr_t>(memAddr);
                        if (pv && *pv > 0x1000) {
                            auto sn = syms.resolveWithOffset(untag(*pv));
                            if (!sn.empty()) comment = "-> " + sn;
                        }
                    }
                }
            }
        }
        else if ((mn == "str" || mn == "strb" || mn == "strh" ||
                  mn == "stur" || mn == "sturb" || mn == "sturh") && parts.size() == 2) {
            line = memExpr(parts[1]) + " = " + regName(parts[0]) + ";";
        }
        else if (mn == "stp" && parts.size() == 3) {
            line = memExpr(parts[2]) + " = {" + regName(parts[0]) + ", " + regName(parts[1]) + "};";
        }
        else if (mn == "ldp" && parts.size() == 3) {
            line = "{" + regName(parts[0]) + ", " + regName(parts[1]) + "} = " + memExpr(parts[2]) + ";";
        }
        else if (mn == "cmp" && parts.size() == 2) {
            std::string p1 = parts[1]; if (!p1.empty() && p1[0] == '#') p1 = p1.substr(1);
            line = "if (" + regName(parts[0]) + " cmp " + p1 + ")";
        }
        else if (mn == "cbz" && parts.size() == 2)
            line = "if (" + regName(parts[0]) + " == 0) goto " + parts[1] + ";";
        else if (mn == "cbnz" && parts.size() == 2)
            line = "if (" + regName(parts[0]) + " != 0) goto " + parts[1] + ";";
        else if (mn == "tbz" && parts.size() == 3)
            line = "if (!(" + regName(parts[0]) + " & (1 << " + parts[1] + "))) goto " + parts[2] + ";";
        else if (mn == "tbnz" && parts.size() == 3)
            line = "if (" + regName(parts[0]) + " & (1 << " + parts[1] + ")) goto " + parts[2] + ";";
        else if (mn.size() >= 2 && mn[0] == 'b' && mn[1] == '.')
            line = "if (" + mn.substr(2) + ") goto " + ops + ";";
        else if (mn == "b") { line = "goto " + ops + ";"; }
        else if (mn == "bl") {
            addr_t callTarget = 0;
            std::string rawAddr = ops.substr(ops[0] == '#' ? 1 : 0);
            try { callTarget = untag(std::stoull(rawAddr, nullptr, 16)); } catch (...) {}
            std::string fname = syms.resolve(callTarget);
            if (fname.empty()) fname = syms.resolveWithOffset(callTarget);
            if (!fname.empty())
                line = fname + "();";
            else if (callTarget == funcStart)
                line = "sub_" + rawAddr + "();  // recursive";
            else
                line = "sub_" + rawAddr + "();";
        }
        else if (mn == "blr") { line = "(*" + regName(ops) + ")();  // indirect call"; }
        else if (mn == "br") { line = "goto *" + regName(ops) + ";"; }
        else if (mn == "adr" || mn == "adrp") {
            if (parts.size() == 2) line = regName(parts[0]) + " = " + parts[1] + ";  // addr";
            else line = mn + " " + ops + ";";
        }
        else if (mn == "madd" && parts.size() == 4)
            line = regName(parts[0]) + " = " + regName(parts[1]) + " * " + regName(parts[2]) + " + " + regName(parts[3]) + ";";
        else if (mn == "msub" && parts.size() == 4)
            line = regName(parts[0]) + " = " + regName(parts[3]) + " - " + regName(parts[1]) + " * " + regName(parts[2]) + ";";
        else if ((mn == "and" || mn == "orr" || mn == "eor") && parts.size() == 3) {
            std::string op = (mn == "and") ? " & " : (mn == "orr") ? " | " : " ^ ";
            line = regName(parts[0]) + " = " + regName(parts[1]) + op + regName(parts[2]) + ";";
        }
        else if (mn == "lsl" && parts.size() == 3)
            line = regName(parts[0]) + " = " + regName(parts[1]) + " << " + parts[2] + ";";
        else if ((mn == "lsr" || mn == "asr") && parts.size() == 3)
            line = regName(parts[0]) + " = " + regName(parts[1]) + " >> " + parts[2] + ";";
        else if (mn == "csel" && parts.size() == 4)
            line = regName(parts[0]) + " = " + parts[3] + " ? " + regName(parts[1]) + " : " + regName(parts[2]) + ";";
        else if (mn == "cset" && parts.size() == 2)
            line = regName(parts[0]) + " = " + parts[1] + " ? 1 : 0;";
        else if (mn == "fmov" && parts.size() == 2)
            line = regName(parts[0]) + " = (float)" + regName(parts[1]) + ";";
        else if (mn == "fcvt" && parts.size() == 2)
            line = regName(parts[0]) + " = (double)" + regName(parts[1]) + ";";
        else if (mn == "scvtf" && parts.size() == 2)
            line = regName(parts[0]) + " = (float)" + regName(parts[1]) + ";";
        else if (mn == "fcvtzs" && parts.size() == 2)
            line = regName(parts[0]) + " = (int)" + regName(parts[1]) + ";";
        else if (mn == "fadd" && parts.size() == 3)
            line = regName(parts[0]) + " = " + regName(parts[1]) + " + " + regName(parts[2]) + ";  // float";
        else if (mn == "fsub" && parts.size() == 3)
            line = regName(parts[0]) + " = " + regName(parts[1]) + " - " + regName(parts[2]) + ";  // float";
        else if (mn == "fmul" && parts.size() == 3)
            line = regName(parts[0]) + " = " + regName(parts[1]) + " * " + regName(parts[2]) + ";  // float";
        else if (mn == "fdiv" && parts.size() == 3)
            line = regName(parts[0]) + " = " + regName(parts[1]) + " / " + regName(parts[2]) + ";  // float";
        else if (mn == "fcmp" && parts.size() == 2)
            line = "if (" + regName(parts[0]) + " fcmp " + regName(parts[1]) + ")";
        else {
            line = "/* " + mn + " " + ops + " */";
        }

        result.lines.push_back({ line, comment, insn[i].address, isTarget });
    }

    cs_free(insn, n);
    cs_close(&cs);

    // ── Peephole 优化 ──
    optimizeLines(result.lines);

    return result;
}

} // namespace TsEngine
