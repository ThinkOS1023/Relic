#pragma once

#include "Core.h"

namespace TsEngine {

class Maps {
public:
    explicit Maps(pid_t pid);

    bool refresh();
    [[nodiscard]] const std::vector<MemRegion>& regions() const { return regions_; }
    [[nodiscard]] std::vector<MemRegion> findByName(const std::string& name) const;
    [[nodiscard]] std::optional<MemRegion> findModule(const std::string& name) const;

private:
    pid_t pid_;
    std::vector<MemRegion> regions_;
};

} // namespace TsEngine
