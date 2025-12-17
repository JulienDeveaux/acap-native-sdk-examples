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

#include "imgprovider.h"

#include <assert.h>
#include <errno.h>
#include <glib-object.h>
#include <gmodule.h>
#include <math.h>
#include <poll.h>
#include <syslog.h>

#include "panic.h"
#include "vdo-map.h"
#include <vdo-channel.h>
#include <vdo-error.h>

G_DEFINE_AUTOPTR_CLEANUP_FUNC(VdoResolutionSet, g_free);

#define VDO_INPUT_CHANNEL (1)

img_provider_t* create_img_provider(unsigned int width,
                                    unsigned int height,
                                    unsigned int num_buffers,
                                    VdoFormat format,
                                    double framerate) {
    g_autoptr(VdoMap) vdo_settings = vdo_map_new();
    g_autoptr(GError) error        = NULL;

    img_provider_t* provider = (img_provider_t*)calloc(1, sizeof(img_provider_t));
    if (!provider) {
        panic("%s: Unable to allocate ImgProvider: %s", __func__, strerror(errno));
    }

    provider->format       = format;
    provider->buffer_count = num_buffers;
    provider->vdo_stream   = NULL;
    provider->fd           = -1;

    if (!vdo_settings) {
        panic("%s: Failed to create vdo_map", __func__);
    }

    vdo_map_set_uint32(vdo_settings, "input", VDO_INPUT_CHANNEL);
    vdo_map_set_uint32(vdo_settings, "format", format);
    vdo_map_set_uint32(vdo_settings, "width", width);
    vdo_map_set_uint32(vdo_settings, "height", height);
    vdo_map_set_double(vdo_settings, "framerate", framerate);
    vdo_map_set_boolean(vdo_settings, "dynamic.framerate", true);
    vdo_map_set_uint32(vdo_settings, "buffer.count", provider->buffer_count);
    vdo_map_set_boolean(vdo_settings, "socket.blocking", false);

    syslog(LOG_INFO, "Creating VDO stream settings");
    vdo_map_dump(vdo_settings);

    g_autoptr(VdoStream) vdo_stream = vdo_stream_new(vdo_settings, NULL, &error);
    if (!vdo_stream) {
        panic("%s: Failed creating vdo stream: %s", __func__, error->message);
    }

    g_autoptr(VdoMap) vdo_info = vdo_stream_get_info(vdo_stream, &error);
    if (!vdo_info) {
        panic("%s: Failed to get info map for stream: %s", __func__, error->message);
    }

    provider->pitch               = vdo_map_get_uint32(vdo_info, "pitch", width);
    provider->height              = vdo_map_get_uint32(vdo_info, "height", height);
    provider->width               = vdo_map_get_uint32(vdo_info, "width", width);
    provider->framerate           = vdo_map_get_double(vdo_info, "framerate", framerate);
    provider->rotation            = vdo_map_get_uint32(vdo_info, "rotation", 0);
    provider->requested_framerate = framerate;
    provider->frametime           = static_cast<unsigned int>((1 / provider->framerate) * 1000);
    provider->vdo_stream          = g_steal_pointer(&vdo_stream);

    return provider;
}

void destroy_img_provider(img_provider_t* provider) {
    assert(provider);
    g_clear_object(&provider->vdo_stream);
    free(provider);
}

bool img_provider_start(img_provider_t* provider) {
    g_autoptr(GError) error = NULL;
    assert(provider);

    if (!vdo_stream_start(provider->vdo_stream, &error)) {
        panic("%s: Failed to start stream: %s", __func__, error->message);
    }

    int fd = vdo_stream_get_fd(provider->vdo_stream, &error);
    if (fd < 0) {
        panic("%s: Failed to get fd for stream: %s", __func__, error->message);
    }
    provider->fd = fd;
    return true;
}

VdoBuffer* img_provider_get_frame(img_provider_t* provider) {
    g_autoptr(GError) error = NULL;
    assert(provider);

    struct pollfd fds = {
        .fd     = provider->fd,
        .events = POLL_IN,
    };

    while (true) {
        int status = 0;
        do {
            status = poll(&fds, 1, -1);
        } while (status == -1 && errno == EINTR);

        if (status < 0) {
            panic("%s: Failed to poll fd: %s", __func__, strerror(errno));
        }

        g_autoptr(VdoBuffer) vdo_buf = vdo_stream_get_buffer(provider->vdo_stream, &error);
        if (!vdo_buf) {
            if (g_error_matches(error, VDO_ERROR, VDO_ERROR_NO_DATA)) {
                g_clear_object(&error);
                continue;
            }
            if (vdo_error_is_expected(&error)) {
                syslog(LOG_INFO, "Likely global rotation: %s", error->message);
                return NULL;
            } else {
                panic("%s: Unexpected error: %s", __func__, error->message);
            }
        }
        return g_steal_pointer(&vdo_buf);
    }
}

void img_provider_flush_all_frames(img_provider_t* provider) {
    g_autoptr(GError) error = NULL;
    assert(provider);

    while (true) {
        g_autoptr(VdoBuffer) read_vdo_buf = vdo_stream_get_buffer(provider->vdo_stream, NULL);
        if (!read_vdo_buf) {
            break;
        }
        if (!vdo_stream_buffer_unref(provider->vdo_stream, &read_vdo_buf, &error)) {
            if (!vdo_error_is_expected(&error)) {
                panic("%s: Unexpected error: %s", __func__, error->message);
            }
        }
    }
}

bool choose_stream_resolution(unsigned int req_width,
                              unsigned int req_height,
                              VdoFormat format,
                              const char* aspect_ratio,
                              const char* select,
                              unsigned int* chosen_width,
                              unsigned int* chosen_height) {
    g_autoptr(VdoResolutionSet) set     = NULL;
    g_autoptr(VdoChannel) channel       = NULL;
    g_autoptr(GError) error             = NULL;
    g_autoptr(VdoMap) resolution_filter = vdo_map_new();

    assert(chosen_width);
    assert(chosen_height);

    g_autoptr(VdoMap) ch_desc = vdo_map_new();
    vdo_map_set_uint32(ch_desc, "input", VDO_INPUT_CHANNEL);
    channel = vdo_channel_get_ex(ch_desc, &error);
    if (!channel) {
        panic("%s: Failed vdo_channel_get(): %s", __func__, error->message);
    }

    vdo_map_set_uint32(resolution_filter, "format", format);
    vdo_map_set_string(resolution_filter, "select", "minmax");
    if (select != nullptr) {
        vdo_map_set_string(resolution_filter, "select", select);
    }
    if (aspect_ratio) {
        vdo_map_set_string(resolution_filter, "aspect_ratio", aspect_ratio);
    }
    set = vdo_channel_get_resolutions(channel, resolution_filter, &error);
    if (!set) {
        panic("%s: Failed vdo_channel_get_resolutions(): %s", __func__, error->message);
    }

    if (select != nullptr && !g_strcmp0(select, "all")) {
        ssize_t best_resolution_idx       = -1;
        unsigned int best_resolution_area = UINT_MAX;
        for (ssize_t i = 0; i < set->count; ++i) {
            VdoResolution* res = &set->resolutions[i];
            if ((res->width >= req_width) && (res->height >= req_height)) {
                unsigned int area = res->width * res->height;
                if (area < best_resolution_area) {
                    best_resolution_idx  = i;
                    best_resolution_area = area;
                }
            }
        }

        *chosen_width  = req_width;
        *chosen_height = req_height;
        if (best_resolution_idx >= 0) {
            *chosen_width  = set->resolutions[best_resolution_idx].width;
            *chosen_height = set->resolutions[best_resolution_idx].height;
        } else {
            syslog(LOG_WARNING,
                   "%s: VDO channel info contains no resolution info. Fallback "
                   "to client-requested stream resolution.",
                   __func__);
        }
    } else {
        *chosen_width  = req_width;
        *chosen_height = req_height;

        if (req_width > set->resolutions[1].width || req_height > set->resolutions[1].height) {
            *chosen_width  = set->resolutions[1].width;
            *chosen_height = set->resolutions[1].height;
            syslog(LOG_WARNING,
                   "%s: Requested resolution larger than max. Limiting to %ux%u.",
                   __func__,
                   set->resolutions[1].width,
                   set->resolutions[1].height);
        }
        if (req_width < set->resolutions[0].width || req_height < set->resolutions[0].height) {
            *chosen_width  = set->resolutions[0].width;
            *chosen_height = set->resolutions[0].height;
            syslog(LOG_WARNING,
                   "%s: Requested resolution smaller than min. Limiting to %ux%u.",
                   __func__,
                   set->resolutions[0].width,
                   set->resolutions[0].height);
        }
    }

    syslog(LOG_INFO,
           "%s: Selected stream resolution %u x %u based on VDO channel info.",
           __func__,
           *chosen_width,
           *chosen_height);

    return true;
}
