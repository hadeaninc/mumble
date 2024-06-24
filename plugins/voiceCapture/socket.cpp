#include "socket.hpp"
#include "log.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

// MARK: HTTPRequest
// Adapted from: https://stackoverflow.com/a/70552118.

HTTPRequest::HTTPRequest(const Data& data) : m_data(data) {
    // Allocate socket.
    m_socketDesc = ::socket(AF_INET, SOCK_STREAM, 0);
    if (m_socketDesc < 0) {
        throw request_exception("failed to create Hadean services socket!");
    }

    // Obtain host address and attempt to connect to the host via the given port.
    const auto server = ::gethostbyname(m_data.host.c_str());
    if (!server) {
        throw request_exception("could not resolve host \"" +
            m_data.host + "\" for Hadean services socket!");
    }
    struct sockaddr_in serverAddress;
    ::memset(&serverAddress, 0, sizeof(serverAddress));
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = ::htons(m_data.port);
    ::memcpy(reinterpret_cast<void*>(&serverAddress.sin_addr.s_addr),
        reinterpret_cast<void*>(server->h_addr), server->h_length);
    if (::connect(m_socketDesc,
        reinterpret_cast<struct sockaddr*>(&serverAddress),
        sizeof(serverAddress)) < 0) {
        throw request_exception("could not connect to the Hadean services socket on port " +
            std::to_string(m_data.port) + "!");
    }
}

HTTPRequest::~HTTPRequest() noexcept {
    if (m_socketDesc < 0) { return; }
    ::close(m_socketDesc);
}

std::string HTTPRequest::send() {
    // Prevent sending the request if the socket has been closed.
    if (m_socketDesc < 0) {
        throw request_exception("Hadean services socket is in a bad state");
    }

    // Build the request header and body.
    std::stringstream requestBuilder;
    switch (m_data.method) {
    case Data::Method::GET:
        requestBuilder << "GET ";
        break;
    case Data::Method::POST:
        requestBuilder << "POST ";
        break;
    default:
        throw request_exception("invalid HTTP method " +
            std::to_string(static_cast<int>(m_data.method)));
    }
    requestBuilder << m_data.url << " HTTP/1.1\r\nContent-Type: ";
    switch (m_data.contentType) {
    case Data::ContentType::TEXT:
        requestBuilder << "text/plain";
        break;
    case Data::ContentType::RAW:
        requestBuilder << "application/octet-stream";
        break;
    default:
        throw request_exception("invalid content type " +
            std::to_string(static_cast<int>(m_data.contentType)));
    }
    const std::size_t contentLength = m_data.body ? m_data.body->size() : 0;
    requestBuilder << "\r\nContent-Length: " << contentLength <<
        // Host argument is required for the express.js webserver,
        // it will reject the request with 400 otherwise.
        // Pretty sure the value can be anything.
        "\r\nHost: localhost\r\n\r\n";
    if (m_data.body) { requestBuilder.write(&m_data.body->front(), contentLength); }

    // Attempt to make the request.
    const auto httpRequest = requestBuilder.str();
    if (::send(m_socketDesc, httpRequest.c_str(), httpRequest.size(), 0) < 0) {
        throw request_exception("couldn't send request to Hadean services!");
    }

    // Request successful, now read the response.
    std::string response;
    const auto startedAt = std::chrono::steady_clock::now();
    for (;;) {
        char buffer[2048];
        int byteCount = ::recv(m_socketDesc, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (byteCount == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            // We're not using this class for large responses right now,
            // so if any response has been retrieved, break early.
            // TODO: This approach will need updating if we start dealing with large responses.
            if (!response.empty()) {
                break;
            }
            if (std::chrono::steady_clock::now() - startedAt >= std::chrono::seconds{8}) {
                throw request_exception("couldn't retrieve response from Hadean services: timed out!");
            }
        } else if (byteCount < 0) {
            throw request_exception("couldn't retrieve response from Hadean services! Error code " +
                std::to_string(errno));
        } else if (byteCount > 0) {
            response.append(buffer, byteCount);
        } else if (byteCount == 0) {
            break;
        }
    }

    // Response successful, close the socket (to prevent resubmission of request),
    // and extract body from the response.
    ::close(m_socketDesc);
    m_socketDesc = -1;
    const auto bodyStart = response.find("\r\n\r\n");
    if (bodyStart == std::string::npos) { return ""; }
    return response.substr(bodyStart + 4);
}

// MARK: HTTPRequestThread

HTTPRequestThread::HTTPRequestThread(const Data& data) : m_data(data) {}

HTTPRequestThread::~HTTPRequestThread() noexcept {
    waitForResponse();
}

void HTTPRequestThread::request() {
    try {
        HTTPRequest httpRequest(m_data.request);
        m_response = httpRequest.send();
        if (m_data.receivedResponse) { m_data.receivedResponse(m_response); }
    } catch (const std::exception& e) {
        LOG("ERROR: couldn't complete HTTP request in thread: " << e.what());
    }
}

void HTTPRequestThread::send() {
    m_thread = std::make_unique<std::thread>(&HTTPRequestThread::request, this);
}

std::string HTTPRequestThread::waitForResponse() const {
    if (m_thread && m_thread->joinable()) { m_thread->join(); }
    return m_response;
}
