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

#include "NvUtils.h"
#include <errno.h>
#include <fstream>
#include <iostream>
#include <malloc.h>
#include <string.h>
#include <unistd.h>

#include "jpeg_encode.h"

#define TEST_ERROR(cond, str, label) if(cond) { \
                                        cerr << str << endl; \
                                        error = 1; \
                                        goto label; }

#define PERF_LOOP   300

using namespace std;

static void
set_defaults(context_t * ctx)
{
    memset(ctx, 0, sizeof(context_t));
    ctx->perf = false;
    ctx->use_fd = true;
    ctx->in_pixfmt = V4L2_PIX_FMT_YUV420M;
    ctx->stress_test = 1;
    ctx->quality = 75;
}

/**
 * Class NvJPEGEncoder encodes YUV420 image to JPEG.
 * NvJPEGEncoder::encodeFromBuffer() encodes from software buffer memory
 * which can be access by CPU directly.
 * NvJPEGEncoder::encodeFromFd() encodes from hardware buffer memory which is faster
 * than NvJPEGEncoder::encodeFromBuffer() since the latter involves conversion
 * from software buffer memory to hardware buffer memory.
 *
 * When using NvJPEGEncoder::encodeFromFd(), NvUtils is used to
 * convert MMAP buffer (CPU buffer holding YUV420 image) to hardware buffer memory
 * (DMA buffer fd). There may be YUV420 to NV12 conversion depends on commandline
 * argument.
 */
static int
jpeg_encode_proc(context_t& ctx, int argc, char *argv[])
{
    int ret = 0;
    int error = 0;
    int iterator_num = 1;
    int src_dma_fd = -1;
    int dst_dma_fd = -1;
    unsigned long out_buf_size;
    unsigned char *out_buf;

    set_defaults(&ctx);

    ret = parse_csv_args(&ctx, argc, argv);
    TEST_ERROR(ret < 0, "Error parsing commandline arguments", cleanup);

    ctx.in_file = new ifstream(ctx.in_file_path);
    TEST_ERROR(!ctx.in_file->is_open(), "Could not open input file", cleanup);

    ctx.out_file = new ofstream(ctx.out_file_path);
    TEST_ERROR(!ctx.out_file->is_open(), "Could not open output file", cleanup);

    ctx.jpegenc = NvJPEGEncoder::createJPEGEncoder("jpenenc");
    TEST_ERROR(!ctx.jpegenc, "Could not create Jpeg Encoder", cleanup);

    out_buf_size = ctx.in_width * ctx.in_height * 3 / 2;
    out_buf = new unsigned char[out_buf_size];

    if (ctx.perf)
    {
        iterator_num = PERF_LOOP;
        ctx.jpegenc->enableProfiling();
    }

    ctx.jpegenc->setCropRect(ctx.crop_left, ctx.crop_top,
            ctx.crop_width, ctx.crop_height);

    if(ctx.scaled_encode)
    {
      ctx.jpegenc->setScaledEncodeParams(ctx.scale_width, ctx.scale_height);
    }

    /**
     * Case 1:
     * Read YUV420 image from file system to CPU buffer, encode by
     * encodeFromBuffer() then write to file system.
     */
    if (!ctx.use_fd)
    {
        NvBuffer buffer(V4L2_PIX_FMT_YUV420M, ctx.in_width,
                ctx.in_height, 0);

        buffer.allocateMemory();

        ret = read_video_frame(ctx.in_file, buffer);
        TEST_ERROR(ret < 0, "Could not read a complete frame from file",
                cleanup);

        for (int i = 0; i < iterator_num; ++i)
        {
            ret = ctx.jpegenc->encodeFromBuffer(buffer, JCS_YCbCr, &out_buf,
                    out_buf_size, ctx.quality);
            TEST_ERROR(ret < 0, "Error while encoding from buffer", cleanup);
        }

        ctx.out_file->write((char *) out_buf, out_buf_size);

        goto cleanup;
    }

    /**
     * Case 2:
     * Read YUV420 image from file system to CPU buffer, convert to hardware
     * buffer memory (DMA buffer fd), encode by encodeFromFd() then write to
     * file system.
     * Note:
     *     While converting to hardware buffer, NvTransform may convert
     *     YUV420 to NV12 depends on ctx.in_pixfmt.
     */

    /**
     * Read YUV420 image to conv output plane buffer and enqueue so conv can
     * start processing.
     */
    NvBufSurf::NvCommonAllocateParams params;
    /* Create PitchLinear output buffer for transform. */
    params.memType = NVBUF_MEM_SURFACE_ARRAY;
    params.width = ctx.in_width;
    params.height = ctx.in_height;
    params.layout = NVBUF_LAYOUT_PITCH;
    params.colorFormat = NVBUF_COLOR_FORMAT_YUV420;

    params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;

    ret = NvBufSurf::NvAllocate(&params, 1, &src_dma_fd);
    TEST_ERROR(ret == -1, "create dmabuf failed", cleanup);

    /* Dumping two planes for NV12, NV16, NV24 and three for I420 */
    read_dmabuf(src_dma_fd, 0, ctx.in_file);
    read_dmabuf(src_dma_fd, 1, ctx.in_file);
    read_dmabuf(src_dma_fd, 2, ctx.in_file);

    /* Create PitchLinear output buffer for transform. */
    params.memType = NVBUF_MEM_SURFACE_ARRAY;
    params.width = ctx.in_width;
    params.height = ctx.in_height;
    params.layout = NVBUF_LAYOUT_BLOCK_LINEAR;
    if (ctx.in_pixfmt == V4L2_PIX_FMT_NV12M)
      params.colorFormat = NVBUF_COLOR_FORMAT_NV12;
    else
      params.colorFormat = NVBUF_COLOR_FORMAT_YUV420;
    params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;

    ret = NvBufSurf::NvAllocate(&params, 1, &dst_dma_fd);
    TEST_ERROR(ret == -1, "create dmabuf failed", cleanup);

    /* Clip & Stitch can be done by adjusting rectangle. */
    NvBufSurf::NvCommonTransformParams transform_params;
    transform_params.src_top = 0;
    transform_params.src_left = 0;
    transform_params.src_width = ctx.in_width;
    transform_params.src_height = ctx.in_height;
    transform_params.dst_top = 0;
    transform_params.dst_left = 0;
    transform_params.dst_width = ctx.in_width;
    transform_params.dst_height = ctx.in_height;
    transform_params.flag = NVBUFSURF_TRANSFORM_FILTER;
    transform_params.flip = NvBufSurfTransform_None;
    transform_params.filter = NvBufSurfTransformInter_Nearest;
    ret = NvBufSurf::NvTransform(&transform_params, src_dma_fd, dst_dma_fd);
    TEST_ERROR(ret == -1, "Transform failed", cleanup);

    for (int i = 0; i < iterator_num; ++i)
    {
        ret = ctx.jpegenc->encodeFromFd(dst_dma_fd, JCS_YCbCr, &out_buf,
              out_buf_size, ctx.quality);
        if (ret < 0)
        {
            cerr << "Error while encoding from fd" << endl;
            ctx.got_error = true;
            break;
        }
    }
    if (ret >= 0)
    {
        ctx.out_file->write((char *) out_buf, out_buf_size);
    }


cleanup:
    if (ctx.perf)
    {
        ctx.jpegenc->printProfilingStats(cout);
    }

    delete[] out_buf;
    delete ctx.in_file;
    delete ctx.out_file;
    /**
     * Destructors do all the cleanup, unmapping and deallocating buffers
     * and calling v4l2_close on fd
     */
    delete ctx.jpegenc;

    free(ctx.in_file_path);
    free(ctx.out_file_path);

    return -error;
}

int
main(int argc, char *argv[])
{
    context_t ctx;
    int ret = 0;
    /* save iterator number */
    int iterator_num = 0;

    do
    {
        ret = jpeg_encode_proc(ctx, argc, argv);
        iterator_num++;
    } while((ctx.stress_test != iterator_num) && ret == 0);

    if (ret)
    {
        cout << "App run failed" << endl;
    }
    else
    {
        cout << "App run was successful" << endl;
    }
    return ret;
}
