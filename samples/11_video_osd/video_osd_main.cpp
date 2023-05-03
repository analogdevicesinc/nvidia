/*
 * Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
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
#include <pthread.h>

#include "NvUtils.h"
#include "NvBufSurface.h"
#include "video_osd.h"

using namespace std;

#define PERF_LOOP   3000
#define MAX_RECT_NUM 100
#define BORDER_WIDTH 2
#define ELEMENT_NUM 100

struct thread_context
{
    ifstream *in_file;
    ofstream *out_file;
    int in_dmabuf_fd;
    int process_dmabuf_fd;
    NvBufSurf::NvCommonAllocateParams input_params;
    NvBufSurf::NvCommonAllocateParams process_params;
    NvBufSurf::NvCommonTransformParams transform_params;
    vector<int> src_fmt_bytes_per_pixel;
    bool perf;

    void *nvosd_context;
    bool osd_perf;
    NvOSD_Mode osd_mode;
    bool draw_rect;
    bool draw_text;
    bool draw_arrow;
    bool draw_circle;
    bool draw_line;
    bool show_clock;
    NvOSD_TextParams clock_text_params;
    NvOSD_TextParams text_params[MAX_RECT_NUM];
    NvOSD_RectParams rect_params[MAX_RECT_NUM];
    NvOSD_ArrowParams arrow_params[MAX_RECT_NUM];
    NvOSD_CircleParams circle_params[MAX_RECT_NUM];
    NvOSD_LineParams line_params[MAX_RECT_NUM];
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
        case NVBUF_COLOR_FORMAT_NV12_709:
        case NVBUF_COLOR_FORMAT_NV12_709_ER:
        case NVBUF_COLOR_FORMAT_NV12_2020:
        {
            bytes_per_pixel_fmt->push_back(1);
            bytes_per_pixel_fmt->push_back(2);
            break;
        }
        case NVBUF_COLOR_FORMAT_RGBA:
        {
            bytes_per_pixel_fmt->push_back(4);
            break;
        }
        default:
            return -1;
    }
    return 0;
}

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

static int
write_video_frame(int dst_dma_fd, ofstream * output_stream, const vector<int> &bytes_per_pixel_fmt)
{
    unsigned int plane;

    for (plane = 0; plane < bytes_per_pixel_fmt.size(); plane ++)
    {
        dump_dmabuf(dst_dma_fd, plane, output_stream);
    }

    return 0;
}

static void
osd_perf_elements (struct thread_context *tctx)
{
    tctx->draw_rect = 1;
    tctx->draw_text = 1;
    tctx->show_clock = 1;
    tctx->draw_arrow = 1;
    tctx->draw_circle = 1;
    tctx->draw_line = 1;

    if (tctx->draw_rect)
    {
        for (uint32_t i = 0; i < ELEMENT_NUM; i ++)
        {
            tctx->rect_params[i].left =  i * 12;
            tctx->rect_params[i].top =  i * 6;
            tctx->rect_params[i].width = 100;
            tctx->rect_params[i].height = 300;
            tctx->rect_params[i].border_width = BORDER_WIDTH;
            tctx->rect_params[i].border_color.red = 1.0f;
            tctx->rect_params[i].border_color.green = 0.0;
            tctx->rect_params[i].border_color.blue = 0.0;
            tctx->rect_params[i].border_color.alpha = 1.0;
        }
    }

    if (tctx->draw_text)
    {
        for (uint32_t i = 0; i < ELEMENT_NUM; i ++)
        {
            tctx->text_params[i].display_text = strdup("nvosd video test sample!");
            if (i < 30)
            {
                tctx->text_params[i].x_offset = 30;
                tctx->text_params[i].y_offset = 60 + i * 30;
            }
            else if (i < 60)
            {
                tctx->text_params[i].x_offset = 600;
                tctx->text_params[i].y_offset = 60 + (i - 30) * 30;
            }
            else
            {
                tctx->text_params[i].x_offset = 1200;
                tctx->text_params[i].y_offset = 60 + (i - 60) * 20;
            }
            tctx->text_params[i].font_params.font_name = strdup("Arial");
            tctx->text_params[i].font_params.font_size = 18;
            tctx->text_params[i].font_params.font_color.red = 1.0;
            tctx->text_params[i].font_params.font_color.green = 0.0;
            tctx->text_params[i].font_params.font_color.blue = 1.0;
            tctx->text_params[i].font_params.font_color.alpha = 1.0;
            tctx->text_params[i].set_bg_clr = 1;
            tctx->text_params[i].text_bg_clr = (NvOSD_ColorParams) {
                0.0, 0.0, 0.0, 1.0};
        }
    }

    if (tctx->draw_arrow)
    {
        for (uint32_t i = 0; i < ELEMENT_NUM; i ++)
        {
            NvOSD_ArrowParams *arrow_params = &tctx->arrow_params[i];
            arrow_params[0].x1 = 500;
            arrow_params[0].y1 = 100 + i * 6;
            arrow_params[0].x2 = arrow_params[0].x1 + 500;
            arrow_params[0].y2 = arrow_params[0].y1 + 100;
            arrow_params[0].arrow_width = 4;
            arrow_params[0].start_arrow_head = 1;

            arrow_params[0].arrow_color.red = 0;
            arrow_params[0].arrow_color.blue = 0.8;
            arrow_params[0].arrow_color.green = 1;
            arrow_params[0].arrow_color.alpha = 1.0;
        }
    }

    if (tctx->draw_circle)
    {
        for (uint32_t i = 0; i < ELEMENT_NUM; i ++)
        {
            NvOSD_CircleParams *circle_params = &tctx->circle_params[i];
            circle_params[0].xc = 500;
            circle_params[0].yc = 100 + 6 * i;
            circle_params[0].radius = 100;

            circle_params[0].circle_color.red = 0;
            circle_params[0].circle_color.blue = 0.8;
            circle_params[0].circle_color.green = 1;
            circle_params[0].circle_color.alpha = 1.0;
        }
    }

    if (tctx->draw_line)
    {
        for (uint32_t i = 0; i < ELEMENT_NUM; i ++)
        {
            NvOSD_LineParams *line_params = &tctx->line_params[i];
            line_params[0].x1 = 800;
            line_params[0].y1 = 100 + 6 * i;
            line_params[0].x2 = line_params[0].x1 - 500;
            line_params[0].y2 = line_params[0].y1 - 100;
            line_params[0].line_width = 4;

            line_params[0].line_color.red = 0;
            line_params[0].line_color.blue = 0.8;
            line_params[0].line_color.green = 1;
            line_params[0].line_color.alpha = 1;
        }
    }

    if (tctx->show_clock)
    {
        /* enable clock */
        tctx->clock_text_params.font_params.font_color.red = 1.0;
        tctx->clock_text_params.font_params.font_color.green = 1.0;
        tctx->clock_text_params.font_params.font_color.blue = 1.0;
        tctx->clock_text_params.font_params.font_color.alpha = 1.0;

        tctx->clock_text_params.x_offset = 400;
        tctx->clock_text_params.y_offset = 300;

        tctx->clock_text_params.font_params.font_name = strdup("Arial");
        tctx->clock_text_params.font_params.font_size = 40;
        nvosd_set_clock_params(tctx->nvosd_context, &tctx->clock_text_params);
    }

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

    tctx->process_params.width = ctx->in_width;
    tctx->process_params.height = ctx->in_height;
    if (ctx->is_bl)
    {
        tctx->process_params.layout = NVBUF_LAYOUT_BLOCK_LINEAR;
    }
    else
    {
        tctx->process_params.layout = NVBUF_LAYOUT_PITCH;
    }
    tctx->process_params.memType = NVBUF_MEM_SURFACE_ARRAY;
    tctx->process_params.colorFormat = ctx->process_pixfmt;
    tctx->process_params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;

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

    tctx->process_dmabuf_fd = -1;
    ret = NvBufSurf::NvAllocate(&tctx->process_params, 1, &tctx->process_dmabuf_fd);
    if (ret)
    {
        cerr << "Error in creating the process buffer." << endl;
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
    tctx->transform_params.dst_width = ctx->in_width;
    tctx->transform_params.dst_height = ctx->in_height;
    tctx->transform_params.flag = (NvBufSurfTransform_Transform_Flag)(NVBUFSURF_TRANSFORM_FILTER | NVBUFSURF_TRANSFORM_FLIP);

    tctx->perf = ctx->perf;
    tctx->osd_mode = ctx->osd_mode;

    tctx->nvosd_context = nvosd_create_context();
    if (!tctx->nvosd_context)
    {
        cerr << "Error in create nvosd context." << endl;
        ret = -1;
        goto out;
    }
    osd_perf_elements (tctx);

out:
    return ret;
}

static int
prepare_process_video(struct thread_context *tctx)
{
    int ret = 0;

    ret = read_video_frame(tctx->in_dmabuf_fd, tctx->in_file, tctx->src_fmt_bytes_per_pixel);
    if (ret < 0)
    {
        ret = -1;
        goto out;
    }

    ret = NvBufSurf::NvTransform(&tctx->transform_params, tctx->in_dmabuf_fd, tctx->process_dmabuf_fd);
    if (ret)
    {
        ret = -1;
        goto out;
    }

out:
    return ret;
}


static int
output_processed_video(struct thread_context *tctx)
{
    int ret = 0;

    if (tctx->out_file && tctx->out_file->is_open()
            && tctx->in_dmabuf_fd != -1
            && tctx->process_dmabuf_fd != -1)
    {
        ret = NvBufSurf::NvTransform(&tctx->transform_params,
                tctx->process_dmabuf_fd, tctx->in_dmabuf_fd);
        if (ret)
        {
            cerr << "Error in transformation." << endl;
            goto out;
        }

        ret = write_video_frame(tctx->in_dmabuf_fd, tctx->out_file,
                tctx->src_fmt_bytes_per_pixel);
        if (ret)
        {
            cerr << "Error in dumping the output raw buffer." << endl;
            goto out;
        }
    }
out:
    return ret;
}

static void
destory_thread_context(context_t *ctx, struct thread_context *tctx)
{
    if (tctx->nvosd_context)
    {
        nvosd_destroy_context(tctx->nvosd_context);
        tctx->nvosd_context = NULL;
    }

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
    if (tctx->process_dmabuf_fd != -1)
    {
        NvBufSurf::NvDestroy(tctx->process_dmabuf_fd);
    }
}

static void *
do_video_osd(void *arg)
{
    struct thread_context *tctx = (struct thread_context *)arg;
    NvBufSurface *nvbuf_surf = 0;
    int ret = 0;
    int count = tctx->perf ? PERF_LOOP : 1;

    ret = NvBufSurfaceFromFd(tctx->process_dmabuf_fd, (void**)(&nvbuf_surf));
    if (ret != 0)
    {
        cerr << "Unable to extract NvBufSurfaceFromFd" << endl;
        goto out;
    }

    for (int i = 0; i < count; ++i)
    {
        if (tctx->draw_rect) {
            ret = nvosd_draw_rectangles(tctx->nvosd_context,
                    tctx->osd_mode,
                    tctx->process_dmabuf_fd,
                    ELEMENT_NUM,
                    tctx->rect_params);
            if (ret)
            {
                cerr << "Error in draw rectangles." << endl;
                goto out;
            }
        }

        if (tctx->draw_arrow)
        {
            ret = nvosd_draw_arrows(tctx->nvosd_context,
                    tctx->osd_mode, tctx->process_dmabuf_fd,
                    ELEMENT_NUM, tctx->arrow_params);
            if (ret)
            {
                cerr << "Error in draw arrows." << endl;
                goto out;
            }
        }

        if (tctx->osd_mode != MODE_HW)
        {
            if (tctx->draw_circle)
            {
                ret = nvosd_draw_circles(tctx->nvosd_context,
                        tctx->osd_mode, tctx->process_dmabuf_fd,
                        ELEMENT_NUM, tctx->circle_params);
                if (ret)
                {
                    cerr << "Error in draw circles." << endl;
                    goto out;
                }
            }

            if (tctx->draw_line)
            {
                ret = nvosd_draw_lines(tctx->nvosd_context,
                        tctx->osd_mode, tctx->process_dmabuf_fd,
                        ELEMENT_NUM, tctx->line_params);
                if (ret)
                {
                    cerr << "Error in draw lines." << endl;
                    goto out;
                }
            }
        }

        if (tctx->draw_text)
        {
            ret = nvosd_put_text(tctx->nvosd_context,
                    tctx->osd_mode,
                    tctx->process_dmabuf_fd,
                    ELEMENT_NUM,
                    tctx->text_params);
            if (ret)
            {
                cerr << "Error in text." << endl;
                goto out;
            }
        }

        if (tctx->osd_mode == MODE_GPU)
        {
            ret = nvosd_gpu_apply(tctx->nvosd_context, tctx->process_dmabuf_fd);
            if (ret)
            {
                cerr << "Error in draw shapes with GPU." << endl;
                goto out;
            }
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
    ctx->perf = false;
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
        exit(EXIT_SUCCESS);
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

    for (uint32_t i = 0; i < ctx.num_thread; ++i)
    {
        ret = prepare_process_video(&thread_ctxs[i]);
        if (ret)
        {
            cerr << "Error when prepare process video " << i << endl;
            goto cleanup;
        }
    }

    if (ctx.perf)
    {
        gettimeofday(&start_time, nullptr);
    }

    for (uint32_t i = 0; i < ctx.num_thread; ++i)
    {
        pthread_create(&tids[i], nullptr, do_video_osd, &thread_ctxs[i]);
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

    for (uint32_t i = 0; i < ctx.num_thread; ++i)
    {
        ret = output_processed_video(&thread_ctxs[i]);
        if (ret)
        {
            cerr << "Error when output processed video " << i << endl;
            goto cleanup;
        }
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
