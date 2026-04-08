#include "TsEngine/Maps.h"

#include <fstream>
#include <sstream>
#include <algorithm>

namespace TsEngine {

Maps::Maps(pid_t pid) : pid_(pid) {}

bool Maps::refresh() {
    regions_.clear();

    std::string path = "/proc/" + std::to_string(pid_) + "/maps";
    std::ifstream f(path);
    if (!f.is_open()) return false;

    std::string line;
    while (std::getline(f, line)) {
        MemRegion region{};

        // 格式: start-end perms offset dev inode pathname
        char perms[5] = {};
        unsigned long long start, end;
        int n = 0;

        if (sscanf(line.c_str(), "%llx-%llx %4s %*x %*s %*d%n",
                   &start, &end, perms, &n) < 3) {
            continue;
        }

        region.start = static_cast<addr_t>(start);
        region.end   = static_cast<addr_t>(end);
        region.perms = perms;

        // 提取路径 (跳过空格)
        if (n > 0 && static_cast<size_t>(n) < line.size()) {
            auto pos = line.find_first_not_of(' ', n);
            if (pos != std::string::npos) {
                region.path = line.substr(pos);
            }
        }

        regions_.push_back(std::move(region));
    }

    return true; // 文件读取成功 (空区域也是合法的)
}

std::vector<MemRegion> Maps::findByName(const std::string& name) const {
    std::vector<MemRegion> result;
    for (const auto& r : regions_) {
        if (r.path.find(name) != std::string::npos) {
            result.push_back(r);
        }
    }
    return result;
}

std::optional<MemRegion> Maps::findModule(const std::string& name) const {
    for (const auto& r : regions_) {
        if (r.path.find(name) != std::string::npos && r.executable()) {
            return r;
        }
    }
    return std::nullopt;
}

} // namespace TsEngine
