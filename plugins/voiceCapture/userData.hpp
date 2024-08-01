#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>

#include "mumble/plugin/internal/PluginComponents_v_1_0_x.h"

#include "socket.hpp"
#include <unordered_map>

#pragma once

using ToggleRecordingCallback = std::function<void(const char*, void*, void*)>;
using TimePoint = std::chrono::time_point<std::chrono::steady_clock>;
inline TimePoint now() { return std::chrono::steady_clock::now(); }

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

struct RecordingFolderData {
    std::filesystem::path recordingFolder;
    enum class State {
        Recording,
        Processing,
        FinishedProcessingUnsuccessfully,
        FinishedProcessingSuccessfully
    } state = State::Recording;
};
using RecordingFolderDataVector = std::vector<RecordingFolderData>;
