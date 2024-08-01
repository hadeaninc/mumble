#include "manager.hpp"
#include "log.hpp"

#include "userData.hpp"
#include <cassert>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <thread>

Manager::Manager(const ToggleRecordingCallback& recordingCallback) :
    m_manager(&Manager::periodic, this),
    m_toggleRecording(recordingCallback) {
    // Try to initialise host and port now from JSON config file.
    const auto defaultHost = getHost();
    const auto defaultPort = getPort();
    try {
        nlohmann::json json;
        {
            std::ifstream jsonFile(JSON_CONFIG);
            json = nlohmann::json::parse(jsonFile);
        }
        try {
            setHost(json.at("host"));
        } catch (const std::exception& e) {
            LOG("Setting default host " << defaultHost << ": " << e.what());
        }
        try {
            setPort(json.at("port"));
        } catch (const std::exception& e) {
            LOG("Setting default port " << defaultPort << ": " << e.what());
        }
    } catch (const std::exception& e) {
        LOG("Couldn't load host or port from \"" << JSON_CONFIG << "\", leaving to defaults " <<
            defaultHost << " and " << defaultPort << ": " << e.what());
    }
}

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
        LOG("User with ID " << user.first << " (username " << user.second.username << ") has now STOPPED SPEAKING");
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
        // We only want to generate a new folder name every other time,
        // since this is a recording toggle. When a recording stops,
        // the API doesn't use the folder name parameter anyway.
        const std::string newFolder = (m_generateNewRecordingFolderName ? generateNewRecordingFolderName() : "");
        m_generateNewRecordingFolderName = !m_generateNewRecordingFolderName;
        m_toggleRecording(
            newFolder.c_str(),
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
        m_recordingProcessors.emplace_back(this, audioFiles, &m_userTickMap);
        // NOTE: m_userTickMap is now EMPTY as its contents has been moved into the recording processor thread.
        assert(m_userTickMap.empty());
    }
}

std::vector<std::filesystem::path> Manager::scanForAudioFiles() {
    std::vector<std::filesystem::path> files;
    const auto foldersToScan = getRecordingFoldersToScan();
    for (const auto& folder : foldersToScan) {
        LOG("Scanning directory " << folder);
        try {
            std::filesystem::directory_iterator itr{folder};
            std::copy_if(std::filesystem::begin(itr),
                         std::filesystem::end(itr),
                         std::back_inserter(files),
                         [](const std::filesystem::directory_entry& entry) {
                             return entry.is_regular_file() && entry.path().extension().string() == ".wav";
                         }
            );
        } catch (const std::exception& e) {
            LOG("Failed to fully scan " << folder << " directory: " << e.what());
        }
    }
    return files;
}

void Manager::setHost(const std::string& newHost) {
    LOCK(m_hostMutex);
    m_host = newHost;
    LOG("Setting host to " << newHost);
}

void Manager::setPort(const std::uint16_t newPort) {
    LOCK(m_portMutex);
    m_port = newPort;
    LOG("Setting port to " << newPort);
}

std::string Manager::getHost() const {
    LOCK(m_hostMutex);
    return m_host;
}

std::uint16_t Manager::getPort() const {
    LOCK(m_portMutex);
    return m_port;
}

std::string Manager::sanitiseString(const std::string& str) {
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
    m_topic = sanitiseString(topic);
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
        LOG("User with ID " << userID << " (username " << m_userMap[userID].username << ") is now SPEAKING");
        m_userTickMap[userID].username = m_userMap[userID].username;
        m_userTickMap[userID].tickDataRequest = std::make_unique<HTTPRequestThread>(HTTPRequestThread::Data{
            .request = {
                .host = getHost(),
                .port = getPort(),
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
        if (user.second.isSpeaking) {
            return true;
        }
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

std::filesystem::path Manager::generateNewRecordingFolderName() {
    const auto newFolder = std::filesystem::current_path() / ("voice" +
        std::to_string(m_recordingFolderCounter++));
    // If the folder already exists, delete it. This ensures that this run doesn't include
    // stray audio files from a previous run.
    try {
        if (std::filesystem::exists(newFolder)) {
            try {
                std::filesystem::remove_all(newFolder);
            } catch (const std::exception& e) {
                LOG("WARNING: could not delete existing file/folder " << newFolder <<
                    ", old audio recordings may be sent as part of this transcription! " <<
                    e.what());
            }
        }
    } catch (const std::exception& e) {
        LOG("WARNING: could not discover if file/folder " << newFolder << "currently exists: "
            "old audio recordings may be sent as part of this transcription, or recording may fail! " <<
            e.what());
    }
    LOCK(m_recordingFolderMutex);
    m_recordingFolders.push_back(RecordingFolderData{
        .recordingFolder = newFolder,
        .state = RecordingFolderData::State::Recording
    });
    return newFolder;
}

std::vector<std::filesystem::path> Manager::getRecordingFoldersToScan() {
    LOCK(m_recordingFolderMutex);
    std::vector<std::filesystem::path> recordingFoldersToScan;
    for (auto itr = m_recordingFolders.begin(); itr != m_recordingFolders.end();) {
        if (itr->state == RecordingFolderData::State::Processing) {
            // Do not scan folders that are currently processing: we don't want to process them twice!
            // We do include FinishedProcessingUnsuccessfully items, however, as they will include
            // recordings that failed to be sent the first time.
            ++itr;
            continue;
        } else if (itr->state == RecordingFolderData::State::FinishedProcessingSuccessfully) {
            itr = m_recordingFolders.erase(itr);
            continue;
        } else {
            // We're going to process the files [again], so mark the folder as Processing.
            itr->state = RecordingFolderData::State::Processing;
        }
        recordingFoldersToScan.push_back(itr->recordingFolder);
        ++itr;
    }
    return recordingFoldersToScan;
}

void Manager::recordingProcessingHasFinished(const std::vector<std::filesystem::path>& folders) {
    LOCK(m_recordingFolderMutex);
    for (const auto& folder : folders) {
        for (auto& recordingFolder : m_recordingFolders) {
            if (recordingFolder.recordingFolder != folder) { continue; }
            // A recording processor has [attempted to] push at least one recording file from this folder.
            // Find out if it was successful by counting the number of files within the folder.
            // If it's zero, delete the entry from the deque.
            // If it's non-zero, leave the deque entry so that future scans will include this folder.
            recordingFolder.state = RecordingFolderData::State::FinishedProcessingUnsuccessfully;
            try {
                const auto fileCount = std::distance(
                    std::filesystem::directory_iterator{folder},
                    std::filesystem::directory_iterator{}
                );
                if (fileCount == 0) {
                    try {
                        std::filesystem::remove_all(folder);
                        recordingFolder.state = RecordingFolderData::State::FinishedProcessingSuccessfully;
                    } catch (const std::exception& e) {
                        LOG("WARNING: could not delete recording folder " << folder << ", "
                            "its audio files will be sent for transcription again when the "
                            "next recording finishes!");
                    }
                } else {
                    LOG("WARNING: failed to send some recordings from folder " << folder << ", "
                        "will not delete the folder");
                }
            } catch (const std::exception& e) {
                LOG("WARNING: could not count number of files within folder " << folder << ", "
                    "will attempt to resend any files within this folder once the next "
                    "recording has finished");
            }
        }
    }
}
