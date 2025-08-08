/*
 * SPDX-FileCopyrightText: Copyright (c) 2016-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "NvVideoDecoder.h"
#include "NvEglRenderer.h"
#include <queue>
#include <fstream>
#include <pthread.h>
#include <semaphore.h>

#include "NvBufSurface.h"

#define MAX_BUFFERS 32
#define MAX_VIEWID 32

typedef struct
{
    NvVideoDecoder *dec;
    uint32_t decoder_pixfmt;

    NvEglRenderer *renderer;

    char **in_file_path;
    std::ifstream **in_file;

    char *out_file_path;
    std::ofstream *out_file;

    bool disable_rendering;
    bool fullscreen;
    uint32_t window_height;
    uint32_t window_width;
    uint32_t window_x;
    uint32_t window_y;
    uint32_t out_pixfmt;
    uint32_t video_height;
    uint32_t video_width;
    uint32_t display_height;
    uint32_t display_width;
    uint32_t file_count;
    float fps;

    bool disable_dpb;

    bool input_nalu;
    bool enable_sld;
    bool is_gdr_stream;

    bool copy_timestamp;
    bool flag_copyts;
    uint32_t start_ts;
    float dec_fps;
    uint64_t timestamp;
    uint64_t timestampincr;

    bool stats;

    int  stress_test;
    bool enable_metadata;
    bool bLoop;
    bool bQueue;
    bool enable_input_metadata;
    enum v4l2_skip_frames_type skip_frames;
    enum v4l2_memory output_plane_mem_type;
    enum v4l2_memory capture_plane_mem_type;

    std::queue < NvBuffer * > *conv_output_plane_buf_queue;
    pthread_mutex_t queue_lock;
    pthread_cond_t queue_cond;

    sem_t pollthread_sema; // Polling thread waits on this to be signalled to issue Poll
    sem_t decoderthread_sema; // Decoder thread waits on this to be signalled to continue q/dq loop
    pthread_t   dec_pollthread; // Polling thread, created if running in non-blocking mode.

    pthread_t dec_capture_loop; // Decoder capture thread, created if running in blocking mode.
    bool got_error;
    bool got_eos;
    bool vp9_file_header_flag;
    bool vp8_file_header_flag;
    int dst_dma_fd;
    int dmabuff_fd[MAX_BUFFERS];
    int numCapBuffers;
    int loop_count;
    int max_perf;
    int extra_cap_plane_buffer;
    int blocking_mode; // Set to true if running in blocking mode
    int output_plane_fd[MAX_BUFFERS];
    int operating_point;
    uint8_t max_operating_points;
    bool enable_av1_mvc;
    int viewId;
    int picture_width[MAX_VIEWID];
    int picture_height[MAX_VIEWID];
    int dst_dma_fd_mvc[MAX_VIEWID];
    std::ofstream *out_file_mvc[MAX_VIEWID];
} context_t;

int parse_csv_args(context_t * ctx, int argc, char *argv[]);
