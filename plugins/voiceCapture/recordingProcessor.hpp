#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "userData.hpp"

#pragma once

class Manager;

class RecordingProcessor {
    Manager* const m_manager;
    std::vector<std::filesystem::path> m_recordingFolders;
    std::vector<std::filesystem::path> m_files;
    UserTickDataRequestMap m_userTickMap;
    std::unique_ptr<std::thread> m_processor;
    UserTickData waitForUserTickData(const mumble_userid_t userID,
                                     const std::string& username) const;
    static void deleteFile(const std::filesystem::path& file);
    void process();
public:
    RecordingProcessor(Manager* const manager, const std::vector<std::filesystem::path>& files,
                       UserTickDataRequestMap* const userTickMap);
    RecordingProcessor(const RecordingProcessor&) = delete;
    RecordingProcessor(RecordingProcessor&&) = default;
    ~RecordingProcessor() noexcept;
};
