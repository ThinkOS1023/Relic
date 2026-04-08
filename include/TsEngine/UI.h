#pragma once

#include "Process.h"
#include "Memory.h"
#include "Maps.h"
#include "Breakpoint.h"
#include "Il2cpp.h"
#include "Symbols.h"
#include "Remote.h"

#include <memory>
#include <string>
#include <vector>

namespace TsEngine {

class UI {
public:
    UI();
    void run();

private:
    void printBanner();
    void printHelp();
    void handleCommand(const std::string& line);

    // 命令
    void cmdAttach(const std::string& arg);
    void cmdDetach();
    void cmdPause();
    void cmdResume();
    void cmdMaps(const std::string& arg);
    void cmdRead(const std::string& arg);
    void cmdWrite(const std::string& arg);
    void cmdBp(const std::string& arg);
    void cmdRegs();
    void cmdIl2cpp(const std::string& arg);
    void cmdPs(const std::string& arg);
    void cmdScan(const std::string& arg);
    void cmdWatch(const std::string& arg);
    void cmdDisasm(const std::string& arg);
    void cmdPatch(const std::string& arg);
    void cmdDumpFn(const std::string& arg);
    void cmdDecompile(const std::string& arg);
    void cmdCall(const std::string& arg);
    void cmdHook(const std::string& arg);
    void cmdDump(const std::string& arg);
    void cmdReadval(const std::string& arg);
    void cmdSearch(const std::string& arg);
    void cmdStatus();

    void ensureAttached();
    void printColor(const char* color, const char* fmt, ...);
    void pollWatchHits();

    // 把字符串追加到 lastOutput_ 用于 dump 导出
    void record(const char* fmt, ...);

    Process proc_;
    std::unique_ptr<Memory> mem_;
    std::unique_ptr<Maps> maps_;
    std::unique_ptr<Breakpoint> bp_;
    std::unique_ptr<Il2cppInspector> il2cpp_;
    Symbols syms_;
    std::unique_ptr<Remote> remote_;
    bool running_ = true;

    // 上次输出缓存, dump 命令用
    std::string lastOutput_;
};

} // namespace TsEngine
