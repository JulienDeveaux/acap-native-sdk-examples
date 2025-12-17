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

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>
#include <opencv2/core.hpp>
#include <atomic>
#include <mutex>
#include <string>

struct RTSPServerConfig {
    unsigned int port   = 8554;
    unsigned int width  = 1920;
    unsigned int height = 1080;
    unsigned int fps    = 15;
    std::string mount_point = "/stream";
};

class RTSPServer {
public:
    RTSPServer();
    ~RTSPServer();

    bool init(const RTSPServerConfig& config);
    bool start();
    void stop();

    // Push a BGR frame to the stream
    bool push_frame(const cv::Mat& bgr_frame);

    bool is_running() const { return running_; }

private:
    static void need_data_callback(GstElement* appsrc, guint unused, gpointer user_data);
    static void media_configure_callback(GstRTSPMediaFactory* factory,
                                         GstRTSPMedia* media,
                                         gpointer user_data);

    void on_need_data(GstElement* appsrc);

    RTSPServerConfig config_;
    GstRTSPServer* server_        = nullptr;
    GstRTSPMountPoints* mounts_   = nullptr;
    GstRTSPMediaFactory* factory_ = nullptr;
    GstElement* appsrc_           = nullptr;
    GMainLoop* main_loop_         = nullptr;

    std::mutex frame_mutex_;
    cv::Mat current_frame_;
    std::atomic<bool> frame_available_{false};
    std::atomic<bool> running_{false};
    guint64 timestamp_ = 0;
};
