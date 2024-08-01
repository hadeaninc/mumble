#include "mumble/plugin/internal/PluginComponents_v_1_0_x.h"
#include "mumble/plugin/MumblePlugin.h"

#include "log.hpp"
#include "manager.hpp"

#include <filesystem>
#include <memory>
#include <queue>

class VoiceCapturePlugin : public MumblePlugin {
    std::unique_ptr<Manager> m_manager;
    std::queue<mumble_userid_t> m_newUserQueue;
    bool m_unsynchronised = true;

    void addUser(const mumble_connection_t connection, const mumble_userid_t userID) {
        // userID ensures that we always have a unique fallback option in case the API calls fail.
        std::string username("User" + std::to_string(userID));
        try {
            username = static_cast<std::string>(m_api.getUserName(connection, userID));
        } catch (const MumbleAPIException& e) {
            if (m_unsynchronised && e.errorCode() == MUMBLE_EC_CONNECTION_UNSYNCHRONIZED) {
                // When the client first joins the server, this gets called for each user even before the
                // connection has finished synchronising. For this case, we need to queue the request so
                // that onServerSynchronized() can perform the addUser() call instead of onUserAdded().
                m_newUserQueue.push(userID);
                return;
            }
            LOG("Failed to retrieve username of user " << userID << ", will default to \"" <<
                username << "\": " << e.what());
        } catch (const std::exception& e) {
            LOG("Failed to retrieve username of user " << userID << ", will default to \"" <<
                username << "\": " << e.what());
        }
        m_manager->setUserName(userID, username);
    }
public:
    VoiceCapturePlugin()
        : MumblePlugin("Hadean Voice Capture", "Hadean",
                       "This plugin records all incoming voice packets and redirects them to Hadean services that transcribe and store them.") {}

    // When the plugin is initialised, start the manager thread.
    virtual mumble_error_t init() noexcept override {
        m_manager = std::make_unique<Manager>([this](const char* folder, void* manager, void* recordingStoppedCallback) {
            this->m_api.toggleRecording(folder, recordingStoppedCallback, manager);
        });
        // TODO: in the case when the plugin has started once the client has already connected to the server,
        //       we will need to manually pull the user list and add each user to the manager.
        return MUMBLE_STATUS_OK;
    }

    // Terminate the plugin when the Mumble client releases it.
    virtual void releaseResource(const void *pointer) noexcept override {
        std::terminate();
    }

    // Whenever the client connects to a new server, update the chat topic with the username
    // chosen by the operator.
    virtual void onServerSynchronized(mumble_connection_t connection) noexcept override {
        LOG("Connected to server");
        m_unsynchronised = false;
        try {
            const auto username = m_api.getUserName(connection, m_api.getLocalUserID(connection));
            m_manager->setChatTopic(static_cast<std::string>(username));
        } catch (const std::exception& e) {
            const auto defaultTopic = m_manager->getChatTopic();
            LOG("Failed to retrieve channel observer client's username, will default "
                "the chat topic to \"" << defaultTopic << "\": " << e.what());
        }
        // If there are users in the queue, add them now that the connection has synchronised.
        while (!m_newUserQueue.empty()) {
            addUser(connection, m_newUserQueue.front());
            m_newUserQueue.pop();
        }
    }

    // Whenever the client disconnects from a server, we need to remember that the connection
    // is no longer synchronised for when onUserAdded() is next called.
    virtual void onServerDisconnected(mumble_connection_t connection) noexcept override {
        LOG("Disconnected from server");
        m_unsynchronised = true;
    }

    // Whenever a new user joins the channel, cache their username in the manager.
    virtual void onUserAdded(mumble_connection_t connection, mumble_userid_t userID) noexcept override {
        addUser(connection, userID);
    }

    // Whenever a user speaks, inform the manager.
    virtual bool onAudioSourceFetched(float *outputPCM, uint32_t sampleCount, uint16_t channelCount,
                                      uint32_t sampleRate, bool isSpeech, mumble_userid_t userID) noexcept override {
        if (isSpeech) { m_manager->userHasJustSpoken(userID); }
        return false; // False indicates that we haven't modified the PCM data.
    }
};

MumblePlugin &MumblePlugin::getPlugin() noexcept {
    static VoiceCapturePlugin plugin;
    return plugin;
}
