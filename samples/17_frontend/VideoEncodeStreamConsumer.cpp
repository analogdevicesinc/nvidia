/*
 * Copyright (c) 2016-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include "VideoEncodeStreamConsumer.h"
#include <EGLStream/NV/ImageNativeBuffer.h>
#include "Error.h"
#include "NvBufSurface.h"

#define MAX_QUEUE_SIZE (10)

extern bool g_bVerbose;

VideoEncodeStreamConsumer::VideoEncodeStreamConsumer(const char *name,
        const char *outputFilename, Size2D<uint32_t> size, uint32_t pixfmt) :
    StreamConsumer(name, size),
    m_VideoEncoder(name, outputFilename, size.width(), size.height(), pixfmt)
{
    m_VideoEncoder.setBufferDoneCallback(bufferDoneCallback, this);
}

VideoEncodeStreamConsumer::~VideoEncodeStreamConsumer()
{
}

bool VideoEncodeStreamConsumer::threadInitialize()
{
    NvBufSurf::NvCommonAllocateParams input_params = {0};

    if (!StreamConsumer::threadInitialize())
        return false;

    input_params.memType = NVBUF_MEM_SURFACE_ARRAY;
    input_params.width = m_size.width();
    input_params.height = m_size.height();
    input_params.layout = NVBUF_LAYOUT_BLOCK_LINEAR;
    input_params.colorFormat = NVBUF_COLOR_FORMAT_YUV420;
    input_params.memtag = NvBufSurfaceTag_NONE;

    // Create buffers
    for (unsigned i = 0; i < MAX_QUEUE_SIZE; i++)
    {
        int dmabuf_fd;

        if (NvBufSurf::NvAllocate(&input_params, 1, &dmabuf_fd) < 0)
            ORIGINATE_ERROR("Failed to create NvBuffer.");

        m_emptyBufferQueue.push(dmabuf_fd);
    }

    // Init encoder
    m_VideoEncoder.initialize();

    return true;
}

bool VideoEncodeStreamConsumer::processFrame(Frame *frame)
{
    IFrame *iFrame = interface_cast<IFrame>(frame);
    if (iFrame == NULL)
    {
        m_VideoEncoder.encodeFromFd(-1);    // EOS
        return false;
    }

    if (g_bVerbose)
        Log("%s: frame %d\n", __func__, iFrame->getNumber());

    int dmabuf_fd = m_emptyBufferQueue.pop();

    // Get the IImageNativeBuffer extension interface and copy to NvBuffer.
    NV::IImageNativeBuffer *iNativeBuffer =
        interface_cast<NV::IImageNativeBuffer>(iFrame->getImage());
    if (!iNativeBuffer)
        ORIGINATE_ERROR("IImageNativeBuffer not supported by Image.");

    iNativeBuffer->copyToNvBuffer(dmabuf_fd);

    m_VideoEncoder.encodeFromFd(dmabuf_fd);

    return true;
}

bool VideoEncodeStreamConsumer::threadShutdown()
{
    m_VideoEncoder.shutdown();

    // Ensure all buffer are returned by encoder
    assert(m_emptyBufferQueue.size() == MAX_QUEUE_SIZE);

    // Destroy all buffers
    while (m_emptyBufferQueue.size() > 0)
        NvBufSurf::NvDestroy(m_emptyBufferQueue.pop());

    return StreamConsumer::threadShutdown();
}

void VideoEncodeStreamConsumer::bufferDoneCallback(int dmabuf_fd)
{
    m_emptyBufferQueue.push(dmabuf_fd);
}
