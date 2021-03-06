// Copyright 2016 Etix Labs
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "server.h"
#include <cstdio>
#include <cstdlib>
#include <stdlib.h>
#include <gst/app/gstappsink.h>
#include <gst/app/gstappsrc.h>
#include <iostream>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int count = 0;

typedef struct _App App;
struct _App {
  GstElement *videosink;
};
App s_app;

typedef struct {
  App *app;
  GstClockTime timestamp;
} Context;

// Receive data from videosink and push it downstream
static void need_data(GstElement *appsrc, guint unused, Context *ctx) {
  GstFlowReturn ret;
  GstSample *sample =
      gst_app_sink_pull_sample(GST_APP_SINK(ctx->app->videosink));
  if (sample != NULL) {
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    gst_app_src_push_sample(GST_APP_SRC(appsrc), sample);
    gst_sample_unref(sample);
    GST_BUFFER_PTS(buffer) = ctx->timestamp;
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale_int(1, GST_SECOND, 29);
    ctx->timestamp += GST_BUFFER_DURATION(buffer);
    g_signal_emit_by_name(appsrc, "push-buffer", buffer, &ret);
  }
}
// Configure the media to push properly data

static void media_configure(GstRTSPMediaFactory *factory, GstRTSPMedia *media,
                            App *app) {
  Context *ctx;
  GstElement *pipeline;
  GstElement *appsrc;
  pipeline = gst_rtsp_media_get_element(media);
  appsrc = gst_bin_get_by_name_recurse_up(GST_BIN(pipeline), "mysrc");
  gst_util_set_object_arg(G_OBJECT(appsrc), "format", "time");
  g_object_set(G_OBJECT(appsrc), "max-bytes",
               gst_app_src_get_max_bytes(GST_APP_SRC(appsrc)), NULL);
  ctx = g_new0(Context, 1);
  ctx->app = app;
  ctx->timestamp = 0;
  g_signal_connect(appsrc, "need-data", (GCallback)need_data, ctx);
}

// Bus message handler
bool r = true;
unsigned long long int videoBeginPoint = 0;

gboolean bus_callback(GstBus *bus, GstMessage *msg, gpointer data) {
  GstElement *pipeline = GST_ELEMENT(data);
  //std::cout << GST_MESSAGE_TYPE_NAME(msg) << "---- seqnum: " << GST_MESSAGE_SEQNUM(msg) << " ---- timestamp: " << GST_MESSAGE_TIMESTAMP(msg) << std::endl;
  switch (GST_MESSAGE_TYPE(msg)) {
    
    case GST_MESSAGE_EOS: // Catch EOS to reset TS 
      //if (!gst_element_seek (pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
      //                   GST_SEEK_TYPE_SET, 0000000000,
      //                   GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
      //  g_message("Seek failed!");
      //                      }
      break;
    case GST_MESSAGE_STREAM_START:
      if (!gst_element_seek (pipeline, 1.0, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
                         GST_SEEK_TYPE_SET, videoBeginPoint,
                         GST_SEEK_TYPE_NONE, GST_CLOCK_TIME_NONE)) {
        g_message("Seek failed!");
                            }
      break;
    default:
      break;
  }
  return TRUE;
}

inline bool file_exists(const std::string& name) {
  struct stat buffer;

  std::string corrected_name = name.substr(5);
  return (stat(corrected_name.c_str(), &buffer) == 0);
}
bool configure_file_input(t_server *serv) {
  // Setup and configuration

  App *app = &s_app;
  GstElement *playbin = gst_element_factory_make("playbin", "play");
  app->videosink = gst_element_factory_make("appsink", "video_sink");
  g_object_set(G_OBJECT(app->videosink), "emit-signals", TRUE, "sync", TRUE,
               NULL);
  g_object_set(G_OBJECT(playbin), "video-sink", app->videosink, NULL);
  GstBus *bus = gst_pipeline_get_bus(GST_PIPELINE(playbin));
  gst_bus_add_watch(bus, bus_callback, playbin);


  //setting up date to get specific video file
  //this variable will later be used to select the wanted file (or files?) according to user input
  std::string dateString = serv->config->date;

  std::string input_path = "file:///" + serv->config->input;

  //setting up video starting time

  std::string beginTime = serv->config->beginTime;
  unsigned long long int videoStartTime = 0;

  if (beginTime.substr(0,2) != "00"){
    videoStartTime += 3600000000000 * stoull(beginTime.substr(0,2), nullptr, 10);
  }

  if (beginTime.substr(3,2) != "00"){
    videoStartTime += 60000000000 * stoull(beginTime.substr(3,2), nullptr, 10);
  }

  if (beginTime.substr(6,2) != "00"){
    videoStartTime += 1000000000 * stoull(beginTime.substr(6,2), nullptr, 10);
  }

  //setting up video jump to
  std::string jumpTo = serv->config->jumpTo;

  if (jumpTo.substr(0,2) != "00"){
    videoBeginPoint += 3600000000000 * stoull(jumpTo.substr(0,2), nullptr, 10);
  }

  if (jumpTo.substr(3,2) != "00"){
    videoBeginPoint += 60000000000 * stoull(jumpTo.substr(3,2), nullptr, 10);
  }

  if (jumpTo.substr(6,2) != "00"){
    videoBeginPoint += 1000000000 * stoull(jumpTo.substr(6,2), nullptr, 10);
  }

  //time, in nanoseconds, that will be added to the begining of the video
  //it is defined by the point where we want it to jump to minus the time
  //when the video actually started

  videoBeginPoint -= videoStartTime; //for some reason the stream always start with 1 second added to the
                                                  //begining point set
  
  g_object_set(G_OBJECT(playbin), "uri", input_path.c_str(), NULL);

  gst_element_set_state(playbin, GST_STATE_PLAYING);
  // Media

  g_signal_connect(serv->factory, "media-configure", (GCallback)media_configure,
  	app);
  if (!file_exists(input_path)) {
    std::cerr << "Can't access " << input_path.c_str() << std::endl;
    return false;
  }

  return true;
}
