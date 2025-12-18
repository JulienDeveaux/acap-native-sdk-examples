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

#include "image_buffer.h"

ImageBuffer::ImageBuffer() : slots_(BUFFER_SIZE) {}

void ImageBuffer::push(const uint8_t* jpeg_data, size_t jpeg_size) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Copy JPEG data to current slot
    ImageSlot& slot = slots_[write_index_];
    slot.data.assign(jpeg_data, jpeg_data + jpeg_size);
    slot.valid = true;

    // Advance write index (circular)
    write_index_ = (write_index_ + 1) % BUFFER_SIZE;

    // Track count until buffer is full
    if (image_count_ < BUFFER_SIZE) {
        image_count_++;
    }
}

bool ImageBuffer::get(size_t relative_index, std::vector<uint8_t>& out_jpeg) const {
    if (relative_index >= BUFFER_SIZE) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (image_count_ == 0) {
        return false;
    }

    // Calculate actual buffer index
    // write_index_ points to next write location
    // Most recent image is at (write_index_ - 1 + BUFFER_SIZE) % BUFFER_SIZE
    size_t latest_index = (write_index_ + BUFFER_SIZE - 1) % BUFFER_SIZE;
    size_t actual_index = (latest_index + BUFFER_SIZE - relative_index) % BUFFER_SIZE;

    // Check if we have an image at this index
    if (relative_index >= image_count_) {
        return false;
    }

    const ImageSlot& slot = slots_[actual_index];
    if (!slot.valid || slot.data.empty()) {
        return false;
    }

    out_jpeg = slot.data;
    return true;
}

bool ImageBuffer::has_image(size_t relative_index) const {
    if (relative_index >= BUFFER_SIZE) {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    return relative_index < image_count_;
}
