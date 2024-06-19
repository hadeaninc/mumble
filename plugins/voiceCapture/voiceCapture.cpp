#include "mumble/plugin/internal/PluginComponents_v_1_0_x.h"
#include "mumble/plugin/MumblePlugin.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
#include <memory>
#include <mutex>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <thread>
#include <unistd.h>

class VoiceCapturePlugin;

void periodic(VoiceCapturePlugin* const);
void recordingStopped();

class VoiceCapturePlugin : public MumblePlugin {
public:
    using time_point = std::chrono::time_point<std::chrono::steady_clock>;
private:
    std::unique_ptr<std::thread> m_periodicProcessing;
    std::atomic_bool m_running = true;
    std::mutex m_periodicMutex;
    // Keeps track of the last time a user sent voice samples.
    std::unordered_map<mumble_userid_t, time_point> m_speakingUsers;
public:
    VoiceCapturePlugin()
        : MumblePlugin("Hadean Voice Capture", "Hadean",
                       "This plugin records all incoming voice packets and redirects them to Hadean services that transcribe and store them.") {}

    // periodic() API start

    bool isRunning() const {
        return m_running;
    }

    time_point now() const {
        return std::chrono::steady_clock::now();
    }

    void addOrUpdateSpeakingUser(const mumble_userid_t userID) {
        const std::lock_guard<std::mutex> lock(m_periodicMutex);
        m_speakingUsers[userID] = now();
    }

    static constexpr std::chrono::milliseconds USER_SPEAKING_TIMEOUT{100};
    bool removeNonSpeakingUsers() {
        const std::lock_guard<std::mutex> lock(m_periodicMutex);
        const auto currentTime = now();
        for (auto itr = m_speakingUsers.cbegin(); itr != m_speakingUsers.cend();) {
            if (currentTime - itr->second >= USER_SPEAKING_TIMEOUT) {
                // It's been `USER_SPEAKING_TIMEOUT` since a user sent a voice packet,
                // we can say with certainty they are not talking anymore if they're using PTT.
                itr = m_speakingUsers.erase(itr);
            } else {
                // The user is likely still speaking.
                ++itr;
            }
        }
        return !m_speakingUsers.empty();
    }

    static const std::filesystem::path WAV_FOLDER;
    void toggleRecording() const {
        m_api.toggleRecording(WAV_FOLDER.c_str(), reinterpret_cast<void*>(&recordingStopped));
    }

    // periodic() API end

    virtual mumble_error_t init() noexcept override {
        std::filesystem::remove_all(WAV_FOLDER);
        m_periodicProcessing = std::make_unique<std::thread>(::periodic, this);
        return MUMBLE_STATUS_OK;
    }

    virtual void shutdown() noexcept override {
        m_running = false;
        if (m_periodicProcessing->joinable()) {
            m_periodicProcessing->join();
        }
    }

    virtual void releaseResource(const void *pointer) noexcept override {
        std::terminate();
    }

    virtual bool onAudioSourceFetched(float *outputPCM, uint32_t sampleCount, uint16_t channelCount,
                                      uint32_t sampleRate, bool isSpeech, mumble_userid_t userID) noexcept override {
        if (isSpeech) {
            addOrUpdateSpeakingUser(userID);
        }
        // False indicates that we haven't modified the PCM data.
        return false;
    }
};

// This will mean that a "voice" folder is created within the directory that the
// Observer Client was run from, and WAV files will be written into this folder.
const std::filesystem::path VoiceCapturePlugin::WAV_FOLDER{std::filesystem::current_path() / "voice"};

MumblePlugin &MumblePlugin::getPlugin() noexcept {
    static VoiceCapturePlugin plugin;
    return plugin;
}

void periodic(VoiceCapturePlugin* const plugin) {
    std::cout << "[HADEAN] Voice capture plugin periodic function started\n";
    bool aUserWasSpeaking = false;
    while (plugin->isRunning()) {
        const auto nowAUserIsSpeaking = plugin->removeNonSpeakingUsers();
        if (!aUserWasSpeaking && nowAUserIsSpeaking) {
            // Commence recording.
            plugin->toggleRecording();
            std::cout << "[HADEAN] STARTED voice capture recording\n";
        } else if (aUserWasSpeaking && !nowAUserIsSpeaking) {
            // Finish recording and send off for transcription.
            plugin->toggleRecording();
            std::cout << "[HADEAN] STOPPED voice capture recording\n";
        }
        aUserWasSpeaking = nowAUserIsSpeaking;
        std::this_thread::sleep_for(VoiceCapturePlugin::USER_SPEAKING_TIMEOUT);
    }
    std::cout << "[HADEAN] Voice capture plugin periodic function ended\n";
}

void recordingStopped() {
    static std::mutex transcription_mutex;
    const std::lock_guard<std::mutex> lock(transcription_mutex);
    // Scan the files within the voice directory. All *.wav files should be sent to the chat service.
    std::vector<std::filesystem::path> wav_files;
    try {
        std::filesystem::directory_iterator itr{VoiceCapturePlugin::WAV_FOLDER};
        std::copy_if(std::filesystem::begin(itr), std::filesystem::end(itr), std::back_inserter(wav_files),
            [](const std::filesystem::directory_entry& entry) {
                return entry.is_regular_file() && entry.path().extension().string() == ".wav";
            }
        );
    } catch (const std::exception& e) {
        std::cout << "[HADEAN] Failed to scan " << VoiceCapturePlugin::WAV_FOLDER << " directory: " << e.what() << '\n';
        return;
    }
    // wav_files now contains a list of full paths to *.wav files.
    // Send them to the chat service.
    // "Adapted" from: https://stackoverflow.com/a/70552118.
    std::size_t successfulSends = 0;
    int socketDesc = -1;
    try {
        // Allocate socket.
        socketDesc = socket(AF_INET, SOCK_STREAM, 0);
        if (socketDesc < 0) {
            throw std::runtime_error("failed to create Hadean services socket!");
        };

        // Obtain Hadean services server address and attempt to connect to the right port.
        static constexpr auto HOST = "localhost";
        static constexpr uint16_t PORT = 8080;
        const auto server = gethostbyname(HOST);
        if (!server) {
            throw std::runtime_error("could not resolve hostname for Hadean services socket!");
        }
        struct sockaddr_in serverAddress;
        memset(&serverAddress, 0, sizeof(serverAddress));
        serverAddress.sin_family = AF_INET;
        serverAddress.sin_port = htons(PORT);
        memcpy(reinterpret_cast<void*>(&serverAddress.sin_addr.s_addr),
            reinterpret_cast<void*>(server->h_addr), server->h_length);
        if (connect(socketDesc, reinterpret_cast<struct sockaddr*>(&serverAddress), sizeof(serverAddress)) < 0) {
            throw std::runtime_error("could not connect to the Hadean services socket!");
        }

        // Send each *.wav file.
        for (const auto wav_file : wav_files) {
            try {
                // We'll need to manually chunk up each *.wav file, as sending it all at once
                // may be too large for the receiving webserver. Figure out how many chunks
                // there will be and divide up the *.wav file accordingly.
                const auto wavFileSize = std::filesystem::file_size(wav_file);
                // Our Express Node.js webserver is automatically configured with a limit of 100KB
                // since it uses the body-parser middleware.
                static constexpr decltype(wavFileSize) CHUNK_SIZE = 1024 * 100;
                const auto chunks = static_cast<decltype(wavFileSize)>(std::ceil(static_cast<double>(wavFileSize) /
                                                                                 static_cast<double>(CHUNK_SIZE)));
                std::vector<std::vector<char>> wav_chunks(chunks, std::vector<char>(CHUNK_SIZE, '\0'));
                {
                    std::ifstream wav_file_stream(wav_file);
                    for (auto& wav_chunk : wav_chunks) {
                        wav_file_stream.read(&wav_chunk.front(), wav_chunk.size());
                        // The last chunk will be < CHUNK_SIZE unless the file can be evenly divided up.
                        // Usually it won't, so we shrink the chunk down to the number of bytes actually read.
                        wav_chunk.resize(wav_file_stream.gcount());
                    }
                    // Note that the ifstream at this point will usually have its eof and fail bits set
                    // due to the final chunk being < CHUNK_SIZE.
                }

                // Setup request parameters.
                // 1. Username.
                // TODO.
                static constexpr auto USERNAME = "User";
                // 2. Tick ID and elapsed time.
                // TODO.
                // 3. Chunking parameters.
                static std::size_t file_counter = 0;
                const std::size_t file_id = ++file_counter;
                std::size_t chunk_id = 0;
                const auto chunk_count = wav_chunks.size();

                // Send each chunk.
                for (const auto& wav_chunk : wav_chunks) {
                    // Build the request header and body.
                    std::stringstream requestBuilder;
                    requestBuilder << "POST /chat/sendAudioChunk/from" <<
                        "/" << USERNAME <<
                        "/RADIO" << // Topic.
                        "/" << file_id <<
                        "/" << chunk_id++ <<
                        "/" << chunk_count <<
                        " HTTP/1.1\r\n" <<
                        "Content-Type: application/octet-stream\r\n" <<
                        "Content-Length: " << wav_chunk.size() << "\r\n" <<
                        // Host argument is required for the express.js webserver,
                        // it will reject the request with 400 otherwise.
                        // Pretty sure the value can be anything.
                        "Host: localhost\r\n" <<
                        "\r\n";
                    requestBuilder.write(&wav_chunk.front(), wav_chunk.size());

                    // Attempt to make request.
                    const auto request = requestBuilder.str();
                    if (send(socketDesc, request.c_str(), request.size(), 0) < 0) {
                        throw std::runtime_error("couldn't send request to Hadean services!");
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds{100});
                }

                // Assume we've sent the *.wav file successfully, and attempt to delete it.
                ++successfulSends;
                try {
                    std::filesystem::remove(wav_file);
                } catch (const std::exception& e) {
                    std::cout << "[HADEAN] Failed to remove file " << wav_file << ": " << e.what() << '\n';
                }
            } catch (const std::exception& e) {
                std::cout << "[HADEAN] Failed to send " << wav_file << " to chat service, "
                    "will not delete it until it can be sent: " << e.what() << '\n';
            }
        }
    } catch (const std::exception& e) {
        std::cout << "[HADEAN] Failed to send *.wav files to chat service, "
            "will not delete them until they can be sent: " << e.what() << '\n';
    }
    if (socketDesc >= 0) {
        close(socketDesc);
    }
    // Because we've deleted all successfully sent files, and because of the lock,
    // we can reliably send each WAV file one time and one time only.
    // If a file fails to send, it will be kept until it can be sent. This may result in
    // a user having multiple *.wav files, with the second file include (1) in the filename,
    // third includes (2), etc.
    std::cout << "[HADEAN] " << successfulSends << " voice capture recording" << (successfulSends == 1 ? "" : "s")
        << " sent for transcription\n";
}
