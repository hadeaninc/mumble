#include <cstdint>
#include <functional>
#include <stdexcept>
#include <thread>
#include <vector>

#pragma once

using request_exception = std::runtime_error;

class HTTPRequest {
public:
    struct Data {
        std::string host;
        std::uint16_t port;
        enum class Method {
            GET,
            POST
        } method;
        std::string url;
        enum class ContentType {
            TEXT,
            RAW
        } contentType;
        std::shared_ptr<std::vector<char>> body;
    };
private:
    int m_socketDesc = -1;
    Data m_data;
public:
    HTTPRequest(const Data& data);
    HTTPRequest(const HTTPRequest&) = delete;
    HTTPRequest(HTTPRequest&&) = default;
    ~HTTPRequest() noexcept;
    std::string send();
};

class HTTPRequestThread {
public:
    struct Data {
        HTTPRequest::Data request;
        std::function<void(const std::string&)> receivedResponse;
    };
private:
    Data m_data;
    std::unique_ptr<std::thread> m_thread;
    std::string m_response;
    void request();
public:
    HTTPRequestThread(const Data& data);
    HTTPRequestThread(const HTTPRequestThread&) = delete;
    HTTPRequestThread(HTTPRequestThread&&) = default;
    ~HTTPRequestThread() noexcept;
    void send();
    std::string waitForResponse() const;
};
