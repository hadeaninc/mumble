#include "mumble/plugin/internal/PluginComponents_v_1_0_x.h"
#include "mumble/plugin/MumblePlugin.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <iostream>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <thread>
#include <unistd.h>

#include <fstream>
#include <ios>
#include <unordered_map>

class VoiceCapturePlugin;

void periodic(VoiceCapturePlugin* const);
void recordingStopped();

class VoiceCapturePlugin : public MumblePlugin {
public:
    using time_point = std::chrono::time_point<std::chrono::steady_clock>;
private:
    std::unique_ptr<std::thread> m_periodicProcessing;
    std::atomic_bool m_running = true;
    mutable std::mutex m_periodicMutex;
    // Keeps track of the last time a user sent voice samples.
    std::unordered_map<mumble_userid_t, time_point> m_speakingUsers;
public:
    VoiceCapturePlugin()
        : MumblePlugin("Voice Capture", "Hadean",
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

    void toggleRecording() const {
        m_api.toggleRecording((std::filesystem::current_path() / "voice").c_str(),
                              reinterpret_cast<void*>(&recordingStopped));
    }

    // periodic() API end

    virtual mumble_error_t init() noexcept override {
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
        // False indicates that we haven't modified the audio array.
        return false;

        // OLD.
        if (isSpeech) {
            // The PCM data given by the Mumble Client can't be used directly as transcribable audio.
            // We will apply the same post-processing to the PCM data that recording does, namely:
            // 1. We DO mix down stereo to mono if applicable.
            // 2. We DO NOT apply volume adjustments.
            // 3. We DO write samples of silence.
            // See in the Mumble codebase:
            //     AudioOutput::mix(), from `emit audioSourceFetched(...)` onwards.
            //     VoiceRecorder::addBuffer().
            //     VoiceRecorder::run().

            std::vector<float> newPCM;
            newPCM.resize(sampleCount);
            ::memset(&newPCM.front(), 0, sizeof(float) * sampleCount);

            // 1. Mix stereo down to mono if required.
            for (std::size_t i = 0; i < sampleCount; ++i) {
                if (channelCount == 1) {
                    // I wrote it in this way in order to make it easier to apply volume adjustments
                    // if I ended up really needing to. But in hindsight this could have been a simple
                    // memcpy().
                    newPCM[i] += outputPCM[i];
                } else {
                    newPCM[i] += outputPCM[i * 2] / 2.0f + outputPCM[i * 2 + 1] / 2.0f;
                }
            }

            // DEBUGGING: dump it to files instead.
            {
                std::ofstream sampleCountsFile("/home/dan/temp-mumble-plugin/voice/sampleCounts.txt", std::ios_base::app);
                sampleCountsFile << sampleCount << '\n';
            }
            {
                std::ofstream channelCountsFile("/home/dan/temp-mumble-plugin/voice/channelCounts.txt", std::ios_base::app);
                channelCountsFile << channelCount << '\n'; 
            }
            {
                std::ofstream sampleRatesFile("/home/dan/temp-mumble-plugin/voice/sampleRates.txt", std::ios_base::app);
                sampleRatesFile << sampleRate << '\n'; 
            }
            {
                std::ofstream samplesFile("/home/dan/temp-mumble-plugin/voice/samples.pcm", std::ios_base::app | std::ios_base::binary);
                samplesFile.write(reinterpret_cast<const char*>(&newPCM.front()), sampleCount);
            }
            return false;

            // "Adapted" from: https://stackoverflow.com/a/70552118.
            // int socketDesc = -1;
            // try {
            //     socketDesc = socket(AF_INET, SOCK_STREAM, 0);
            //     if (socketDesc < 0) {
            //         throw std::runtime_error("Failed to create voice API socket!");
            //     };
            //     const auto server = gethostbyname("localhost");
            //     if (!server) {
            //         throw std::runtime_error("Could not resolve hostname for voice API socket!");
            //     }
            //     struct sockaddr_in serverAddress;
            //     memset(&serverAddress, 0, sizeof(serverAddress));
            //     serverAddress.sin_family = AF_INET;
            //     serverAddress.sin_port = htons(60000);
            //     memcpy(reinterpret_cast<void*>(&serverAddress.sin_addr.s_addr),
            //         reinterpret_cast<void*>(server->h_addr), server->h_length);
            //     if (connect(socketDesc, reinterpret_cast<struct sockaddr*>(&serverAddress), sizeof(serverAddress)) < 0) {
            //         throw std::runtime_error("Could not connect to the voice API socket!");
            //     }
            //     // Mumble API calls will throw if they fail.
            //     const auto mumbleServer = m_api.getActiveServerConnection();
            //     const auto mumbleUserName = std::string(m_api.getUserName(mumbleServer, userID));
            //     const std::size_t pcmDataLength = static_cast<std::size_t>(sampleCount) *
            //                                       static_cast<std::size_t>(channelCount);
            //     std::stringstream requestBuilder;
            //     requestBuilder << "POST /voice?sampleCount=" << sampleCount <<
            //         "&channelCount=" << channelCount <<
            //         "&sampleRate=" << sampleRate <<
            //         "&userName=" << mumbleUserName <<
            //         " HTTP/1.1\r\n" <<
            //         "Content-Type: application/octet-stream\r\n" <<
            //         "Content-Length: " << pcmDataLength << "\r\n" <<
            //         "\r\n";
            //     requestBuilder.write(reinterpret_cast<const char*>(outputPCM), pcmDataLength);
            //     const auto request = requestBuilder.str();
            //     if (send(socketDesc, request.c_str(), request.size(), 0) < 0) {
            //         throw std::runtime_error("Failed to send voice data to voice API server!");
            //     }
            // } catch (const std::exception& e) {
            //     std::cout << e.what() << '\n';
            // }
            // if (socketDesc >= 0) {
            //     close(socketDesc);
            // }
        }
        // False indicates that we haven't modified the audio array.
        return false;
    }
};

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
    std::cout << "[HADEAN] Voice capture recording sent for transcription\n";
}
