#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>

#include "mumble/plugin/internal/PluginComponents_v_1_0_x.h"

#include "socket.hpp"
#include <unordered_map>

#pragma once

using ToggleRecordingCallback = std::function<void(void*, void*)>;
using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;
inline TimePoint now() { return std::chrono::steady_clock::now(); }
// This will mean that a "voice" folder is created within the directory that the
// Observer Client was run from, and WAV files will be written into this folder.
static const std::filesystem::path WAV_FOLDER{
    std::filesystem::current_path() / "voice"
};

struct UserData {
    std::string username;
    TimePoint lastSpokeAt;
    bool isSpeaking = false;
};
using UserDataMap = std::unordered_map<mumble_userid_t, UserData>;

struct UserTickData {
    std::optional<std::int64_t> tickID;
    std::optional<std::int64_t> elapsed;
};
struct UserTickDataRequest {
    std::string username;
    std::unique_ptr<HTTPRequestThread> tickDataRequest;
};
using UserTickDataRequestMap = std::unordered_map<mumble_userid_t, UserTickDataRequest>;
