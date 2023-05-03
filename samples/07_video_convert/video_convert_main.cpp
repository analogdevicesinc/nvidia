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

#include <fstream>
#include <iostream>
#include <vector>
#include <cassert>
#include <string.h>
#include <sys/time.h>

#include "NvUtils.h"
#include "video_convert.h"
#include "NvBufSurface.h"

using namespace std;

#define PERF_LOOP   3000

struct thread_context
{
    ifstream *in_file;
    ofstream *out_file;
    int in_dmabuf_fd;
    int out_dmabuf_fd;
    NvBufSurf::NvCommonAllocateParams input_params;
    NvBufSurf::NvCommonAllocateParams output_params;
    NvBufSurf::NvCommonTransformParams transform_params;
    vector<int> src_fmt_bytes_per_pixel;
    vector<int> dest_fmt_bytes_per_pixel;
    NvBufSurfTransformSyncObj_t syncobj;
    bool perf;
    bool async;
    bool create_session;
};

/**
 * This function returns vector contians bytes per pixel info
 * of each plane in sequence.
**/
static int
fill_bytes_per_pixel(NvBufSurfaceColorFormat pixel_format, vector<int> *bytes_per_pixel_fmt)
{
    switch (pixel_format)
    {
        case NVBUF_COLOR_FORMAT_NV12:
        case NVBUF_COLOR_FORMAT_NV12_ER:
        case NVBUF_COLOR_FORMAT_NV21:
        case NVBUF_COLOR_FORMAT_NV21_ER:
        case NVBUF_COLOR_FORMAT_NV12_709:
        case NVBUF_COLOR_FORMAT_NV12_709_ER:
        case NVBUF_COLOR_FORMAT_NV12_2020:
        case NVBUF_COLOR_FORMAT_NV16:
        case NVBUF_COLOR_FORMAT_NV24:
        case NVBUF_COLOR_FORMAT_NV16_ER:
        case NVBUF_COLOR_FORMAT_NV24_ER:
        case NVBUF_COLOR_FORMAT_NV16_709:
        case NVBUF_COLOR_FORMAT_NV24_709:
        case NVBUF_COLOR_FORMAT_NV16_709_ER:
        case NVBUF_COLOR_FORMAT_NV24_709_ER:
        {
            bytes_per_pixel_fmt->push_back(1);
            bytes_per_pixel_fmt->push_back(2);
            break;
        }
        case NVBUF_COLOR_FORMAT_NV12_10LE:
        case NVBUF_COLOR_FORMAT_NV12_10LE_709:
        case NVBUF_COLOR_FORMAT_NV12_10LE_709_ER:
        case NVBUF_COLOR_FORMAT_NV12_10LE_2020:
        case NVBUF_COLOR_FORMAT_NV21_10LE:
        case NVBUF_COLOR_FORMAT_NV12_12LE:
        case NVBUF_COLOR_FORMAT_NV12_12LE_2020:
        case NVBUF_COLOR_FORMAT_NV21_12LE:
        case NVBUF_COLOR_FORMAT_NV16_10LE:
        case NVBUF_COLOR_FORMAT_NV24_10LE_709:
        case NVBUF_COLOR_FORMAT_NV24_10LE_709_ER:
        case NVBUF_COLOR_FORMAT_NV24_10LE_2020:
        case NVBUF_COLOR_FORMAT_NV24_12LE_2020:
        {
            bytes_per_pixel_fmt->push_back(2);
            bytes_per_pixel_fmt->push_back(4);
            break;
        }
        case NVBUF_COLOR_FORMAT_ABGR:
        //case NVBUF_COLOR_FORMAT_XRGB:
        case NVBUF_COLOR_FORMAT_ARGB:
        {
            bytes_per_pixel_fmt->push_back(4);
            break;
        }
        case NVBUF_COLOR_FORMAT_YUV420:
        case NVBUF_COLOR_FORMAT_YUV420_ER:
        case NVBUF_COLOR_FORMAT_YUV420_709:
        case NVBUF_COLOR_FORMAT_YUV420_709_ER:
        case NVBUF_COLOR_FORMAT_YUV420_2020:
        case NVBUF_COLOR_FORMAT_YUV444:
        {
            bytes_per_pixel_fmt->push_back(1);
            bytes_per_pixel_fmt->push_back(1);
            bytes_per_pixel_fmt->push_back(1);
            break;
        }
        case NVBUF_COLOR_FORMAT_UYVY:
        case NVBUF_COLOR_FORMAT_UYVY_ER:
        case NVBUF_COLOR_FORMAT_VYUY:
        case NVBUF_COLOR_FORMAT_VYUY_ER:
        case NVBUF_COLOR_FORMAT_YUYV:
        case NVBUF_COLOR_FORMAT_YUYV_ER:
        case NVBUF_COLOR_FORMAT_YVYU:
        case NVBUF_COLOR_FORMAT_YVYU_ER:
        {
            bytes_per_pixel_fmt->push_back(2);
            break;
        }
        case NVBUF_COLOR_FORMAT_GRAY8:
        {
            bytes_per_pixel_fmt->push_back(1);
            break;
        }
        default:
            return -1;
    }
    return 0;
}

/**
 * This function reads the video frame from the input file stream
 * and writes to the source HW buffer exported as FD.
 * Using the FD, HW buffer parameters are filled by calling
 * NvBufferGetParams. The parameters recieved from the buffer are
 * then used to write the raw stream in planar form into the buffer.
 *
 * For writing in the HW buffer:
 * A void data-pointer in created which stores the memory-mapped
 * virtual addresses of the planes.
 * For each plane, NvBufferMemMap is called which gets the
 * memory-mapped virtual address of the plane with the access
 * pointed by the flag in the void data-pointer.
 * Before the mapped memory is accessed, a call to NvBufferMemSyncForDevice()
 * with the virtual address must be present, before any modification
 * from CPU to the buffer is performed.
 * After writing the data, the memory-mapped virtual address of the
 * plane is unmapped.
**/
static int
read_video_frame(int src_dma_fd, ifstream * input_stream, const vector<int> &bytes_per_pixel_fmt)
{
    int ret;
    unsigned int plane;

    for (plane = 0; plane < bytes_per_pixel_fmt.size(); plane ++)
    {
        ret = read_dmabuf(src_dma_fd, plane, input_stream);
        if (ret < 0)
            return -1;
    }

    return 0;
}

/**
 * This function writes the video frame from the HW buffer
 * exported as FD into the destination file.
 * Using the FD, HW buffer parameters are filled by calling
 * NvBufferGetParams. The parameters recieved from the buffer are
 * then used to read the planar stream from the HW buffer into the
 * output filestream.
 *
 * For reading from the HW buffer:
 * A void data-pointer in created which stores the memory-mapped
 * virtual addresses of the planes.
 * For each plane, NvBufferMemMap is called which gets the
 * memory-mapped virtual address of the plane with the access
 * pointed by the flag in the void data-pointer.
 * Before the mapped memory is accessed, a call to NvBufferMemSyncForCpu()
 * with the virtual address must be present, before any access is made
 * by the CPU to the buffer.
 *
 * After reading the data, the memory-mapped virtual address of the
 * plane is unmapped.
**/
static int
write_video_frame(int dst_dma_fd, ofstream * output_stream, const vector<int> &bytes_per_pixel_fmt)
{
    unsigned int plane;

    for (plane = 0; plane < bytes_per_pixel_fmt.size(); plane ++)
        dump_dmabuf(dst_dma_fd, plane, output_stream);

    return 0;
}

static int
create_thread_context(context_t *ctx, struct thread_context *tctx, int index)
{
    int ret = 0;
    string out_file_path(ctx->out_file_path);

    tctx->in_file = new ifstream(ctx->in_file_path);
    if (!tctx->in_file->is_open())
    {
        cerr << "Could not open input file" << endl;
        goto out;
    }
    tctx->out_file = new ofstream(out_file_path + to_string(index));
    if (!tctx->out_file->is_open())
    {
        cerr << "Could not open output file" << endl;
        goto out;
    }

    /* Define the parameter for the HW Buffer.
    ** @payloadType: Define the memory handle for the NvBuffer,
    **               here defined for the set of planese.
    ** @nvbuf_tag: Identifie the type of device or compoenet
    **             requesting the operation.
    ** @layout: Defines memory layout for the surfaces, either
    **          NvBufferLayout_Pitch or NvBufferLayout_BlockLinear.
    ** (Note: This sample needs to read data from file and
    **        dump converted buffer to file, so input and output
    **        layout are both NvBufferLayout_Pitch.
    */
    tctx->input_params.width = ctx->in_width;
    tctx->input_params.height = ctx->in_height;
    tctx->input_params.layout = NVBUF_LAYOUT_PITCH;
    tctx->input_params.memType = NVBUF_MEM_SURFACE_ARRAY;
    tctx->input_params.colorFormat = ctx->in_pixfmt;
    tctx->input_params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;

    tctx->output_params.width = ctx->out_width;
    tctx->output_params.height = ctx->out_height;
    tctx->output_params.layout = NVBUF_LAYOUT_PITCH;
    tctx->output_params.memType = NVBUF_MEM_SURFACE_ARRAY;
    tctx->output_params.colorFormat = ctx->out_pixfmt;
    tctx->output_params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;

    /* Create the HW Buffer. It is exported as
    ** an FD by the hardware.
    */
    tctx->in_dmabuf_fd = -1;
    ret = NvBufSurf::NvAllocate(&tctx->input_params, 1, &tctx->in_dmabuf_fd);
    if (ret)
    {
        cerr << "Error in creating the input buffer." << endl;
        goto out;
    }

    tctx->out_dmabuf_fd = -1;
    ret = NvBufSurf::NvAllocate(&tctx->output_params, 1, &tctx->out_dmabuf_fd);
    if (ret)
    {
        cerr << "Error in creating the output buffer." << endl;
        goto out;
    }

    /* Store th bpp required for each color
    ** format to read/write properly to raw
    ** buffers.
    */
    ret = fill_bytes_per_pixel(ctx->in_pixfmt, &tctx->src_fmt_bytes_per_pixel);
    if (ret)
    {
        cerr << "Error figure out bytes per pixel for source format." << endl;
        goto out;
    }
    ret = fill_bytes_per_pixel(ctx->out_pixfmt, &tctx->dest_fmt_bytes_per_pixel);
    if (ret)
    {
        cerr << "Error figure out bytes per pixel for destination format." << endl;
        goto out;
    }

    /* @transform_flag defines the flags for
    ** enabling the valid transforms.
    ** All the valid parameters are present in
    ** the nvbuf_utils header.
    */
    memset(&tctx->transform_params, 0, sizeof(tctx->transform_params));
    tctx->transform_params.src_top = 0;
    tctx->transform_params.src_left = 0;
    tctx->transform_params.src_width = ctx->in_width;
    tctx->transform_params.src_height = ctx->in_height;
    tctx->transform_params.dst_top = 0;
    tctx->transform_params.dst_left = 0;
    tctx->transform_params.dst_width = ctx->out_width;
    tctx->transform_params.dst_height = ctx->out_height;
    tctx->transform_params.flag = (NvBufSurfTransform_Transform_Flag)(NVBUFSURF_TRANSFORM_FILTER | NVBUFSURF_TRANSFORM_FLIP);
    if (ctx->crop_rect.width != 0 && ctx->crop_rect.height != 0)
    {
        tctx->transform_params.flag = (NvBufSurfTransform_Transform_Flag)(tctx->transform_params.flag | NVBUFSURF_TRANSFORM_CROP_SRC);
        tctx->transform_params.src_top = ctx->crop_rect.top;
        tctx->transform_params.src_left = ctx->crop_rect.left;
        tctx->transform_params.src_width = ctx->crop_rect.width;
        tctx->transform_params.src_height = ctx->crop_rect.height;
    }
    tctx->transform_params.flip = ctx->flip_method;
    tctx->transform_params.filter = ctx->interpolation_method;

    tctx->perf = ctx->perf;
    tctx->async = ctx->async;
    tctx->create_session = ctx->create_session;

out:
    return ret;
}

static void
destory_thread_context(context_t *ctx, struct thread_context *tctx)
{
    if (tctx->in_file && tctx->in_file->is_open())
    {
        delete tctx->in_file;
    }
    if (tctx->out_file && tctx->out_file->is_open())
    {
        delete tctx->out_file;
    }

    /* HW allocated buffers must be destroyed
    ** at the end of execution.
    */
    if (tctx->in_dmabuf_fd != -1)
    {
        NvBufSurf::NvDestroy(tctx->in_dmabuf_fd);
    }

    if (tctx->out_dmabuf_fd != -1)
    {
        NvBufSurf::NvDestroy(tctx->out_dmabuf_fd);
    }
}

static void *
do_video_convert(void *arg)
{
    struct thread_context *tctx = (struct thread_context *)arg;
    int ret = 0;
    int count = tctx->perf ? PERF_LOOP : 1;

    NvBufSurfTransformConfigParams config_params;
    memset(&config_params,0,sizeof(NvBufSurfTransformConfigParams));

    if (tctx->create_session)
    {
        NvBufSurfTransformSetSessionParams (&config_params);
    }

    /* The main loop for reading the data from
    ** file into the HW source buffer, calling
    ** the transform and writing the output
    ** bytestream back to the destination file.
    */
    while (true)
    {
        ret = read_video_frame(tctx->in_dmabuf_fd, tctx->in_file, tctx->src_fmt_bytes_per_pixel);
        if (ret < 0)
        {
            cout << "File read complete." << endl;
            break;
        }
        if (!tctx->async)
        {
            for (int i = 0; i < count; ++i)
            {
                ret = NvBufSurf::NvTransform(&tctx->transform_params, tctx->in_dmabuf_fd, tctx->out_dmabuf_fd);
                if (ret)
                {
                    cerr << "Error in transformation." << endl;
                    goto out;
                }
            }
        }
        else
        {
            for (int i = 0; i < count; ++i)
            {
                ret = NvBufSurf::NvTransformAsync(&tctx->transform_params, &tctx->syncobj, tctx->in_dmabuf_fd, tctx->out_dmabuf_fd);
                if (ret)
                {
                    cerr << "Error in asynchronous transformation." << endl;
                    goto out;
                }

                ret =  NvBufSurfTransformSyncObjWait(tctx->syncobj, -1);
                if (ret)
                {
                    cerr << "Error in sync object wait." << endl;
                    goto out;
                }
                NvBufSurfTransformSyncObjDestroy (&tctx->syncobj);
            }
        }
        ret = write_video_frame(tctx->out_dmabuf_fd, tctx->out_file, tctx->dest_fmt_bytes_per_pixel);
        if (ret)
        {
            cerr << "Error in dumping the output raw buffer." << endl;
            break;
        }
    }

out:
    return nullptr;
}

static void
set_defaults(context_t * ctx)
{
    memset(ctx, 0, sizeof(context_t));

    ctx->num_thread = 1;
    ctx->async = false;
    ctx->create_session = false;
    ctx->perf = false;
    ctx->flip_method = NvBufSurfTransform_None;
    ctx->interpolation_method = NvBufSurfTransformInter_Nearest;
}

int
main(int argc, char *argv[])
{
    context_t ctx;
    struct thread_context tctx;
    int ret = 0;
    pthread_t *tids = nullptr;
    struct thread_context *thread_ctxs = nullptr;
    struct timeval start_time;
    struct timeval stop_time;

    set_defaults(&ctx);

    ret = parse_csv_args(&ctx, argc, argv);
    if (ret < 0)
    {
        cerr << "Error parsing commandline arguments" << endl;
        goto cleanup;
    }

    tids = new pthread_t[ctx.num_thread];
    thread_ctxs = new struct thread_context[ctx.num_thread];

    for (uint32_t i = 0; i < ctx.num_thread; ++i)
    {
        ret = create_thread_context(&ctx, &thread_ctxs[i], i);
        if (ret)
        {
            cerr << "Error when init thread context " << i << endl;
            goto cleanup;
        }
    }

    if (ctx.perf)
    {
        gettimeofday(&start_time, nullptr);
    }

    for (uint32_t i = 0; i < ctx.num_thread; ++i)
    {
        pthread_create(&tids[i], nullptr, do_video_convert, &thread_ctxs[i]);
    }

    pthread_yield();

    for (uint32_t i = 0; i < ctx.num_thread; ++i)
    {
        pthread_join(tids[i], nullptr);
    }

    if (ctx.perf)
    {
        unsigned long total_time_us = 0;

        gettimeofday(&stop_time, nullptr);
        total_time_us = (stop_time.tv_sec - start_time.tv_sec) * 1000000 +
                   (stop_time.tv_usec - start_time.tv_usec);

        cout << endl;
        cout << "Total conversion takes " << total_time_us << " us, average "
             << total_time_us / PERF_LOOP / ctx.num_thread << " us per conversion" << endl;
        cout << endl;
    }

cleanup:

    for (uint32_t i = 0; i < ctx.num_thread; ++i)
    {
        destory_thread_context(&ctx, &thread_ctxs[i]);
    }

    free(ctx.in_file_path);
    free(ctx.out_file_path);

    delete []tids;
    delete []thread_ctxs;

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
