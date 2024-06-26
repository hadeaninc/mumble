#include "recordingProcessor.hpp"
#include "log.hpp"
#include "manager.hpp"
#include "socket.hpp"

#include "userData.hpp"
#include <cassert>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>

RecordingProcessor::RecordingProcessor(Manager* const manager,
                                       const std::vector<std::filesystem::path>& files,
                                       UserTickDataRequestMap* const userTickMap,
                                       const std::size_t fileCounter) :
    m_manager(manager),
    m_files(files),
    m_fileCounter(fileCounter) {
    assert(m_manager);
    m_userTickMap.merge(*userTickMap);
    assert(!m_userTickMap.empty());
    m_processor = std::make_unique<std::thread>(&RecordingProcessor::process, this);
}

RecordingProcessor::~RecordingProcessor() noexcept {
    if (!m_processor || !m_processor->joinable()) { return; }
    m_processor->join();
}

UserTickData RecordingProcessor::waitForUserTickData(const mumble_userid_t userID,
                                                     const std::string& username) const {
    if (m_userTickMap.at(userID).tickDataRequest) {
        std::string tickDataJSON;
        try {
            tickDataJSON = m_userTickMap.at(userID).tickDataRequest->waitForResponse();
            nlohmann::json json = nlohmann::json::parse(tickDataJSON);
            return UserTickData{
                .tickID = json.at("tickId"),
                .elapsed = json.at("elapsed")
            };
        } catch (const std::exception& e) {
            LOG("ERROR: could not extract tick data for user \"" << username << "\" (ID " <<
                userID << "). HTTP request response: " << tickDataJSON << " — " << e.what());
        }
    }
    return {};
}

void RecordingProcessor::deleteFile(const std::filesystem::path& file) {
    try {
        std::filesystem::remove(file);
    } catch (const std::exception& e) {
        LOG("Failed to remove file " << file << ": " << e.what());
    }
}

void RecordingProcessor::process() {
    LOG("Recording stopped, will attempt to push " << m_files.size() << " file" <<
        (m_files.size() == 1 ? "" : "s") << " to Hadean services");
    std::size_t successfulSends = 0;
    for (const auto& file : m_files) {
        // 1. Extract user ID from file name.
        mumble_userid_t userID = 0;
        std::string username;
        try {
            const auto stem = static_cast<std::string>(file.stem());
            const auto idStr = stem.substr(0, stem.find_first_not_of("0123456789"));
            userID = std::stoi(idStr);
            username = m_userTickMap.at(userID).username;
        } catch (const std::exception& e) {
            // If a user ID can't be extracted from the file, or it wasn't a valid user ID,
            // try to delete the file straight away, i.e. ignore it.
            LOG("Failed to extract user ID from file \"" << file << "\" (file will be deleted): " <<
                e.what());
            deleteFile(file);
            continue;
        }

        // 2. If this file has cached tick data associated with it, use that instead of
        //    the incoming tick data, as that will be for the newest file and not for a
        //    file that previously failed to send to the webserver.
        //    Otherwise, wait for the user's new tick data to arrive.
        std::optional<UserTickData> tickData;
        try {
            tickData = m_manager->getCachedUserTickData(file);
            if (!tickData) {
                tickData = waitForUserTickData(userID, username);
            }
            if (!tickData->tickID || !tickData->elapsed) {
                LOG("WARNING: user \"" << username << "\" (ID " << userID << ") " <<
                    "hasn't got full tick data associated with the audio file! tickID=" <<
                    tickData->tickID << ", elapsed=" << tickData->elapsed);
            }

            // 3. Chunk up the audio file.
            const auto audioFileSize = std::filesystem::file_size(file);
            // Our Express Node.js webserver is automatically configured with a limit of 100KB
            // since it uses the body-parser middleware.
            static constexpr decltype(audioFileSize) CHUNK_SIZE = 1024 * 100;
            const auto chunks = static_cast<decltype(audioFileSize)>(std::ceil(static_cast<double>(audioFileSize) /
                                                                               static_cast<double>(CHUNK_SIZE)));
            std::vector<std::shared_ptr<std::vector<char>>> audioChunks(chunks);
            {
                std::ifstream fileStream(file);
                for (auto& audioChunk : audioChunks) {
                    audioChunk = std::make_shared<std::vector<char>>(CHUNK_SIZE, '\0');
                    fileStream.read(&audioChunk->front(), audioChunk->size());
                    // The last chunk will be < CHUNK_SIZE unless the file can be evenly divided up.
                    // Usually it won't, so we shrink the chunk down to the number of bytes actually read.
                    audioChunk->resize(fileStream.gcount());
                }
                // Note that the ifstream at this point will usually have its eof and fail bits set
                // due to the final chunk being < CHUNK_SIZE.
            }

            // 4. Send each chunk.
            const auto host = m_manager->getHost();
            const auto port = m_manager->getPort();
            const auto topic = m_manager->getChatTopic();
            const auto fileID = m_fileCounter++;
            std::size_t chunkID = 0;
            const auto chunkCount = audioChunks.size();
            for (const auto& audioChunk : audioChunks) {
                std::stringstream urlBuilder;
                urlBuilder << "/chat/sendAudioChunk/from" <<
                    "/" << username <<
                    "/" << topic <<
                    "/" << fileID <<
                    "/wav" <<
                    "/" << chunkID++ <<
                    "/" << chunkCount;
                if (tickData->tickID && tickData->elapsed) {
                    urlBuilder << "?tickId=" << *tickData->tickID <<
                        "&elapsed=" << *tickData->elapsed;
                }
                HTTPRequest{HTTPRequest::Data{
                    .host = host,
                    .port = port,
                    .method = HTTPRequest::Data::Method::POST,
                    .url = urlBuilder.str(),
                    .contentType = HTTPRequest::Data::ContentType::RAW,
                    .body = audioChunk
                }}.send(); // Ignore response.
            }

            // 5. Assume successful send, now attempt to delete the audio file.
            m_manager->removeCachedUserTickData(file);
            ++successfulSends;
            deleteFile(file);
        } catch (const std::exception& e) {
            LOG("Failed to send file \"" << file << "\", will not delete it until it can be sent: " <<
                e.what());
            if (tickData) { m_manager->setCachedUserTickData(file, *tickData); }
        }
    }
    LOG("Successfully sent " << successfulSends << " file" << (successfulSends == 1 ? "" : "s") <<
        " for transcription");
}
