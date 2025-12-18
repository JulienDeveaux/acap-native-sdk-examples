/**
 * Copyright (C) 2025, Axis Communications AB, Lund, Sweden
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "fastcgi_server.h"
#include "image_buffer.h"

#include <fcgiapp.h>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <syslog.h>
#include <vector>

FastCGIServer::FastCGIServer() = default;

FastCGIServer::~FastCGIServer() {
    stop();
}

bool FastCGIServer::init(ImageBuffer* buffer) {
    if (!buffer) {
        syslog(LOG_ERR, "FastCGI: null image buffer");
        return false;
    }
    buffer_ = buffer;

    // Get socket path from environment
    const char* socket_path = getenv("FCGI_SOCKET_NAME");
    if (!socket_path) {
        syslog(LOG_ERR, "FastCGI: FCGI_SOCKET_NAME not set");
        return false;
    }

    syslog(LOG_INFO, "FastCGI: initializing with socket %s", socket_path);

    if (FCGX_Init() != 0) {
        syslog(LOG_ERR, "FastCGI: FCGX_Init failed");
        return false;
    }

    socket_fd_ = FCGX_OpenSocket(socket_path, 5);
    if (socket_fd_ < 0) {
        syslog(LOG_ERR, "FastCGI: failed to open socket");
        return false;
    }

    // Set socket permissions so Apache can connect
    chmod(socket_path, S_IRWXU | S_IRWXG | S_IRWXO);

    syslog(LOG_INFO, "FastCGI: initialized successfully");
    return true;
}

bool FastCGIServer::start() {
    if (running_) {
        return true;
    }

    running_ = true;
    server_thread_ = std::thread(&FastCGIServer::server_loop, this);
    syslog(LOG_INFO, "FastCGI: server started");
    return true;
}

void FastCGIServer::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    syslog(LOG_INFO, "FastCGI: server stopped");
}

void FastCGIServer::server_loop() {
    FCGX_Request request;
    if (FCGX_InitRequest(&request, socket_fd_, 0) != 0) {
        syslog(LOG_ERR, "FastCGI: FCGX_InitRequest failed");
        return;
    }

    while (running_) {
        // Accept with a timeout to allow checking running_ flag
        int result = FCGX_Accept_r(&request);
        if (result < 0) {
            if (running_) {
                syslog(LOG_WARNING, "FastCGI: FCGX_Accept_r returned %d", result);
            }
            continue;
        }

        handle_request(&request);
        FCGX_Finish_r(&request);
    }
}

void FastCGIServer::handle_request(void* request_ptr) {
    FCGX_Request* request = static_cast<FCGX_Request*>(request_ptr);

    const char* uri = FCGX_GetParam("REQUEST_URI", request->envp);
    if (!uri) {
        send_error_response(request_ptr, 400, "Bad Request: no URI");
        return;
    }

    syslog(LOG_DEBUG, "FastCGI: request URI=%s", uri);

    int image_index = parse_image_index(uri);
    if (image_index < 0 || image_index > 30) {
        send_error_response(request_ptr, 404, "Not Found: use ?index=0 to ?index=30");
        return;
    }

    std::vector<uint8_t> jpeg_data;
    if (!buffer_->get(static_cast<size_t>(image_index), jpeg_data)) {
        send_error_response(request_ptr, 503, "Service Unavailable: no image yet");
        return;
    }

    send_jpeg_response(request_ptr, jpeg_data.data(), jpeg_data.size());
}

int FastCGIServer::parse_image_index(const char* uri) const {
    // Support two URI formats:
    // 1. Query parameter: /local/flatten_image/image.cgi?index=0
    // 2. Path info: /local/flatten_image/image.cgi/0

    if (!uri) {
        return -1;
    }

    // First try query parameter format (?index=X)
    const char* query_start = strchr(uri, '?');
    if (query_start) {
        // Look for "index=" in query string
        const char* index_param = strstr(query_start, "index=");
        if (index_param) {
            const char* num_start = index_param + 6;  // Skip "index="
            char* end_ptr = nullptr;
            long index = strtol(num_start, &end_ptr, 10);
            if (end_ptr != num_start && index >= 0 && index <= 30) {
                return static_cast<int>(index);
            }
        }
    }

    // Fall back to path info format (/X at the end)
    // Find the last '/' in the URI (before query string if present)
    const char* search_end = query_start ? query_start : uri + strlen(uri);
    const char* last_slash = nullptr;
    for (const char* p = uri; p < search_end; p++) {
        if (*p == '/') {
            last_slash = p;
        }
    }

    if (!last_slash || last_slash + 1 >= search_end) {
        return -1;
    }

    const char* num_start = last_slash + 1;
    char* end_ptr = nullptr;
    long index = strtol(num_start, &end_ptr, 10);

    // Validate: must have consumed at least one digit
    if (end_ptr == num_start) {
        return -1;
    }

    // Check range
    if (index < 0 || index > 30) {
        return -1;
    }

    return static_cast<int>(index);
}

void FastCGIServer::send_jpeg_response(void* request_ptr, const uint8_t* data, size_t size) {
    FCGX_Request* request = static_cast<FCGX_Request*>(request_ptr);

    FCGX_FPrintF(request->out, "Content-Type: image/jpeg\r\n");
    FCGX_FPrintF(request->out, "Content-Length: %zu\r\n", size);
    FCGX_FPrintF(request->out, "Cache-Control: no-cache\r\n");
    FCGX_FPrintF(request->out, "\r\n");

    FCGX_PutStr(reinterpret_cast<const char*>(data), static_cast<int>(size), request->out);
}

void FastCGIServer::send_error_response(void* request_ptr, int status, const char* message) {
    FCGX_Request* request = static_cast<FCGX_Request*>(request_ptr);

    const char* status_text = "Error";
    switch (status) {
        case 400: status_text = "Bad Request"; break;
        case 404: status_text = "Not Found"; break;
        case 503: status_text = "Service Unavailable"; break;
    }

    FCGX_FPrintF(request->out, "Status: %d %s\r\n", status, status_text);
    FCGX_FPrintF(request->out, "Content-Type: text/plain\r\n");
    FCGX_FPrintF(request->out, "\r\n");
    FCGX_FPrintF(request->out, "%s\n", message);
}
