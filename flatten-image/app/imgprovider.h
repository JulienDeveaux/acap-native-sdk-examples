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

#include <stdbool.h>
#include <stdint.h>

#include "vdo-error.h"
#include "vdo-stream.h"
#include "vdo-types.h"

typedef struct img_provider {
    VdoFormat format;
    VdoStream* vdo_stream;
    unsigned int buffer_count;
    unsigned int width;
    unsigned int height;
    unsigned int pitch;
    double framerate;
    double requested_framerate;
    unsigned int rotation;
    unsigned int frametime;
    int fd;
} img_provider_t;

bool img_provider_start(img_provider_t* provider);
VdoBuffer* img_provider_get_frame(img_provider_t* provider);
void img_provider_flush_all_frames(img_provider_t* provider);

bool choose_stream_resolution(unsigned int req_width,
                              unsigned int req_height,
                              VdoFormat format,
                              const char* aspect_ratio,
                              const char* select,
                              unsigned int* chosen_width,
                              unsigned int* chosen_height);

img_provider_t* create_img_provider(unsigned int width,
                                    unsigned int height,
                                    unsigned int num_buffers,
                                    VdoFormat format,
                                    double framerate);

void destroy_img_provider(img_provider_t* provider);
