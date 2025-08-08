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

#ifndef __TRTSTREAMCONSUMER_H__
#define __TRTSTREAMCONSUMER_H__

#include <vector>
#include "Queue.h"
#include "StreamConsumer.h"
#include "VideoEncoder.h"
#include "trt_inference.h"
#include "nvosd.h"

struct BufferInfo
{
    int fd; // DMABUF Fd of the buffer
    int number; // Frame number of the buffer
};

#define CLASS_NUM 4

typedef cv::Rect_<float> Rect2f;

class NvEglRenderer;

class TRTStreamConsumer : public StreamConsumer
{
public:
    TRTStreamConsumer(const char *name, const char *outputFilename, Size2D<uint32_t> size,
            NvEglRenderer *renderer, bool hasEncoding = true);
    virtual ~TRTStreamConsumer();
    virtual bool threadInitialize();
    virtual bool threadShutdown();
    virtual bool processFrame(Frame *frame);

    void initTRTContext();

    void setEngineFile(const string &file) { m_engineFile = file; }

private:
    static void* RenderThreadProc(void *thiz)
    {
        ((TRTStreamConsumer*)thiz)->RenderThreadProc();
        return NULL;
    }
    static void* TRTThreadProc(void *thiz)
    {
        ((TRTStreamConsumer*)thiz)->TRTThreadProc();
        return NULL;
    }
    static void bufferDoneCallback(int dmabuf_fd, void *arg)
    {
        TRTStreamConsumer *thiz = static_cast<TRTStreamConsumer*>(arg);
        thiz->bufferDoneCallback(dmabuf_fd);
    }

    bool RenderThreadProc();
    bool TRTThreadProc();
    void bufferDoneCallback(int dmabuf_fd);

    pthread_t m_renderThread;
    pthread_t m_trtThread;

    Queue<vector<Rect2f>*> m_bboxesQueue[CLASS_NUM];  // Inference result

    std::string m_engineFile;
    Queue<int> m_emptyBufferQueue;
    Queue<int> m_emptyTRTBufferQueue;
    Queue<BufferInfo> m_renderBufferQueue;
    Queue<BufferInfo> m_trtBufferQueue;
    vector<NvOSD_RectParams> m_rectParams;

    // Encoder support
    VideoEncoder m_VideoEncoder;
    bool m_hasEncoding;

    // TRT support
    TRT_Context m_TRTContext;

    // OSD support
    void *nvosd_context;

    // EGL render
    NvEglRenderer *m_eglRenderer;
    NvElementProfiler::NvElementProfilerData m_profilerData;
    float m_fps;

};

#endif  // __TRTSTREAMCONSUMER_H__
