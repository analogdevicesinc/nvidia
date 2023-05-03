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

#include "jpeg_decode.h"

#define TEST_ERROR(cond, str, label) if(cond) { \
                                        cerr << str << endl; \
                                        error = 1; \
                                        goto label; }

#define PERF_LOOP   300

using namespace std;

static uint64_t
get_file_size(ifstream * stream)
{
    uint64_t size = 0;
    streampos current_pos = stream->tellg();
    stream->seekg(0, stream->end);
    size = stream->tellg();
    stream->seekg(current_pos, stream->beg);
    return size;
}

static void
set_defaults(context_t * ctx)
{
    memset(ctx, 0, sizeof(context_t));
    ctx->perf = false;
    ctx->use_fd = true;
    ctx->stress_test = 1;
    ctx->current_file = 0;
}

/**
 * Class NvJPEGDecoder decodes JPEG image to YUV.
 * NvJPEGDecoder::decodeToBuffer() decodes to software buffer memory
 * which can be access by CPU directly.
 * NvJPEGDecoder::decodeToFd() decodes to hardware buffer memory which is faster
 * than NvJPEGDecoder::decodeToBuffer() since the latter involves conversion
 * from hardware buffer memory to software buffer memory.
 *
 * When using NvJPEGDecoder::decodeToFd(), NvUtils is used to
 * convert NvJPEGDecoder output YUV hardware buffer memory (DMA buffer fd) to
 * MMAP buffer so CPU can access it to write it to file.
 */
static int
jpeg_decode_proc(context_t& ctx, int argc, char *argv[])
{
    int ret = 0;
    int error = 0;
    int fd = 0;
    uint32_t width, height, pixfmt;
    int i = 0;
    int iterator_num = 1;
    int dst_dma_fd = -1;
    int out_pixfmt = 2;

    set_defaults(&ctx);

    ret = parse_csv_args(&ctx, argc, argv);
    TEST_ERROR(ret < 0, "Error parsing commandline arguments", cleanup);

    for(i = 0; i < ctx.num_files; i++)
    {
      ctx.in_file[i] = new ifstream(ctx.in_file_path[i]);
      TEST_ERROR(!ctx.in_file[i]->is_open(), "Could not open input file", cleanup);

      ctx.out_file[i] = new ofstream(ctx.out_file_path[i]);
      TEST_ERROR(!ctx.out_file[i]->is_open(), "Could not open output file", cleanup);
    }

    ctx.jpegdec = NvJPEGDecoder::createJPEGDecoder("jpegdec");
    TEST_ERROR(!ctx.jpegdec, "Could not create Jpeg Decoder", cleanup);

    if (ctx.perf)
    {
      iterator_num = PERF_LOOP;
      ctx.jpegdec->enableProfiling();
    }

    for(i = 0; i < ctx.num_files; i++)
    {
      ctx.in_file_size = get_file_size(ctx.in_file[i]);
      ctx.in_buffer = new unsigned char[ctx.in_file_size];
      ctx.in_file[i]->read((char *) ctx.in_buffer, ctx.in_file_size);

      /**
       * Case 1:
       * Decode to software buffer memory by decodeToBuffer() and write to
       * local file.
       */
      if (!ctx.use_fd)
      {
        NvBuffer *buffer;

        for (int i = 0; i < iterator_num; ++i)
        {
          ret = ctx.jpegdec->decodeToBuffer(&buffer, ctx.in_buffer,
                ctx.in_file_size, &pixfmt, &width, &height);
          TEST_ERROR(ret < 0, "Could not decode image", cleanup);
        }

        cout << "Image Resolution - " << width << " x " << height << endl;
        write_video_frame(ctx.out_file[i], *buffer);
        delete buffer;
        goto cleanup;
      }

      /**
       * Case 2:
       * Decode to hardware buffer memory by decodeToFd(), convert to
       * other format then write to local file.
       */
      for (int i = 0; i < iterator_num; ++i)
      {
        ret = ctx.jpegdec->decodeToFd(fd, ctx.in_buffer, ctx.in_file_size, pixfmt,
            width, height);
        TEST_ERROR(ret < 0, "Could not decode image", cleanup);
      }

      cout << "Image Resolution - " << width << " x " << height << endl;

      NvBufSurf::NvCommonAllocateParams params;
      /* Create PitchLinear output buffer for transform. */
      params.memType = NVBUF_MEM_SURFACE_ARRAY;
      params.width = width;
      params.height = height;
      params.layout = NVBUF_LAYOUT_PITCH;
      if (out_pixfmt == 1)
        params.colorFormat = NVBUF_COLOR_FORMAT_NV12;
      else if (out_pixfmt == 2)
        params.colorFormat = NVBUF_COLOR_FORMAT_YUV420;
      else if (out_pixfmt == 3)
        params.colorFormat = NVBUF_COLOR_FORMAT_NV16;
      else if (out_pixfmt == 4)
        params.colorFormat = NVBUF_COLOR_FORMAT_NV24;

      params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;

      ret = NvBufSurf::NvAllocate(&params, 1, &dst_dma_fd);
      TEST_ERROR(ret == -1, "create dmabuf failed", cleanup);

      /* Clip & Stitch can be done by adjusting rectangle. */
      NvBufSurf::NvCommonTransformParams transform_params;
      transform_params.src_top = 0;
      transform_params.src_left = 0;
      transform_params.src_width = width;
      transform_params.src_height = height;
      transform_params.dst_top = 0;
      transform_params.dst_left = 0;
      transform_params.dst_width = width;
      transform_params.dst_height = height;
      transform_params.flag = NVBUFSURF_TRANSFORM_FILTER;
      transform_params.flip = NvBufSurfTransform_None;
      transform_params.filter = NvBufSurfTransformInter_Nearest;
      ret = NvBufSurf::NvTransform(&transform_params, fd, dst_dma_fd);
      TEST_ERROR(ret == -1, "Transform failed", cleanup);

      /* Write raw video frame to file. */
      if (ctx.out_file)
      {
          int index = ctx.current_file++;
          /* Dumping two planes for NV12, NV16, NV24 and three for I420 */
          dump_dmabuf(dst_dma_fd, 0, ctx.out_file[index]);
          dump_dmabuf(dst_dma_fd, 1, ctx.out_file[index]);
          if (out_pixfmt == 2)
          {
              dump_dmabuf(dst_dma_fd, 2, ctx.out_file[index]);
          }
      }

cleanup:
      if (ctx.perf)
      {
        ctx.jpegdec->printProfilingStats(cout);
      }

      if(dst_dma_fd != -1)
      {
          ret = NvBufSurf::NvDestroy(dst_dma_fd);
          dst_dma_fd = -1;
      }

      delete[] ctx.in_buffer;
    }

    for(i = 0; i < ctx.num_files; i++)
    {

      delete ctx.in_file[i];
      delete ctx.out_file[i];

      free(ctx.in_file_path[i]);
      free(ctx.out_file_path[i]);
    }
    /**
     * Destructors do all the cleanup, unmapping and deallocating buffers
     * and calling v4l2_close on fd
     */
    delete ctx.jpegdec;

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
        ret = jpeg_decode_proc(ctx, argc, argv);
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
