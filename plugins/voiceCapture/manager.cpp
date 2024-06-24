#include "manager.hpp"
#include "log.hpp"
#include "socket.hpp"

#include <cassert>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

Manager::Manager(const ToggleRecordingCallback& recordingCallback) :
    m_manager(&Manager::periodic, this),
    m_toggleRecording(recordingCallback) {}

Manager::~Manager() noexcept {
    m_exitSignal = true;
    if (!m_manager.joinable()) { return; }
    m_manager.join();
}

void Manager::periodic() {
    LOG("Voice capture plugin periodic function started");
    bool aUserWasSpeaking = false;
    while (!m_exitSignal) {
        areUsersStillTalking();
        toggleRecordingIfNecessary(aUserWasSpeaking);
        pushRecordingsIfAvailable();
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }
    LOG("Voice capture plugin periodic function ended");
}

void Manager::areUsersStillTalking() {
    LOCK(m_userMapMutex);
    const auto now = ::now();
    for (auto& user : m_userMap) {
        if (!user.second.isSpeaking) { continue; }
        if (now - user.second.lastSpokeAt < USER_SPEAKING_TIMEOUT) { continue; }
        user.second.isSpeaking = false;
    }
}

void recordingHasStopped(void* userParam) {
    reinterpret_cast<Manager*>(userParam)->m_recordingStoppedSignal = true;
}

void Manager::toggleRecordingIfNecessary(bool& aUserWasSpeaking) {
    const auto nowAUserIsSpeaking = isAUserSpeaking();
    if ((!aUserWasSpeaking && nowAUserIsSpeaking) ||
        (aUserWasSpeaking && !nowAUserIsSpeaking)) {
        m_toggleRecording(
            reinterpret_cast<void*>(this),
            reinterpret_cast<void*>(&recordingHasStopped)
        );
    }
    aUserWasSpeaking = nowAUserIsSpeaking;
}

void Manager::pushRecordingsIfAvailable() {
    if (m_recordingStoppedSignal) {
        m_recordingStoppedSignal = false;
        // Pushing the audio files to Hadean services will not block the manager.
        // The only operation that will block will be scanning for the audio files themselves.
        // This ensures that a recording doesn't start in the middle of the scan,
        // resulting in *.wav files that pertain to the new recording being sent as part of the old one.
        // We must also move ownership of the tick data request map to the recording processor,
        // so we block for that, too. We transfer ownership because we need to wait for the tick
        // data requests to complete before including tick data into the audio chunk request URLs,
        // and we mustn't block the manager to do so or else it might miss opportunities to record voice data.
        LOCK(m_userTickMapMutex);
        const auto audioFiles = scanForAudioFiles();
        m_recordingProcessors.emplace_back(this, audioFiles, &m_userTickMap,
            m_audioFileCounter += audioFiles.size());
        // NOTE: m_userTickMap is now EMPTY as its contents has been moved into the recording processor thread.
        assert(m_userTickMap.empty());
    }
}

std::vector<std::filesystem::path> Manager::scanForAudioFiles() const {
    std::vector<std::filesystem::path> files;
    try {
        std::filesystem::directory_iterator itr{WAV_FOLDER};
        std::copy_if(std::filesystem::begin(itr),
                     std::filesystem::end(itr),
                     std::back_inserter(files),
                     [](const std::filesystem::directory_entry& entry) {
                         return entry.is_regular_file() && entry.path().extension().string() == ".wav";
                     }
        );
    } catch (const std::exception& e) {
        LOG("Failed to fully scan " << WAV_FOLDER << " directory: " << e.what());
        return files;
    }
    return files;
}

std::string Manager::_sanitiseString(const std::string& str) {
    std::string finalString;
    finalString.reserve(SANITIZED_STRING_LIMIT);
    for (const auto& chr : str) {
        if (finalString.size() >= SANITIZED_STRING_LIMIT) { break; }
        if ((chr >= '0' && chr <= '9') ||
            (chr >= 'A' && chr <= 'Z') ||
            (chr >= 'a' && chr <= 'z')) { finalString += chr; }
    }
    return finalString;
}

void Manager::setChatTopic(const std::string& topic) {
    LOCK(m_topicMutex);
    const auto oldTopic = m_topic;
    m_topic = _sanitiseString(topic);
    LOG("Chat topic updated from \"" << oldTopic << "\" to \"" << m_topic << "\"");
}

std::string Manager::getChatTopic() const {
    LOCK(m_topicMutex);
    return m_topic;
}

void Manager::setUserName(const mumble_userid_t userID, const std::string& username) {
    LOCK(m_userMapMutex);
    m_userMap[userID].username = username;
    LOG("User with ID " << userID << "'s username has been set to \"" << username << "\"");
}

void Manager::userHasJustSpoken(const mumble_userid_t userID) {
    LOCK2(m_userMapMutex, m_userTickMapMutex);
    m_userMap[userID].lastSpokeAt = ::now();
    m_userMap[userID].isSpeaking = true;
    if (m_userTickMap.find(userID) == m_userTickMap.end()) {
        // TODO: For now, let's just request a user's tick data once.
        //       This will, however, be inaccurate if the user speaks more than once during a recording.
        //       If users adhere to correct radio protocol though, this lack of robustness should not
        //       be an issue, as recordings are likely to start and stop when a single user speaks a
        //       single sentence or paragraph.
        m_userTickMap[userID].username = m_userMap[userID].username;
        m_userTickMap[userID].tickDataRequest = std::make_unique<HTTPRequestThread>(HTTPRequestThread::Data{
            .request = {
                .host = HOST,
                .port = PORT,
                .method = HTTPRequest::Data::Method::GET,
                .url = "/replay/tick",
                .contentType = HTTPRequest::Data::ContentType::TEXT
            }
        });
        m_userTickMap[userID].tickDataRequest->send();
    }
}

bool Manager::isAUserSpeaking() const {
    LOCK(m_userMapMutex);
    for (const auto& user : m_userMap) {
        if (user.second.isSpeaking) { return true; }
    }
    return false;
}

void Manager::setCachedUserTickData(const std::string& filepath, const UserTickData& data) {
    LOCK(m_userTickCacheMutex);
    m_userTickCache[filepath] = data;
}

std::optional<UserTickData> Manager::getCachedUserTickData(const std::string& filepath) const {
    LOCK(m_userTickCacheMutex);
    const auto itr = m_userTickCache.find(filepath);
    if (itr == m_userTickCache.end()) { return std::nullopt; }
    return itr->second;
}

void Manager::removeCachedUserTickData(const std::string& filepath) {
    LOCK(m_userTickCacheMutex);
    m_userTickCache.erase(filepath);
}
