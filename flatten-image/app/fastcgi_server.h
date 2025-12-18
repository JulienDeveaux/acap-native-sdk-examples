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

#pragma once

#include <atomic>
#include <string>
#include <thread>

class ImageBuffer;

// FastCGI server that serves images from the buffer
// Endpoints: /0 (latest) to /30 (30 seconds ago)
class FastCGIServer {
public:
    FastCGIServer();
    ~FastCGIServer();

    // Initialize with reference to image buffer
    // socket_path: Unix socket path from FCGI_SOCKET_NAME env var
    bool init(ImageBuffer* buffer);

    // Start the server in a background thread
    bool start();

    // Stop the server
    void stop();

private:
    void server_loop();
    void handle_request(void* request_ptr);
    int parse_image_index(const char* uri) const;
    void send_jpeg_response(void* request_ptr, const uint8_t* data, size_t size);
    void send_error_response(void* request_ptr, int status, const char* message);

    ImageBuffer* buffer_ = nullptr;
    std::thread server_thread_;
    std::atomic<bool> running_{false};
    int socket_fd_ = -1;
};
