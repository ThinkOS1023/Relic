#pragma once

#include "Core.h"

namespace TsEngine {

class Process {
public:
    Process() = default;

    bool attach(pid_t pid);
    bool attach(const std::string& name);
    void detach();

    bool pause();
    bool resume();
    [[nodiscard]] bool isAlive() const;
    [[nodiscard]] bool isAttached() const { return pid_ > 0; }
    [[nodiscard]] pid_t pid() const { return pid_; }
    [[nodiscard]] const std::string& name() const { return name_; }

    static std::vector<ProcessInfo> list();
    static std::optional<pid_t> findPid(const std::string& name);

private:
    pid_t pid_ = -1;
    std::string name_;
};

} // namespace TsEngine
