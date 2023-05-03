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
#include <cstdlib>
#include <cstring>
#include <linux/videodev2.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include "NvCudaProc.h"
#include "v4l2_nv_extensions.h"
#include "v4l2_backend.h"
#include "NvBufSurface.h"

#define TEST_ERROR(cond, str, label) if(cond) { \
                                        cerr << str << endl; \
                                        error = 1; \
                                        goto label; }

#define CHUNK_SIZE 4000000
#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

#define NAL_UNIT_START_CODE 0x00000001
#define MIN_CHUNK_SIZE      50

const char *GOOGLE_NET_DEPLOY_NAME =
        "../../data/Model/GoogleNet_one_class/GoogleNet_modified_oneClass_halfHD.prototxt";
const char *GOOGLE_NET_MODEL_NAME =
        "../../data/Model/GoogleNet_one_class/GoogleNet_modified_oneClass_halfHD.caffemodel";

#define IS_NAL_UNIT_START(buffer_ptr) (!buffer_ptr[0] && !buffer_ptr[1] && \
        !buffer_ptr[2] && (buffer_ptr[3] == 1))

#define IS_NAL_UNIT_START1(buffer_ptr) (!buffer_ptr[0] && !buffer_ptr[1] && \
        (buffer_ptr[2] == 1))

using namespace std;

#ifdef ENABLE_TRT
#define OSD_BUF_NUM 100

using namespace nvinfer1;
using namespace nvcaffeparser1;

#endif

EGLDisplay egl_display;

static uint64_t ts[CHANNEL_NUM];
static uint64_t time_scale[CHANNEL_NUM];

static int
read_decoder_input_nalu(ifstream * stream, NvBuffer * buffer,
        char *parse_buffer, streamsize parse_buffer_size)
{
    // Length is the size of the buffer in bytes
    char *buffer_ptr = (char *) buffer->planes[0].data;

    char *stream_ptr;
    bool nalu_found = false;

    streamsize bytes_read;
    streamsize stream_initial_pos = stream->tellg();

    stream->read(parse_buffer, parse_buffer_size);
    bytes_read = stream->gcount();

    if (bytes_read == 0)
    {
        return buffer->planes[0].bytesused = 0;
    }

    // Find the first NAL unit in the buffer
    stream_ptr = parse_buffer;
    while ((stream_ptr - parse_buffer) < (bytes_read - 3))
    {
        nalu_found = IS_NAL_UNIT_START(stream_ptr) ||
                        IS_NAL_UNIT_START1(stream_ptr);
        if (nalu_found)
        {
            break;
        }
        stream_ptr++;
    }

    // Reached end of buffer but could not find NAL unit
    if (!nalu_found)
    {
        cerr << "Could not read nal unit from file. EOF or file corrupted"
            << endl;
        return -1;
    }

    memcpy(buffer_ptr, stream_ptr, 4);
    buffer_ptr += 4;
    buffer->planes[0].bytesused = 4;
    stream_ptr += 4;

    // Copy bytes till the next NAL unit is found
    while ((stream_ptr - parse_buffer) < (bytes_read - 3))
    {
        if (IS_NAL_UNIT_START(stream_ptr) || IS_NAL_UNIT_START1(stream_ptr))
        {
            streamsize seekto = stream_initial_pos +
                    (stream_ptr - parse_buffer);
            if (stream->eof())
            {
                stream->clear();
            }
            stream->seekg(seekto, stream->beg);
            return 0;
        }
        *buffer_ptr = *stream_ptr;
        buffer_ptr++;
        stream_ptr++;
        buffer->planes[0].bytesused++;
    }

    // Reached end of buffer but could not find NAL unit
    cerr << "Could not read nal unit from file. EOF or file corrupted"
            << endl;
    return -1;
}

static int
read_decoder_input_chunk(ifstream * stream, NvBuffer * buffer)
{
    //length is the size of the buffer in bytes
    streamsize bytes_to_read = MIN(CHUNK_SIZE, buffer->planes[0].length);

    stream->read((char *) buffer->planes[0].data, bytes_to_read);
    // It is necessary to set bytesused properly, so that decoder knows how
    // many bytes in the buffer are valid
    buffer->planes[0].bytesused = stream->gcount();
    return 0;
}

static int
init_decode_ts()
{
    for (uint32_t i = 0; i < CHANNEL_NUM; i++)
    {
        ts[i] = 0L;
        time_scale[i] = 33000 * 10;
    }

    return 1;
}

static int
assign_decode_ts(struct v4l2_buffer *v4l2_buf, uint32_t channel)
{
    v4l2_buf->timestamp.tv_sec = ts[channel] + time_scale[channel];
    ts[channel] += time_scale[channel];

    return 1;
}

static nal_type_e
parse_nalu_unit(NvBuffer * buffer)
{
    unsigned char *pbuf = buffer->planes[0].data;

    return (nal_type_e)(*(pbuf + 4) & 0x1F);
}

static int
wait_for_nextFrame(context_t * ctx)
{
    pthread_mutex_lock(&ctx->fps_lock);
    uint64_t decode_time_usec;
    uint64_t decode_time_sec;
    uint64_t decode_time_nsec;
    struct timespec last_decode_time;
    struct timeval now;
    gettimeofday(&now, NULL);

    last_decode_time.tv_sec = now.tv_sec;
    last_decode_time.tv_nsec = now.tv_usec * 1000L;

    decode_time_usec = 1000000L / ctx->fps;
    decode_time_sec = decode_time_usec / 1000000;
    decode_time_nsec = (decode_time_usec % 1000000) * 1000L;

    last_decode_time.tv_sec += decode_time_sec;
    last_decode_time.tv_nsec += decode_time_nsec;
    last_decode_time.tv_sec += last_decode_time.tv_nsec / 1000000000UL;
    last_decode_time.tv_nsec %= 1000000000UL;

    pthread_cond_timedwait(&ctx->fps_cond, &ctx->fps_lock,
                &last_decode_time);
    pthread_mutex_unlock(&ctx->fps_lock);

    return 1;
}

static void *render_thread(void* arg)
{
    context_t *ctx = (context_t *) arg;
    Shared_Buffer render_buf;
    NvBufSurfaceParams param = {0};
    NvBufSurface *nvbuf_surf = 0;
#ifdef ENABLE_TRT
    frame_bbox temp_bbox;
    temp_bbox.g_rect_num = 0;
    temp_bbox.g_rect = new NvOSD_RectParams[OSD_BUF_NUM];
#endif
    while (1)
    {
        // waiting for buffer to come
        pthread_mutex_lock(&ctx->render_lock);
        while (ctx->render_buf_queue->empty())
            pthread_cond_wait(&ctx->render_cond, &ctx->render_lock);
        //pop up buffer from queue to process
        render_buf = ctx->render_buf_queue->front();
        ctx->render_buf_queue->pop();
        if(render_buf.fd <= 0)
        {
            pthread_mutex_unlock(&ctx->render_lock);
            break;
        }
        pthread_mutex_unlock(&ctx->render_lock);

        if (NvBufSurfaceFromFd(render_buf.fd, (void**)(&nvbuf_surf)) != 0)
        {
            cerr << "render_thread: NvBufferGetParams failed" << endl;
            return NULL;
        }
        param = nvbuf_surf->surfaceList[0];

        NvBufSurf::NvCommonTransformParams transform_params;
        transform_params.src_top = 0;
        transform_params.src_left = 0;
        transform_params.src_width = param.planeParams.width[0];
        transform_params.src_height = param.planeParams.height[0];
        transform_params.dst_top = 0;
        transform_params.dst_left = 0;
        transform_params.dst_width = ctx->window_width;
        transform_params.dst_height = ctx->window_height;
        transform_params.flag = NVBUFSURF_TRANSFORM_FILTER;
        transform_params.flip = NvBufSurfTransform_None;
        transform_params.filter = NvBufSurfTransformInter_Nearest;

        if (NvBufSurf::NvTransform(&transform_params, render_buf.fd,
                    ctx->render_fd) < 0)
        {
            cerr << "render_thread: NvTransform failed" << endl;
            return NULL;
        }

#ifndef ENABLE_TRT
        // Create EGLImage from dmabuf fd
        if (NvBufSurfaceFromFd(ctx->render_fd, (void**)(&nvbuf_surf)) != 0)
        {
            cerr << "Unable to extract NvBufSurfaceFromFd" << endl;
            break;
        }
        if (nvbuf_surf->surfaceList[0].mappedAddr.eglImage == NULL)
        {
            if (NvBufSurfaceMapEglImage(nvbuf_surf, 0) != 0)
            {
                cerr << "Unable to map EGL Image" << endl;
                return NULL;
            }
        }
        ctx->egl_image = nvbuf_surf->surfaceList[0].mappedAddr.eglImage;
        if (ctx->egl_image == NULL)
        {
            cerr << "Error while mapping render_buffer fd (" <<
                render_buf.fd << ") to EGLImage" << endl;
            return NULL;
        }

        // Running algo process with EGLImage via GPU multi cores
        HandleEGLImage(&ctx->egl_image);

        // Destroy EGLImage
        if (NvBufSurfaceFromFd(ctx->render_fd, (void**)(&nvbuf_surf)) != 0)
        {
            cerr << "Unable to extract NvBufSurfaceFromFd" << endl;
            break;
        }
        if (NvBufSurfaceUnMapEglImage(nvbuf_surf, 0) != 0)
        {
            cerr << "Unable to unmap EGL Image" << endl;
            break;
        }
        ctx->egl_image = NULL;
#else
        temp_bbox.g_rect_num = render_buf.bbox->g_rect_num;
        memcpy(temp_bbox.g_rect, render_buf.bbox->g_rect,
                OSD_BUF_NUM * sizeof(NvOSD_RectParams));
        delete []render_buf.bbox->g_rect;
        delete render_buf.bbox;
        render_buf.bbox = NULL;

        if (temp_bbox.g_rect_num != 0)
            nvosd_draw_rectangles(ctx->nvosd_context, MODE_HW,
                    ctx->render_fd, temp_bbox.g_rect_num, temp_bbox.g_rect);
#endif
        // EglRenderer requires the fd of the 0th plane to render
        ctx->renderer->render(ctx->render_fd);

        if (ctx->got_eos == true)
        {
            cerr << "render_thread exit" << endl;
            break;
        }
    }
#ifdef ENABLE_TRT
    delete []temp_bbox.g_rect;
#endif

    cout << "render_thread exit!" << endl;

    return NULL;
}

#ifdef ENABLE_TRT
static void *trt_thread(void *arg)
{
    EGLImageKHR egl_image = NULL;
    Shared_Buffer trt_buf[CHANNEL_NUM];
    trt_context *ctx = (trt_context *) arg;
    TRT_Context *tctx = &ctx->tctx;
    context_t *channel_ctx = ctx->ctx;
    int classCnt = tctx->getModelClassCnt();
    Shared_Buffer rcv_buf;
    int channel = 0;
    NvBufSurfaceParams param = {0};
    NvBufSurface *nvbuf_surf = 0;

    while (1)
    {
        uint32_t buf_num = 0;

        // wait for buffer for process to come
        pthread_mutex_lock(&ctx->trt_lock);
        while (ctx->trt_buf_queue->empty())
            pthread_cond_wait(&ctx->trt_cond, &ctx->trt_lock);
        //pop up buffer from queue to process
        rcv_buf = ctx->trt_buf_queue->front();
        ctx->trt_buf_queue->pop();
        pthread_mutex_unlock(&ctx->trt_lock);

        // fd == -1 indicates stream ended
        if(rcv_buf.fd == -1)
        {
            cout << "trt_thread: end of stream, exit!" << endl;
            break;
        }

        if (NvBufSurfaceFromFd(rcv_buf.fd, (void**)(&nvbuf_surf)) != 0)
        {
            cerr << "trt_thread: NvBufSurfaceFromFd failed" << endl;
            return NULL;
        }
        param = nvbuf_surf->surfaceList[0];

        NvBufSurf::NvCommonTransformParams transform_params;
        transform_params.src_top = 0;
        transform_params.src_left = 0;
        transform_params.src_width = param.planeParams.width[0];
        transform_params.src_height = param.planeParams.height[0];
        transform_params.dst_top = 0;
        transform_params.dst_left = 0;
        transform_params.dst_width = tctx->getNetWidth();
        transform_params.dst_height = tctx->getNetHeight();
        transform_params.flag = NVBUFSURF_TRANSFORM_FILTER;
        transform_params.flip = NvBufSurfTransform_None;
        transform_params.filter = NvBufSurfTransformInter_Nearest;
        if (NvBufSurf::NvTransform(&transform_params, rcv_buf.fd,
                    channel_ctx[rcv_buf.channel].trt_fd) < 0)
        {
            cerr << "trt_thread: NvTransform failed on channel " <<
                rcv_buf.channel << endl;
            return NULL;
        }

        // we still have buffer, so accumulate buffer into batch
        int batch_offset = buf_num  * tctx->getNetWidth() *
            tctx->getNetHeight() * tctx->getChannel();

        // map fd into EGLImage, then copy it with GPU in parallel
        // Create EGLImage from dmabuf fd
        if (NvBufSurfaceFromFd(channel_ctx[rcv_buf.channel].trt_fd,
                    (void**)(&nvbuf_surf)) != 0)
        {
            cerr << "Unable to extract NvBufSurfaceFromFd" << endl;
            return NULL;
        }
        if (nvbuf_surf->surfaceList[0].mappedAddr.eglImage == NULL)
        {
            if (NvBufSurfaceMapEglImage(nvbuf_surf, 0) != 0)
            {
                cerr << "Unable to map EGL Image" << endl;
                break;
            }
        }
        egl_image = nvbuf_surf->surfaceList[0].mappedAddr.eglImage;
        if (egl_image == NULL)
        {
            cerr << "Error while mapping dmabuf fd (" <<
                channel_ctx[rcv_buf.channel].trt_fd << ") to EGLImage" << endl;
            return NULL;
        }

        void *cuda_buf = tctx->getBuffer(0);
        // map eglimage into GPU address
        mapEGLImage2Float(&egl_image,
                tctx->getNetWidth(),
                tctx->getNetHeight(),
                (TRT_MODEL == GOOGLENET_THREE_CLASS) ?
                COLOR_FORMAT_BGR : COLOR_FORMAT_RGB,
                (char *)cuda_buf + batch_offset * sizeof(float),
                tctx->getOffsets(),
                tctx->getScales());

        // Destroy EGLImage
        if (NvBufSurfaceFromFd(channel_ctx[rcv_buf.channel].trt_fd,
                    (void**)(&nvbuf_surf)) != 0)
        {
            cerr << "Unable to extract NvBufSurfaceFromFd" << endl;
            break;
        }
        if (NvBufSurfaceUnMapEglImage(nvbuf_surf, 0) != 0)
        {
            cerr << "Unable to unmap EGL Image" << endl;
            break;
        }
        egl_image = NULL;

        trt_buf[buf_num].channel = rcv_buf.channel;
        trt_buf[buf_num].fd = rcv_buf.fd; //channel_ctx[rcv_buf.channel].trt_fd; //rcv_buf.fd;
        buf_num++;
        // buffer is not enough for a batch, continue to wait for buffer
        if (buf_num < tctx->getBatchSize())
            continue;

        // buffers are bacthed, start inference
        buf_num = 0;
        queue<vector<cv::Rect>> rectList_queue[classCnt];
        tctx->doInference(
                rectList_queue);

        for (int i = 0; i < classCnt; i++)
            assert(rectList_queue[i].size() == tctx->getBatchSize());

        // post-processing
        while (!rectList_queue[0].empty())
        {
            int rectNum = 0;
            frame_bbox *bbox = new frame_bbox;
            int width, height;

            channel = trt_buf[buf_num].channel;
            if (NvBufSurfaceFromFd(channel_ctx[channel].render_fd,
                        (void**)(&nvbuf_surf)) != 0)
            {
                cerr << "trt_thread: NvBufSurfaceFromFd failed" << endl;
                return NULL;
            }
            param = nvbuf_surf->surfaceList[0];
            width = param.planeParams.width[0];
            height = param.planeParams.height[0];

            bbox->g_rect_num = 0;
            bbox->g_rect = new NvOSD_RectParams[OSD_BUF_NUM];

            // batch by batch to run below loop
            for (int class_num = 0; class_num < classCnt; class_num++)
            {
                vector<cv::Rect> rectList = rectList_queue[class_num].front();
                rectList_queue[class_num].pop();
                for (uint32_t i = 0; i < rectList.size(); i++)
                {
                    cv::Rect &r = rectList[i];
                    if ((r.width * width / tctx->getNetWidth() < 10) ||
                        (r.height * height / tctx->getNetHeight() < 10))
                        continue;
                    bbox->g_rect[rectNum].left =
                        (unsigned int) (r.x * width / tctx->getNetWidth());
                    bbox->g_rect[rectNum].top =
                        (unsigned int) (r.y * height / tctx->getNetHeight());
                    bbox->g_rect[rectNum].width =
                        (unsigned int) (r.width * width / tctx->getNetWidth());
                    bbox->g_rect[rectNum].height =
                        (unsigned int) (r.height * height / tctx->getNetHeight());
                    bbox->g_rect[rectNum].border_width = 8;
                    bbox->g_rect[rectNum].has_bg_color = 0;
                    bbox->g_rect[rectNum].border_color.red = ((class_num == 0) ? 1.0f : 0.0);
                    bbox->g_rect[rectNum].border_color.green = ((class_num == 1) ? 1.0f : 0.0);
                    bbox->g_rect[rectNum].border_color.blue = ((class_num == 2) ? 1.0f : 0.0);
                    rectNum++;
                }
            }

            bbox->g_rect_num = rectNum;
            trt_buf[buf_num].bbox = bbox;
            pthread_mutex_lock(&channel_ctx[channel].render_lock);
            channel_ctx[channel].render_buf_queue->push(trt_buf[buf_num]);
            pthread_cond_broadcast(&channel_ctx[channel].render_cond);
            pthread_mutex_unlock(&channel_ctx[channel].render_lock);
            buf_num++;
        }
    }

    for (int i = 0; i < CHANNEL_NUM; i++)
    {
        if(channel_ctx[i].do_stat)
        {
            trt_buf[i].fd = -1;
            channel_ctx[i].render_buf_queue->push(trt_buf[i]);
            pthread_cond_broadcast(&channel_ctx[i].render_cond);
        }
    }

    cout << "trt_thread exit!" << endl;

    return NULL;
}
#endif

static void
query_and_set_capture(context_t * ctx)
{
    NvVideoDecoder *dec = ctx->dec;
    struct v4l2_format format;
    struct v4l2_crop crop;
    int32_t min_dec_capture_buffers;
    int ret = 0;
    int error = 0;
    uint32_t window_width;
    uint32_t window_height;
    NvBufSurfaceColorFormat pix_format;
    NvBufSurf::NvCommonAllocateParams cParams;
#ifndef ENABLE_TRT
    char OSDcontent[512];
#else
    trt_context *trt_ctx = ctx->trt_ctx;
#endif

    // Get capture plane format from the decoder. This may change after
    // an resolution change event
    ret = dec->capture_plane.getFormat(format);
    TEST_ERROR(ret < 0,
            "Error: Could not get format from decoder capture plane", error);

    // Get the display resolution from the decoder
    ret = dec->capture_plane.getCrop(crop);
    TEST_ERROR(ret < 0,
           "Error: Could not get crop from decoder capture plane", error);

    // Destroy the old instance of renderer as resolution might changed
    delete ctx->renderer;

    if (ctx->fullscreen)
    {
        // Required for fullscreen
        window_width = window_height = 0;
    }
    else if (ctx->window_width && ctx->window_height)
    {
        // As specified by user on commandline
        window_width = ctx->window_width;
        window_height = ctx->window_height;
    }
    else
    {
        // Resolution got from the decoder
        window_width = crop.c.width;
        window_height = crop.c.height;
    }

    // If height or width are set to zero, EglRenderer creates a fullscreen
    // window
    ctx->renderer =
        NvEglRenderer::createEglRenderer("renderer0", window_width,
                window_height, ctx->window_x,
                ctx->window_y);
    TEST_ERROR(!ctx->renderer,
            "Error in setting up renderer. "
            "Check if X is running or run with --disable-rendering",
            error);

    ctx->renderer->setFPS(ctx->fps);
#ifndef ENABLE_TRT
    sprintf(OSDcontent, "Channel:%d", ctx->channel);
    ctx->renderer->setOverlayText(OSDcontent, 800, 50);
#endif

    // deinitPlane unmaps the buffers and calls REQBUFS with count 0
    dec->capture_plane.deinitPlane();
    for(int index = 0 ; index < ctx->numCapBuffers ; index++)
    {
        if(ctx->dmabuff_fd[index] != 0)
        {
            ret = NvBufSurf::NvDestroy(ctx->dmabuff_fd[index]);
            TEST_ERROR(ret < 0, "Error: Error in BufferDestroy", error);
        }
    }

    // Not necessary to call VIDIOC_S_FMT on decoder capture plane.
    // But decoder setCapturePlaneFormat function updates the class variables
    ret = dec->setCapturePlaneFormat(format.fmt.pix_mp.pixelformat,
                                     format.fmt.pix_mp.width,
                                     format.fmt.pix_mp.height);
    TEST_ERROR(ret < 0, "Error in setting decoder capture plane format",
                error);

    // Get the minimum buffers which have to be requested on the capture plane
    ret =
        dec->getControl(V4L2_CID_MIN_BUFFERS_FOR_CAPTURE,
                        min_dec_capture_buffers);
    TEST_ERROR(ret < 0,
               "Error while getting value for V4L2_CID_MIN_BUFFERS_FOR_CAPTURE",
               error);

    /* Set colorformats for relevant colorspaces. */
    switch(format.fmt.pix_mp.colorspace)
    {
        case V4L2_COLORSPACE_SMPTE170M:
            if (format.fmt.pix_mp.quantization == V4L2_QUANTIZATION_DEFAULT)
            {
                cout << "Decoder colorspace ITU-R BT.601 with standard range luma (16-235)" << endl;
                pix_format = NVBUF_COLOR_FORMAT_NV12;
            }
            else
            {
                cout << "Decoder colorspace ITU-R BT.601 with extended range luma (0-255)" << endl;
                pix_format = NVBUF_COLOR_FORMAT_NV12_ER;
            }
            break;
        case V4L2_COLORSPACE_REC709:
            if (format.fmt.pix_mp.quantization == V4L2_QUANTIZATION_DEFAULT)
            {
                cout << "Decoder colorspace ITU-R BT.709 with standard range luma (16-235)" << endl;
                pix_format =  NVBUF_COLOR_FORMAT_NV12_709;
            }
            else
            {
                cout << "Decoder colorspace ITU-R BT.709 with extended range luma (0-255)" << endl;
                pix_format = NVBUF_COLOR_FORMAT_NV12_709_ER;
            }
            break;
        case V4L2_COLORSPACE_BT2020:
            {
                cout << "Decoder colorspace ITU-R BT.2020" << endl;
                pix_format = NVBUF_COLOR_FORMAT_NV12_2020;
            }
            break;
        default:
            cout << "supported colorspace details not available, use default" << endl;
            if (format.fmt.pix_mp.quantization == V4L2_QUANTIZATION_DEFAULT)
            {
                cout << "Decoder colorspace ITU-R BT.601 with standard range luma (16-235)" << endl;
                pix_format = NVBUF_COLOR_FORMAT_NV12;
            }
            else
            {
                cout << "Decoder colorspace ITU-R BT.601 with extended range luma (0-255)" << endl;
                pix_format = NVBUF_COLOR_FORMAT_NV12_ER;
            }
            break;
    }

    ctx->numCapBuffers = min_dec_capture_buffers + ctx->extra_cap_plane_buffer;

    if (format.fmt.pix_mp.pixelformat  == V4L2_PIX_FMT_NV24M)
        pix_format = NVBUF_COLOR_FORMAT_NV24;
    else if (format.fmt.pix_mp.pixelformat  == V4L2_PIX_FMT_NV24_10LE)
        pix_format = NVBUF_COLOR_FORMAT_NV24_10LE;

    cParams.memType = NVBUF_MEM_SURFACE_ARRAY;
    cParams.width = format.fmt.pix_mp.width;
    cParams.height = format.fmt.pix_mp.height;
    cParams.layout = NVBUF_LAYOUT_BLOCK_LINEAR;
    cParams.memtag = NvBufSurfaceTag_VIDEO_DEC;
    cParams.colorFormat = pix_format;
    ret = NvBufSurf::NvAllocate(&cParams, ctx->numCapBuffers, ctx->dmabuff_fd);
    TEST_ERROR(ret < 0, "Failed to create buffers", error);

    /* Request buffers on decoder capture plane.
       Refer ioctl VIDIOC_REQBUFS */
    ret = dec->capture_plane.reqbufs(V4L2_MEMORY_DMABUF, ctx->numCapBuffers);
    TEST_ERROR(ret, "Error in request buffers on capture plane", error);

    // create conv to convert the frame from decoder for render
    // NV12 BL --> RGBA PL, since OSD needs RGBA format
    NvBufSurf::NvDestroy(ctx->render_fd);
#ifdef ENABLE_TRT
    // VIC OSD draws on RGBA frame
    cParams.colorFormat = NVBUF_COLOR_FORMAT_RGBA;
#else
    // CUDA draw OSD (black box) on NV12 frame
    cParams.colorFormat = NVBUF_COLOR_FORMAT_NV12;
#endif
    cParams.memtag = NvBufSurfaceTag_VIDEO_CONVERT;
    cParams.memType = NVBUF_MEM_SURFACE_ARRAY;
    cParams.width = window_width;
    cParams.height = window_height;
    cParams.layout = NVBUF_LAYOUT_PITCH;
    ret = NvBufSurf::NvAllocate(&cParams, 1, &ctx->render_fd);
    TEST_ERROR(ret < 0,
            "Error when allocate NvBufSurf for render fd",
            error);

#ifdef ENABLE_TRT
    // create conv1 to convert the frame from decoder for TensorRT
    // NV12 BL --> RGBA packed & scale to the infer model resolution
    NvBufSurf::NvDestroy(ctx->trt_fd);
    cParams.colorFormat = NVBUF_COLOR_FORMAT_BGRA; //NVBUF_COLOR_FORMAT_RGBA;
    cParams.memtag = NvBufSurfaceTag_VIDEO_CONVERT;
    cParams.memType = NVBUF_MEM_SURFACE_ARRAY;
    cParams.width = trt_ctx->tctx.getNetWidth();
    cParams.height = trt_ctx->tctx.getNetHeight();
    cParams.layout = NVBUF_LAYOUT_PITCH;
    ret = NvBufSurf::NvAllocate(&cParams, 1, &ctx->trt_fd);
    TEST_ERROR(ret < 0,
            "Error when allocate NvBufSurf for TensorRT fd",
            error);
#endif

    // Capture plane STREAMON
    ret = dec->capture_plane.setStreamStatus(true);
    TEST_ERROR(ret < 0, "Error in decoder capture plane streamon", error);

    // Enqueue all the empty capture plane buffers
    for (uint32_t i = 0; i < dec->capture_plane.getNumBuffers(); i++)
    {
        struct v4l2_buffer v4l2_buf;
        struct v4l2_plane planes[MAX_PLANES];

        memset(&v4l2_buf, 0, sizeof(v4l2_buf));
        memset(planes, 0, sizeof(planes));

        v4l2_buf.index = i;
        v4l2_buf.m.planes = planes;
        v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        v4l2_buf.memory = V4L2_MEMORY_DMABUF;
        v4l2_buf.m.planes[0].m.fd = ctx->dmabuff_fd[i];
        ret = dec->capture_plane.qBuffer(v4l2_buf, NULL);
        TEST_ERROR(ret < 0, "Error Qing buffer at output plane", error);
    }
    cout << "Query and set capture  successful" << endl;
    return;

error:
    if (error)
    {
        ctx->got_error = true;
        cerr << "Error in " << __func__ << endl;
    }
}

static void *
dec_capture_loop_fcn(void *arg)
{
    context_t *ctx = (context_t *) arg;
    NvVideoDecoder *dec = ctx->dec;
    map<uint64_t, frame_info_t*>::iterator  iter;
    struct v4l2_event ev;
    int ret;
    Shared_Buffer batch_buffer;
#ifdef ENABLE_TRT
    trt_context *trt_ctx = ctx->trt_ctx;
#endif

    cout << "Starting decoder capture loop thread" << endl;
    // Need to wait for the first Resolution change event, so that
    // the decoder knows the stream resolution and can allocate appropriate
    // buffers when we call REQBUFS
    do
    {
        ret = dec->dqEvent(ev, 1000);
        if (ret < 0)
        {
            if (errno == EAGAIN)
            {
                cerr <<
                    "Timed out waiting for first V4L2_EVENT_RESOLUTION_CHANGE"
                    << endl;
            }
            else
            {
                cerr << "Error in dequeueing decoder event" << endl;
            }
            ctx->got_error = true;
            break;
        }
    }
    while (ev.type != V4L2_EVENT_RESOLUTION_CHANGE);

    // query_and_set_capture acts on the resolution change event
    if (!ctx->got_error)
        query_and_set_capture(ctx);

    // Exit on error or EOS which is signalled in main()
    while (!(ctx->got_error || dec->isInError() || ctx->got_eos))
    {
        NvBuffer *dec_buffer;

        // Check for Resolution change again
        ret = dec->dqEvent(ev, false);
        if (ret == 0)
        {
            switch (ev.type)
            {
                case V4L2_EVENT_RESOLUTION_CHANGE:
                    query_and_set_capture(ctx);
                    continue;
            }
        }

        while (1)
        {
            struct v4l2_buffer v4l2_buf;
            struct v4l2_plane planes[MAX_PLANES];

            memset(&v4l2_buf, 0, sizeof(v4l2_buf));
            memset(planes, 0, sizeof(planes));
            v4l2_buf.m.planes = planes;

            // Dequeue a filled buffer
            if (dec->capture_plane.dqBuffer(
                        v4l2_buf, &dec_buffer, NULL, 0))
            {
                if (errno == EAGAIN)
                {
                    usleep(5000);
                }
                else
                {
                    ctx->got_error = true;
                    cerr << "Error while calling dequeue at capture plane" <<
                        endl;
                }
                break;
            }

            if (ctx->do_stat)
            {
                iter = ctx->frame_info_map->find(
                        v4l2_buf.timestamp.tv_sec);
                if (iter == ctx->frame_info_map->end())
                {
                    cout<<"image not return by decoder"<<endl;
                }
                else
                {
                    gettimeofday(&iter->second->output_time, NULL);
                }
            }

            /* Queue the buffer back once it has been used.
             * NOTE: If we are not rendering, queue the buffer back here immediately. */
            v4l2_buf.m.planes[0].m.fd = ctx->dmabuff_fd[v4l2_buf.index];
            if (dec->capture_plane.qBuffer(v4l2_buf, NULL) < 0)
            {
                cerr << "Error while queueing buffer at decoder capture plane" << endl;
                break;
            }

            // capture_plane_mem_type == V4L2_MEMORY_DMABUF
            dec_buffer->planes[0].fd = ctx->dmabuff_fd[v4l2_buf.index];
            batch_buffer.fd = dec_buffer->planes[0].fd;
            batch_buffer.channel = ctx->channel;
#ifdef ENABLE_TRT
            // pass the buffer to trt thread
            pthread_mutex_lock(&trt_ctx->trt_lock);
            trt_ctx->trt_buf_queue->push(batch_buffer);
            pthread_cond_broadcast(&trt_ctx->trt_cond);
            pthread_mutex_unlock(&trt_ctx->trt_lock);
#else
            // if no trt, pass buffer to render directly
            // otherwise, pass buffer to render by trt_thread
            pthread_mutex_lock(&ctx->render_lock);
            ctx->render_buf_queue->push(batch_buffer);
            pthread_cond_broadcast(&ctx->render_cond);
            pthread_mutex_unlock(&ctx->render_lock);
#endif
        }
    }

    cout << "Exiting decoder capture loop thread" << endl;
    // Signal EOS to the decoder capture loop
    ctx->got_eos = true;

#ifdef ENABLE_TRT
    // send fd=-1 to indicate it's ending
    batch_buffer.fd = -1;
    pthread_mutex_lock(&trt_ctx->trt_lock);
    trt_ctx->trt_buf_queue->push(batch_buffer);
    pthread_cond_broadcast(&trt_ctx->trt_cond);
    pthread_mutex_unlock(&trt_ctx->trt_lock);
#endif
    return NULL;
}

static void *
dec_feed_loop_fcn(void *arg)
{
    context_t *ctx = (context_t *) arg;
    int i = 0;
    bool eos = false;
    int ret;
    char *nalu_parse_buffer = NULL;
    nal_type_e nal_type;

    if (ctx->input_nalu)
    {
        nalu_parse_buffer = new char[CHUNK_SIZE];
    }

    // Read encoded data and enqueue all the output plane buffers.
    // Exit loop in case file read is complete.
    while (!eos && !ctx->got_error && !ctx->dec->isInError() &&
        i < (int)ctx->dec->output_plane.getNumBuffers())
    {
        struct v4l2_buffer v4l2_buf;
        struct v4l2_plane planes[MAX_PLANES];
        NvBuffer *buffer;

        memset(&v4l2_buf, 0, sizeof(v4l2_buf));
        memset(planes, 0, sizeof(planes));

        buffer = ctx->dec->output_plane.getNthBuffer(i);
        if (ctx->input_nalu)
        {
            read_decoder_input_nalu(ctx->in_file, buffer,
                    nalu_parse_buffer, CHUNK_SIZE);
            wait_for_nextFrame(ctx);
        }
        else
        {
            read_decoder_input_chunk(ctx->in_file, buffer);
        }

        v4l2_buf.index = i;
        if (ctx->input_nalu && ctx->do_stat)
        {
            nal_type = parse_nalu_unit(buffer);
            switch (nal_type)
            {
                case NAL_UNIT_CODED_SLICE:
                case NAL_UNIT_CODED_SLICE_DATAPART_A:
                case NAL_UNIT_CODED_SLICE_DATAPART_B:
                case NAL_UNIT_CODED_SLICE_DATAPART_C:
                case NAL_UNIT_CODED_SLICE_IDR:
                {
                    assign_decode_ts(&v4l2_buf, ctx->channel);
                    frame_info_t *frame_meta = new frame_info_t;
                    memset(frame_meta, 0, sizeof(frame_info_t));

                    frame_meta->timestamp = v4l2_buf.timestamp.tv_sec;
                    gettimeofday(&frame_meta->input_time, NULL);
                    frame_meta->nal_type = nal_type;

                    ctx->frame_info_map->insert(
                        pair< uint64_t, frame_info_t* >(
                        v4l2_buf.timestamp.tv_sec, frame_meta));
                    break;
                }
                default:
                    break;
            }
        }
        v4l2_buf.m.planes = planes;
        v4l2_buf.m.planes[0].bytesused = buffer->planes[0].bytesused;
        // It is necessary to queue an empty buffer to signal EOS to the decoder
        // i.e. set v4l2_buf.m.planes[0].bytesused = 0 and queue the buffer
        ret = ctx->dec->output_plane.qBuffer(v4l2_buf, NULL);
        if (ret < 0)
        {
            cerr << "Error Qing buffer at output plane" << endl;
            ctx->got_error = true;
            break;
        }
        if (v4l2_buf.m.planes[0].bytesused == 0)
        {
            eos = true;
            cout << "Input file read complete" << endl;
            break;
        }
        i++;
    }

    // Since all the output plane buffers have been queued, we first need to
    // dequeue a buffer from output plane before we can read new data into it
    // and queue it again.
    while (!eos && !ctx->got_error && !ctx->dec->isInError())
    {
        struct v4l2_buffer v4l2_buf;
        struct v4l2_plane planes[MAX_PLANES];
        NvBuffer *buffer;

        memset(&v4l2_buf, 0, sizeof(v4l2_buf));
        memset(planes, 0, sizeof(planes));
        v4l2_buf.m.planes = planes;

        ret = ctx->dec->output_plane.dqBuffer(v4l2_buf, &buffer, NULL, -1);
        if (ret < 0)
        {
            cerr << "Error DQing buffer at output plane" << endl;
            ctx->got_error = true;
            break;
        }

        if (ctx->input_nalu)
        {
            read_decoder_input_nalu(ctx->in_file, buffer,
                                    nalu_parse_buffer, CHUNK_SIZE);
            wait_for_nextFrame(ctx);
        }
        else
        {
            read_decoder_input_chunk(ctx->in_file, buffer);
        }

        if (ctx->input_nalu && ctx->do_stat)
        {
            nal_type = parse_nalu_unit(buffer);
            switch (nal_type)
            {
                case NAL_UNIT_CODED_SLICE:
                case NAL_UNIT_CODED_SLICE_DATAPART_A:
                case NAL_UNIT_CODED_SLICE_DATAPART_B:
                case NAL_UNIT_CODED_SLICE_DATAPART_C:
                case NAL_UNIT_CODED_SLICE_IDR:
                {
                    assign_decode_ts(&v4l2_buf, ctx->channel);
                    frame_info_t *frame_meta = new frame_info_t;
                    memset(frame_meta, 0, sizeof(frame_info_t));

                    frame_meta->timestamp = v4l2_buf.timestamp.tv_sec;
                    gettimeofday(&frame_meta->input_time, NULL);
                    frame_meta->nal_type = nal_type;
                    ctx->frame_info_map->insert(
                        pair< uint64_t, frame_info_t* >(
                        v4l2_buf.timestamp.tv_sec, frame_meta));
                    break;
                }
                default:
                    break;
            }
        }
        v4l2_buf.m.planes[0].bytesused = buffer->planes[0].bytesused;
        ret = ctx->dec->output_plane.qBuffer(v4l2_buf, NULL);
        if (ret < 0)
        {
            cerr << "Error Qing buffer at output plane" << endl;
            ctx->got_error = true;
            break;
        }
        if (v4l2_buf.m.planes[0].bytesused == 0)
        {
            eos = true;
            cout << "Input file read complete" << endl;
            break;
        }
    }

    // After sending EOS, all the buffers from output plane should be dequeued.
    // and after that capture plane loop should be signalled to stop.
    while (ctx->dec->output_plane.getNumQueuedBuffers() > 0 &&
           !ctx->got_error && !ctx->dec->isInError())
    {
        struct v4l2_buffer v4l2_buf;
        struct v4l2_plane planes[MAX_PLANES];

        memset(&v4l2_buf, 0, sizeof(v4l2_buf));
        memset(planes, 0, sizeof(planes));

        v4l2_buf.m.planes = planes;
        ret = ctx->dec->output_plane.dqBuffer(v4l2_buf, NULL, NULL, -1);
        if (ret < 0)
        {
            cerr << "Error DQing buffer at output plane" << endl;
            ctx->got_error = true;
            break;
        }
    }

    if (ctx->input_nalu)
    {
        delete []nalu_parse_buffer;
    }

    ctx->got_eos = true;
    return NULL;
}

static void
set_defaults(context_t * ctx)
{
    memset(ctx, 0, sizeof(context_t));
    ctx->fullscreen = false;
    ctx->extra_cap_plane_buffer = 1;
    ctx->window_height = 0;
    ctx->window_width = 0;
    ctx->window_x = 0;
    ctx->window_y = 0;
    ctx->input_nalu = 1;
    ctx->fps = 10;
    ctx->disable_dpb = false;
    ctx->do_stat = 1;
    ctx->dec_status = 0;
    ctx->render_buf_queue = new queue <Shared_Buffer>;
    ctx->stop_render = 0;
    ctx->frame_info_map = new map< uint64_t, frame_info_t* >;
    ctx->nvosd_context = NULL;
    pthread_mutex_init(&ctx->render_lock, NULL);
}

static void
set_globalcfg_default(global_cfg *cfg)
{
#ifdef ENABLE_TRT
    cfg->deployfile = GOOGLE_NET_DEPLOY_NAME;
    cfg->modelfile = GOOGLE_NET_MODEL_NAME;
#endif
}

static void
get_disp_resolution(display_resolution_t *res)
{
    if (NvEglRenderer::getDisplayResolution(
            res->window_width, res->window_height) < 0)
    {
        cerr << "get resolution failed, program will exit" << endl;
        exit(0);
    }

    return;
}

int
main(int argc, char *argv[])
{
    context_t ctx[CHANNEL_NUM];
    global_cfg cfg;
    int error = 0;
    uint32_t iterator;
    map<uint64_t, frame_info_t*>::iterator  iter;
    display_resolution_t disp_info;
    char **argp;
#ifdef ENABLE_TRT
    trt_context trt_ctx;
#endif

    set_globalcfg_default(&cfg);
    argp = argv;
    parse_global(&cfg, argc, &argp);
    memset(ctx, 0, sizeof(ctx));

    if (parse_csv_args(&ctx[0],
#ifdef ENABLE_TRT
        &trt_ctx.tctx,
#endif
        argc - cfg.channel_num - 1, argp))
    {
        fprintf(stderr, "Error parsing commandline arguments\n");
        return -1;
    }

#ifdef ENABLE_TRT
    trt_ctx.tctx.setModelIndex(TRT_MODEL);
    trt_ctx.tctx.buildTrtContext(cfg.deployfile, cfg.modelfile);

    //Batchsize * FilterNum should be not bigger than buffers allocated by VIC
    if (trt_ctx.tctx.getBatchSize() * trt_ctx.tctx.getFilterNum() > 10)
    {
        fprintf(stderr,
            "Not enough buffers. Decrease trt-proc-interval and run again. Exiting\n");
        trt_ctx.tctx.destroyTrtContext();
        return 0;
    }

    trt_ctx.trt_buf_queue = new queue <Shared_Buffer>;
    pthread_mutex_init(&trt_ctx.trt_lock, NULL);
    pthread_cond_init(&trt_ctx.trt_cond, NULL);
    trt_ctx.osd_queue = new queue <frame_bbox*>;
    trt_ctx.ctx = ctx;

    pthread_create(&trt_ctx.trt_handle, NULL, trt_thread, &trt_ctx);
    pthread_setname_np(trt_ctx.trt_handle, "TRTThreadHandle");
#endif

    get_disp_resolution(&disp_info);
    init_decode_ts();

    // Get defalut EGL display
    egl_display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (egl_display == EGL_NO_DISPLAY)
    {
        cout<<"Error while get EGL display connection"<<endl;
        return -1;
    }

    // Init EGL display connection
    if (!eglInitialize(egl_display, NULL, NULL))
    {
        cout<<"Erro while initialize EGL display connection"<<endl;
        return -1;
    }

    for (iterator = 0; iterator < cfg.channel_num; iterator++)
    {
        int ret = 0;

        set_defaults(&ctx[iterator]);
#ifdef ENABLE_TRT
        ctx[iterator].trt_ctx = &trt_ctx;
#endif

        char decname[512];
        sprintf(decname, "dec%d", iterator);
        ctx[iterator].channel = iterator;

        if (parse_csv_args(&ctx[iterator],
#ifdef ENABLE_TRT
            &trt_ctx.tctx,
#endif
            argc - cfg.channel_num - 1, argp))
        {
            fprintf(stderr, "Error parsing commandline arguments\n");
            return -1;
        }

        ctx[iterator].in_file_path = cfg.in_file_path[iterator];
        ctx[iterator].nvosd_context = nvosd_create_context();
        ctx[iterator].dec = NvVideoDecoder::createVideoDecoder(decname);
        TEST_ERROR(!ctx[iterator].dec, "Could not create decoder", cleanup);

        // Subscribe to Resolution change event
        ret = ctx[iterator].dec->subscribeEvent(V4L2_EVENT_RESOLUTION_CHANGE,
                        0, 0);
        TEST_ERROR(ret < 0,
                "Could not subscribe to V4L2_EVENT_RESOLUTION_CHANGE",
                cleanup);

        // Set format on the output plane
        ret = ctx[iterator].dec->setOutputPlaneFormat(
                    ctx[iterator].decoder_pixfmt, CHUNK_SIZE);

        // Set V4L2_CID_MPEG_VIDEO_DISABLE_COMPLETE_FRAME_INPUT control to false
        // so that application can send chunks/slice of encoded data instead of
        // forming complete frames. This needs to be done before setting format
        // on the output plane.
        ret = ctx[iterator].dec->setFrameInputMode(1);
        TEST_ERROR(ret < 0, "Error in setFrameInputMode", cleanup);

        // V4L2_CID_MPEG_VIDEO_DISABLE_DPB should be set after output plane
        // set format
        if (ctx[iterator].disable_dpb)
        {
            ret = ctx[iterator].dec->disableDPB();
            TEST_ERROR(ret < 0, "Error in disableDPB", cleanup);
        }

        // Query, Export and Map the output plane buffers so that we can read
        // encoded data into the buffers
        ret = ctx[iterator].dec->output_plane.setupPlane(
                V4L2_MEMORY_MMAP, 10, true, false);
        TEST_ERROR(ret < 0, "Error while setting up output plane", cleanup);

        ctx[iterator].in_file = new ifstream(ctx[iterator].in_file_path);
        TEST_ERROR(!ctx[iterator].in_file->is_open(),
                "Error opening input file", cleanup);

        if (ctx[iterator].out_file_path)
        {
            ctx[iterator].out_file = new ofstream(ctx[iterator].out_file_path);
            TEST_ERROR(!ctx[iterator].out_file->is_open(),
                        "Error opening output file",
                        cleanup);
        }

        pthread_create(&ctx[iterator].render_feed_handle, NULL,
                                render_thread, &ctx[iterator]);
        char render_thread[16] = "RenderThread";
        string s = to_string(iterator);
        strcat(render_thread, s.c_str());
        pthread_setname_np(ctx[iterator].render_feed_handle, render_thread);

        ret = ctx[iterator].dec->output_plane.setStreamStatus(true);
        TEST_ERROR(ret < 0, "Error in output plane stream on", cleanup);
        if (cfg.channel_num == 1)
        {
            ctx[iterator].window_width = disp_info.window_width;
            ctx[iterator].window_height = disp_info.window_height;
            ctx[iterator].window_x = 0;
            ctx[iterator].window_y = 0;
        }
        else
        {
            if (iterator == 0)
            {
                ctx[iterator].window_width = disp_info.window_width / 2;
                ctx[iterator].window_height = disp_info.window_height / 2;
                ctx[iterator].window_x = 0;
                ctx[iterator].window_y = 0;
            }
            else if (iterator == 1)
            {
                ctx[iterator].window_width = disp_info.window_width / 2;
                ctx[iterator].window_height = disp_info.window_height / 2;
                ctx[iterator].window_x = disp_info.window_width / 2;
                ctx[iterator].window_y = 0;
            }
            else if (iterator == 2)
            {
                ctx[iterator].window_width = disp_info.window_width / 2;
                ctx[iterator].window_height = disp_info.window_height / 2;
                ctx[iterator].window_x = 0;
                ctx[iterator].window_y = disp_info.window_height / 2;
            }
            else
            {
                ctx[iterator].window_width = disp_info.window_width / 2;
                ctx[iterator].window_height = disp_info.window_height / 2;
                ctx[iterator].window_x = disp_info.window_width / 2;
                ctx[iterator].window_y = disp_info.window_height / 2;
            }
        }

        pthread_create(&ctx[iterator].dec_capture_loop, NULL,
                dec_capture_loop_fcn, &ctx[iterator]);
        char capture_thread[16] = "CapturePlane";
        string s2 = to_string(iterator);
        strcat(capture_thread, s2.c_str());
        pthread_setname_np(ctx[iterator].dec_capture_loop, capture_thread);

        pthread_create(&ctx[iterator].dec_feed_handle, NULL,
                                dec_feed_loop_fcn, &ctx[iterator]);
        char output_thread[16] = "OutputPlane";
        string s3 = to_string(iterator);
        strcat(output_thread, s3.c_str());
        pthread_setname_np(ctx[iterator].dec_feed_handle,output_thread);
    }

cleanup:
#ifdef ENABLE_TRT
    pthread_join(trt_ctx.trt_handle, NULL);
#endif
    for (iterator = 0; iterator < cfg.channel_num; iterator++)
    {
        //send stop command to render, and wait it get consumed
        ctx[iterator].stop_render = 1;
        pthread_cond_broadcast(&ctx[iterator].render_cond);
        pthread_join(ctx[iterator].render_feed_handle, NULL);
        pthread_join(ctx[iterator].dec_feed_handle, NULL);

        if (ctx[iterator].dec->isInError())
        {
            cerr << "Decoder is in error" << endl;
            error = 1;
        }

        if (ctx[iterator].got_error)
        {
            error = 1;
        }

        // The decoder destructor does all the cleanup i.e set streamoff on output and capture planes,
        // unmap buffers, tell decoder to deallocate buffer (reqbufs ioctl with counnt = 0),
        // and finally call v4l2_close on the fd.
        delete ctx[iterator].dec;
        // Similarly, EglRenderer destructor does all the cleanup
        delete ctx[iterator].in_file;
        delete ctx[iterator].out_file;
        delete ctx[iterator].render_buf_queue;
        if (ctx[iterator].nvosd_context)
        {
            nvosd_destroy_context(ctx[iterator].nvosd_context);
            ctx[iterator].nvosd_context = NULL;
        }

        if (ctx[iterator].do_stat)
        {
            for( iter = ctx[iterator].frame_info_map->begin();
                    iter != ctx[iterator].frame_info_map->end(); iter++)
            {
                delete iter->second;
            }
        }
        delete ctx[iterator].frame_info_map;

        if (error)
        {
            cout << "App run failed" << endl;
        }
        else
        {
            cout << "App run was successful" << endl;
        }
    }
#ifdef ENABLE_TRT
    trt_ctx.tctx.destroyTrtContext();
#endif
    // Terminate EGL display connection
    if (egl_display)
    {
        if (!eglTerminate(egl_display))
        {
            cout<<"Error while terminate EGL display connection";
            return -1;
        }
    }

    return -error;
}
