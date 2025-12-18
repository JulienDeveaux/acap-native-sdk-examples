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

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

// Thread-safe circular buffer for storing JPEG images
// Index 0 = most recent, Index 30 = oldest (30 seconds ago)
class ImageBuffer {
public:
    static constexpr size_t BUFFER_SIZE = 31;  // 0-30 seconds of history

    ImageBuffer();
    ~ImageBuffer() = default;

    // Store a new JPEG image (called every second)
    // Automatically rotates buffer, oldest image dropped
    void push(const uint8_t* jpeg_data, size_t jpeg_size);

    // Get image at relative index (0=latest, 1=1sec ago, etc)
    // Returns false if index out of range or no image available
    // Caller provides buffer which will be resized and filled
    bool get(size_t relative_index, std::vector<uint8_t>& out_jpeg) const;

    // Check if an image exists at the given relative index
    bool has_image(size_t relative_index) const;

private:
    struct ImageSlot {
        std::vector<uint8_t> data;
        bool valid = false;
    };

    mutable std::mutex mutex_;
    std::vector<ImageSlot> slots_;
    size_t write_index_ = 0;  // Next slot to write to
    size_t image_count_ = 0;  // Number of valid images stored
};
