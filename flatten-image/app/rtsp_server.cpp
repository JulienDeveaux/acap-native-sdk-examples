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

#include "rtsp_server.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wfloat-equal"
#include <opencv2/imgproc.hpp>
#pragma GCC diagnostic pop

#include <gst/app/gstappsrc.h>
#include <syslog.h>
#include <cstring>

RTSPServer::RTSPServer() {}

RTSPServer::~RTSPServer() {
    stop();
}

bool RTSPServer::init(const RTSPServerConfig& config) {
    config_ = config;

    gst_init(nullptr, nullptr);

    server_ = gst_rtsp_server_new();
    if (!server_) {
        syslog(LOG_ERR, "Failed to create RTSP server");
        return false;
    }

    gchar port_str[16];
    snprintf(port_str, sizeof(port_str), "%u", config_.port);
    gst_rtsp_server_set_service(server_, port_str);

    mounts_ = gst_rtsp_server_get_mount_points(server_);
    if (!mounts_) {
        syslog(LOG_ERR, "Failed to get mount points");
        return false;
    }

    factory_ = gst_rtsp_media_factory_new();
    if (!factory_) {
        syslog(LOG_ERR, "Failed to create media factory");
        return false;
    }

    // Create pipeline with appsrc feeding into x264 encoder
    gchar* launch_str = g_strdup_printf(
        "( appsrc name=source is-live=true format=time "
        "caps=video/x-raw,format=BGR,width=%u,height=%u,framerate=%u/1 "
        "! videoconvert "
        "! video/x-raw,format=I420 "
        "! x264enc tune=zerolatency bitrate=2000 speed-preset=ultrafast "
        "! rtph264pay name=pay0 pt=96 )",
        config_.width,
        config_.height,
        config_.fps);

    gst_rtsp_media_factory_set_launch(factory_, launch_str);
    g_free(launch_str);

    gst_rtsp_media_factory_set_shared(factory_, TRUE);

    g_signal_connect(factory_, "media-configure", G_CALLBACK(media_configure_callback), this);

    gst_rtsp_mount_points_add_factory(mounts_, config_.mount_point.c_str(), factory_);
    g_object_unref(mounts_);

    syslog(LOG_INFO,
           "RTSP server initialized on port %u, mount point %s",
           config_.port,
           config_.mount_point.c_str());

    return true;
}

bool RTSPServer::start() {
    if (running_) {
        return true;
    }

    if (gst_rtsp_server_attach(server_, nullptr) == 0) {
        syslog(LOG_ERR, "Failed to attach RTSP server");
        return false;
    }

    running_ = true;
    syslog(LOG_INFO,
           "RTSP server started at rtsp://DEVICE_IP:%u%s",
           config_.port,
           config_.mount_point.c_str());

    return true;
}

void RTSPServer::stop() {
    running_ = false;

    if (server_) {
        g_object_unref(server_);
        server_ = nullptr;
    }
}

bool RTSPServer::push_frame(const cv::Mat& bgr_frame) {
    if (!running_) {
        return false;
    }

    std::lock_guard<std::mutex> lock(frame_mutex_);
    bgr_frame.copyTo(current_frame_);
    frame_available_ = true;

    return true;
}

void RTSPServer::media_configure_callback(GstRTSPMediaFactory* factory,
                                          GstRTSPMedia* media,
                                          gpointer user_data) {
    (void)factory;
    RTSPServer* self = static_cast<RTSPServer*>(user_data);

    GstElement* element = gst_rtsp_media_get_element(media);
    GstElement* appsrc  = gst_bin_get_by_name_recurse_up(GST_BIN(element), "source");

    if (appsrc) {
        self->appsrc_ = appsrc;
        g_signal_connect(appsrc, "need-data", G_CALLBACK(need_data_callback), user_data);
        syslog(LOG_INFO, "Media configured, appsrc connected");
    }

    gst_object_unref(element);
}

void RTSPServer::need_data_callback(GstElement* appsrc, guint unused, gpointer user_data) {
    (void)unused;
    RTSPServer* self = static_cast<RTSPServer*>(user_data);
    self->on_need_data(appsrc);
}

void RTSPServer::on_need_data(GstElement* appsrc) {
    if (!frame_available_) {
        return;
    }

    cv::Mat frame;
    {
        std::lock_guard<std::mutex> lock(frame_mutex_);
        if (current_frame_.empty()) {
            return;
        }
        current_frame_.copyTo(frame);
        frame_available_ = false;
    }

    gsize size = frame.total() * frame.elemSize();
    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, size, nullptr);

    GstMapInfo map;
    gst_buffer_map(buffer, &map, GST_MAP_WRITE);
    std::memcpy(map.data, frame.data, size);
    gst_buffer_unmap(buffer, &map);

    // Set timestamp
    guint64 duration = gst_util_uint64_scale_int(1, GST_SECOND, config_.fps);
    GST_BUFFER_PTS(buffer)      = timestamp_;
    GST_BUFFER_DURATION(buffer) = duration;
    timestamp_ += duration;

    GstFlowReturn ret;
    g_signal_emit_by_name(appsrc, "push-buffer", buffer, &ret);
    gst_buffer_unref(buffer);
}
