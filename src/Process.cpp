#include "TsEngine/Process.h"

#include <dirent.h>
#include <signal.h>
#include <fstream>
#include <cstring>
#include <filesystem>

namespace TsEngine {

bool Process::attach(pid_t pid) {
    if (!std::filesystem::exists("/proc/" + std::to_string(pid))) {
        return false;
    }
    pid_ = pid;

    // 读取进程名
    std::ifstream f("/proc/" + std::to_string(pid) + "/cmdline");
    if (f.is_open()) {
        std::getline(f, name_, '\0');
    }
    return true;
}

bool Process::attach(const std::string& name) {
    auto pid = findPid(name);
    if (!pid) return false;
    return attach(*pid);
}

void Process::detach() {
    pid_ = -1;
    name_.clear();
}

bool Process::pause() {
    if (pid_ <= 0) return false;
    return kill(pid_, SIGSTOP) == 0;
}

bool Process::resume() {
    if (pid_ <= 0) return false;
    return kill(pid_, SIGCONT) == 0;
}

bool Process::isAlive() const {
    if (pid_ <= 0) return false;
    return kill(pid_, 0) == 0;
}

std::vector<ProcessInfo> Process::list() {
    std::vector<ProcessInfo> result;

    DIR* dir = opendir("/proc");
    if (!dir) return result;

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        // 只看数字目录
        char* end = nullptr;
        long pid = strtol(entry->d_name, &end, 10);
        if (*end != '\0' || pid <= 0) continue;

        ProcessInfo info;
        info.pid = static_cast<pid_t>(pid);

        // 读取 cmdline
        std::string path = "/proc/" + std::string(entry->d_name) + "/cmdline";
        std::ifstream f(path);
        if (f.is_open()) {
            std::getline(f, info.name, '\0');
        }

        if (info.name.empty()) {
            // 尝试 comm
            path = "/proc/" + std::string(entry->d_name) + "/comm";
            std::ifstream fc(path);
            if (fc.is_open()) {
                std::getline(fc, info.name);
            }
        }

        if (!info.name.empty()) {
            result.push_back(std::move(info));
        }
    }
    closedir(dir);
    return result;
}

std::optional<pid_t> Process::findPid(const std::string& name) {
    auto procs = list();

    // 先尝试精确匹配
    for (const auto& p : procs) {
        if (p.name == name) return p.pid;
    }

    // 再尝试子串匹配, 取第一个
    pid_t found = -1;
    int count = 0;
    for (const auto& p : procs) {
        if (p.name.find(name) != std::string::npos) {
            if (found < 0) found = p.pid;
            count++;
        }
    }

    if (count == 0) return std::nullopt;
    return found;
}

} // namespace TsEngine
