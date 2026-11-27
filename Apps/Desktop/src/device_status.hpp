#pragma once

// Read-only snapshots of the console status used by the Desktop panel.
// Battery and RAM come from Linux sysfs/procfs. Storage is reported only
// when sdRoot is itself a mount point, so a PC preview never shows the host
// disk as though it were the TrimUI SD card.

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <dirent.h>
#include <fstream>
#include <string>
#include <utility>
#include <sys/statvfs.h>
#include <vector>

namespace desktop_status {

struct Snapshot {
    bool hasBattery = false;
    int batteryPercent = -1;
    std::string batteryState;

    bool hasMemory = false;
    std::uint64_t memoryTotalBytes = 0;
    std::uint64_t memoryAvailableBytes = 0;

    bool hasStorage = false;
    std::uint64_t storageTotalBytes = 0;
    std::uint64_t storageAvailableBytes = 0;
};

namespace detail {

inline std::string readText(const std::string& path) {
    std::ifstream input(path.c_str());
    if (!input) return {};
    std::string value;
    std::getline(input, value);
    while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' '))
        value.pop_back();
    return value;
}

inline bool isBattery(const std::string& directory, const std::string& name) {
    const std::string type = readText(directory + "/" + name + "/type");
    if (type == "Battery") return true;
    return name.find("battery") != std::string::npos || name.find("Battery") != std::string::npos;
}

inline void readBattery(Snapshot& out) {
    DIR* directory = opendir("/sys/class/power_supply");
    if (!directory) return;

    std::vector<std::string> names;
    while (dirent* entry = readdir(directory)) {
        if (entry->d_name[0] == '.') continue;
        names.emplace_back(entry->d_name);
    }
    closedir(directory);
    std::sort(names.begin(), names.end());

    for (const std::string& name : names) {
        if (!isBattery("/sys/class/power_supply", name)) continue;
        const std::string path = "/sys/class/power_supply/" + name;
        const std::string capacity = readText(path + "/capacity");
        if (capacity.empty()) continue;
        char* end = nullptr;
        const long value = std::strtol(capacity.c_str(), &end, 10);
        if (end == capacity.c_str()) continue;
        out.batteryPercent = static_cast<int>(std::clamp(value, 0L, 100L));
        out.batteryState = readText(path + "/status");
        out.hasBattery = true;
        return;
    }
}

inline void readMemory(Snapshot& out) {
    std::ifstream input("/proc/meminfo");
    if (!input) return;

    std::uint64_t totalKb = 0;
    std::uint64_t availableKb = 0;
    std::uint64_t freeKb = 0;
    std::string key;
    std::uint64_t value = 0;
    std::string unit;
    while (input >> key >> value >> unit) {
        if (key == "MemTotal:") totalKb = value;
        else if (key == "MemAvailable:") availableKb = value;
        else if (key == "MemFree:") freeKb = value;
    }
    if (!totalKb) return;
    if (!availableKb) availableKb = freeKb;
    if (availableKb > totalKb) availableKb = totalKb;

    out.memoryTotalBytes = totalKb * 1024ULL;
    out.memoryAvailableBytes = availableKb * 1024ULL;
    out.hasMemory = true;
}

inline std::string unescapeMountPath(std::string path) {
    const std::pair<const char*, char> escapes[] = {
        {"\\040", ' '}, {"\\011", '\t'}, {"\\012", '\n'}, {"\\134", '\\'}
    };
    for (const auto& escape : escapes) {
        std::size_t pos = 0;
        while ((pos = path.find(escape.first, pos)) != std::string::npos) {
            path.replace(pos, 4, 1, escape.second);
            ++pos;
        }
    }
    return path;
}

inline bool isMountPoint(const std::string& path) {
    std::ifstream mounts("/proc/mounts");
    if (!mounts) return false;
    std::string source, mount, filesystem;
    while (mounts >> source >> mount >> filesystem) {
        if (unescapeMountPath(mount) == path) return true;
        std::string remainder;
        std::getline(mounts, remainder);
    }
    return false;
}

inline void readStorage(Snapshot& out, const std::string& sdRoot) {
    if (sdRoot.empty() || !isMountPoint(sdRoot)) return;
    struct statvfs info {};
    if (statvfs(sdRoot.c_str(), &info) != 0) return;
    const std::uint64_t unit = info.f_frsize ? info.f_frsize : info.f_bsize;
    if (!unit || !info.f_blocks) return;
    out.storageTotalBytes = static_cast<std::uint64_t>(info.f_blocks) * unit;
    out.storageAvailableBytes = static_cast<std::uint64_t>(info.f_bavail) * unit;
    out.hasStorage = true;
}

} // namespace detail

class Reader {
public:
    // Call at any cadence; expensive filesystem reads happen at most once per
    // five seconds. Returns true when a new snapshot was collected.
    bool refresh(const std::string& sdRoot, std::uint64_t nowMs) {
        if (initialized_ && sdRoot == root_ && nowMs >= lastRefreshMs_ &&
            nowMs - lastRefreshMs_ < kRefreshIntervalMs) {
            return false;
        }

        root_ = sdRoot;
        lastRefreshMs_ = nowMs;
        initialized_ = true;
        snapshot_ = Snapshot{};
        detail::readBattery(snapshot_);
        detail::readMemory(snapshot_);
        detail::readStorage(snapshot_, root_);
        return true;
    }

    const Snapshot& snapshot() const { return snapshot_; }

private:
    static constexpr std::uint64_t kRefreshIntervalMs = 5000;
    Snapshot snapshot_{};
    std::string root_;
    std::uint64_t lastRefreshMs_ = 0;
    bool initialized_ = false;
};

} // namespace desktop_status
