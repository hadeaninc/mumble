#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

#include "userData.hpp"
#include "recordingProcessor.hpp"
#include "socket.hpp"

#include "mumble/plugin/internal/PluginComponents_v_1_0_x.h"
#include <unordered_set>

#pragma once

#define JSON_CONFIG "./voiceCapture.json"
#define LOCK(m) const std::lock_guard<std::mutex> lock(m);
#define LOCK2(m, n) LOCK(m); LOCK(n)

class Manager {
    std::thread m_manager;
    std::atomic_bool m_exitSignal = false;
    std::atomic_bool m_recordingStoppedSignal = false;
    ToggleRecordingCallback m_toggleRecording;
    std::vector<RecordingProcessor> m_recordingProcessors;
    void periodic();
    static constexpr std::chrono::milliseconds USER_SPEAKING_TIMEOUT{100};
    void areUsersStillTalking();
    void toggleRecordingIfNecessary(bool& aUserWasSpeaking);
    void pushRecordingsIfAvailable();
    std::vector<std::filesystem::path> scanForAudioFiles();
    friend void recordingHasStopped(void* userParam);
public:
    Manager(const ToggleRecordingCallback& recordingCallback);
    ~Manager() noexcept;

private:
    mutable std::mutex m_hostMutex;
    std::string m_host = "localhost";
    mutable std::mutex m_portMutex;
    std::uint16_t m_port = 8080;
public:
    void setHost(const std::string& newHost);
    void setPort(const std::uint16_t newPort);
    std::string getHost() const;
    std::uint16_t getPort() const;

private:
    mutable std::mutex m_topicMutex;
    std::string m_topic = "RADIO";
    static constexpr std::size_t SANITIZED_STRING_LIMIT{100};
    // Ensures a string is safe for use within a URL.
    static std::string sanitiseString(const std::string& str);
public:
    void setChatTopic(const std::string& topic);
    std::string getChatTopic() const;

private:
    mutable std::mutex m_userMapMutex;
    // User IDs are unique per connection, e.g. a user can have more than one
    // ID if they disconnect from the server then rejoin, and they may or may
    // not have the same username, but they will have separate audio files
    // even in the same recording session.
    UserDataMap m_userMap;
    mutable std::mutex m_userTickMapMutex;
    UserTickDataRequestMap m_userTickMap;
public:
    void setUserName(const mumble_userid_t userID, const std::string& username);
    void userHasJustSpoken(const mumble_userid_t userID);
    bool isAUserSpeaking() const;

// If a file fails to send, then the associated tick data must be cached so that
// when it's resent later, it will still be stored against the correct tick and time.
private:
    mutable std::mutex m_userTickCacheMutex;
    std::unordered_map<std::string, UserTickData> m_userTickCache;
public:
    void setCachedUserTickData(const std::string& filepath, const UserTickData& data);
    std::optional<UserTickData> getCachedUserTickData(const std::string& filepath) const;
    void removeCachedUserTickData(const std::string& filepath);

private:
    mutable std::mutex m_recordingFolderMutex;
    // This deque will be ordered by date generated. This means that the last item in
    // the deque *should* be the most recent recording.
    RecordingFolderDataVector m_recordingFolders;
    // No need for these fields to be protected as they're only touched in one code path.
    std::size_t m_recordingFolderCounter = 0;
    bool m_generateNewRecordingFolderName = true;
    std::filesystem::path generateNewRecordingFolderName();
    std::vector<std::filesystem::path> getRecordingFoldersToScan();
public:
    void recordingProcessingHasFinished(const std::vector<std::filesystem::path>& folders);
};
