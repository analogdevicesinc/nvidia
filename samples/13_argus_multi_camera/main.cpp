/*
 * Copyright (c) 2017-2024, NVIDIA CORPORATION. All rights reserved.
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

#include "Error.h"
#include "Thread.h"

#include <Argus/Argus.h>
#include <EGLStream/EGLStream.h>
#include <EGLStream/NV/ImageNativeBuffer.h>

#include "NvBufSurface.h"
#include <NvEglRenderer.h>

#include <stdio.h>
#include <stdlib.h>
#include <sys/un.h>
#include <sys/socket.h>

using namespace Argus;
using namespace EGLStream;

/* Constants */
static const uint32_t            MAX_CAMERA_NUM = 6;
static const uint32_t            DEFAULT_FRAME_COUNT = 100;
static const uint32_t            DEFAULT_FPS = 30;
static const Size2D<uint32_t>    STREAM_SIZE(640, 480);
static const uint32_t            PACKET_SIZE = 768;

typedef enum
{
  MODE_SINGLE         = 0,
  MODE_DUAL_SENDER    = 1,
  MODE_DUAL_RECEIVER  = 2
} OpMode;

/* Globals */
uint32_t                   g_stream_num = MAX_CAMERA_NUM;
uint32_t                   g_frame_count = DEFAULT_FRAME_COUNT;
uint32_t                   g_mode = MODE_SINGLE;

#define FD_SOCKET_PATH      "/tmp/fd-share.socket"
/* Debug print macros */
#define RENDERER_PRINT(...) printf("RENDERER: " __VA_ARGS__)
#define CONSUMER_PRINT(...) printf("CONSUMER: " __VA_ARGS__)

namespace ArgusSamples
{

/* An utility class to hold all resources of one capture session */
class CaptureHolder : public Destructable
{
public:
    explicit CaptureHolder();
    virtual ~CaptureHolder();

    bool initialize(CameraDevice *device,
                    ICameraProvider *iCameraProvider,
                    NvEglRenderer *renderer);

    CaptureSession* getSession() const
    {
        return m_captureSession.get();
    }

    OutputStream* getStream() const
    {
        return m_outputStream.get();
    }

    Request* getRequest() const
    {
        return m_request.get();
    }

    virtual void destroy()
    {
        delete this;
    }

private:
    UniqueObj<CaptureSession> m_captureSession;
    UniqueObj<OutputStream> m_outputStream;
    UniqueObj<Request> m_request;
};

CaptureHolder::CaptureHolder()
{
}

CaptureHolder::~CaptureHolder()
{
    /* Destroy the output stream */
    m_outputStream.reset();
}

bool CaptureHolder::initialize(CameraDevice *device,
                               ICameraProvider *iCameraProvider,
                               NvEglRenderer *renderer)
{
    if (!iCameraProvider)
        ORIGINATE_ERROR("Failed to get ICameraProvider interface");

    /* Create the capture session using the first device and get the core interface */
    m_captureSession.reset(iCameraProvider->createCaptureSession(device));
    ICaptureSession *iCaptureSession = interface_cast<ICaptureSession>(m_captureSession);
    IEventProvider *iEventProvider = interface_cast<IEventProvider>(m_captureSession);
    if (!iCaptureSession || !iEventProvider)
        ORIGINATE_ERROR("Failed to create CaptureSession");

    /* Create the OutputStream */
    UniqueObj<OutputStreamSettings> streamSettings(
        iCaptureSession->createOutputStreamSettings(STREAM_TYPE_EGL));
    IEGLOutputStreamSettings *iEglStreamSettings =
        interface_cast<IEGLOutputStreamSettings>(streamSettings);
    if (!iEglStreamSettings)
        ORIGINATE_ERROR("Failed to create EglOutputStreamSettings");

    iEglStreamSettings->setPixelFormat(PIXEL_FMT_YCbCr_420_888);
    if (g_mode == MODE_SINGLE)
        iEglStreamSettings->setEGLDisplay(renderer->getEGLDisplay());
    else
        iEglStreamSettings->setEGLDisplay(EGL_NO_DISPLAY);
    iEglStreamSettings->setResolution(STREAM_SIZE);

    m_outputStream.reset(iCaptureSession->createOutputStream(streamSettings.get()));

    /* Create capture request and enable the output stream */
    m_request.reset(iCaptureSession->createRequest());
    IRequest *iRequest = interface_cast<IRequest>(m_request);
    if (!iRequest)
        ORIGINATE_ERROR("Failed to create Request");
    iRequest->enableOutputStream(m_outputStream.get());

    ISourceSettings *iSourceSettings =
            interface_cast<ISourceSettings>(iRequest->getSourceSettings());
    if (!iSourceSettings)
        ORIGINATE_ERROR("Failed to get ISourceSettings interface");
    iSourceSettings->setFrameDurationRange(Range<uint64_t>(1e9/DEFAULT_FPS));

    return true;
}


/*
 * Argus Consumer Thread:
 * This is the thread acquires buffers from each stream and composite them to
 * one frame. Finally it renders the composited frame through EGLRenderer.
 */
class ConsumerThread : public Thread
{
public:
    explicit ConsumerThread(NvEglRenderer *renderer,
                            std::vector<OutputStream*> &streams) :
        m_streams(streams),
        m_framesRemaining(g_frame_count),
        m_compositedFrame(0),
        m_renderer(renderer)
    {
    }
    virtual ~ConsumerThread();

protected:
    /** @name Thread methods */
    /**@{*/
    virtual bool threadInitialize();
    virtual bool threadExecute();
    virtual bool threadShutdown();
    /**@}*/

    std::vector<OutputStream*> &m_streams;
    uint32_t m_framesRemaining;
    UniqueObj<FrameConsumer> m_consumers[MAX_CAMERA_NUM];
    int m_dmabufs[MAX_CAMERA_NUM];
    NvBufSurfTransformCompositeBlendParamsEx m_compositeParam;
    int m_compositedFrame;
    NvBufSurface *pdstSurf;
    NvEglRenderer *m_renderer;

private:
    bool initSocket();
    void sendFd(int *fds, int n);
    void sendNvBufPar(NvBufSurfaceMapParams buf_par, bool lastBuf);
    void waitForAck();

    int m_sfd;
    int m_cfd;
};

ConsumerThread::~ConsumerThread()
{
    if (m_compositeParam.src_comp_rect)
        free(m_compositeParam.src_comp_rect);
    if (m_compositeParam.dst_comp_rect)
        free(m_compositeParam.dst_comp_rect);
    if (m_compositedFrame) {
        NvBufSurf::NvDestroy(m_compositedFrame);
        m_compositedFrame = 0;
    }

    for (uint32_t i = 0; i < m_streams.size(); i++) {
        if (m_dmabufs[i]) {
            NvBufSurf::NvDestroy(m_dmabufs[i]);
            m_dmabufs[i] = 0;
        }
    }

    if (g_mode == MODE_DUAL_SENDER)
    {
        if (close(m_cfd) == -1)
            CONSUMER_PRINT("Failed to close client socket");

        close(m_sfd);

        if (unlink(FD_SOCKET_PATH) == -1 && errno != ENOENT)
            CONSUMER_PRINT("Removing socket file failed");
    }
}

bool ConsumerThread::threadInitialize()
{
    if (g_mode == MODE_DUAL_SENDER)
    {
        if (initSocket() == false)
            return false;
    }

    NvBufSurfTransformRect dstCompRect[6];
    int32_t spacing = 10;
    NvBufSurf::NvCommonAllocateParams input_params = {0};

    // Initialize destination composite rectangles
    // The window layout is as below
    // +-------------------------------+-----------------------------------+
    // |                                                                   |
    // |                                    +-------+      +-------+       |
    // |                                    |       |      |       |       |
    // |     Frame 0                        |Frame1 |      |Frame2 |       |
    // |                                    |       |      |       |       |
    // |                                    +-------+      +-------+       |
    // |                                                                   |
    // |                                    +-------+      +-------+       |
    // |                                    |       |      |       |       |
    // |                                    |Frame3 |      |Frame4 |       |
    // |                                    |       |      |       |       |
    // |                                    +-------+      +-------+       |
    // |                                                                   |
    // |                                    +-------+                      |
    // |                                    |       |                      |
    // |                                    |Frame5 |                      |
    // |                                    |       |                      |
    // |                                    +-------+                      |
    // |                                                                   |
    // +-------------------------------+-----------------------------------+
    int32_t cellWidth = (STREAM_SIZE.width() / 2 - spacing * 3) / 2;
    int32_t cellHeight = (STREAM_SIZE.height() - spacing * 4) / 3;

    dstCompRect[0].top  = 0;
    dstCompRect[0].left = 0;
    dstCompRect[0].width = STREAM_SIZE.width();
    dstCompRect[0].height = STREAM_SIZE.height();

    dstCompRect[1].top  = spacing;
    dstCompRect[1].left = STREAM_SIZE.width() / 2 + spacing;
    dstCompRect[1].width = cellWidth;
    dstCompRect[1].height = cellHeight;

    dstCompRect[2].top  = spacing;
    dstCompRect[2].left = STREAM_SIZE.width() / 2 + cellWidth + spacing * 2;
    dstCompRect[2].width = cellWidth;
    dstCompRect[2].height = cellHeight;

    dstCompRect[3].top  = cellHeight + spacing * 2;
    dstCompRect[3].left = STREAM_SIZE.width() / 2 + spacing;
    dstCompRect[3].width = cellWidth;
    dstCompRect[3].height = cellHeight;

    dstCompRect[4].top  = cellHeight + spacing * 2;
    dstCompRect[4].left = STREAM_SIZE.width() / 2 + cellWidth + spacing * 2;
    dstCompRect[4].width = cellWidth;
    dstCompRect[4].height = cellHeight;

    dstCompRect[5].top  = cellHeight * 2 + spacing * 3;
    dstCompRect[5].left = STREAM_SIZE.width() / 2 + spacing;
    dstCompRect[5].width = cellWidth;
    dstCompRect[5].height = cellHeight;

    /* Allocate composited buffer */
    input_params.width = STREAM_SIZE.width();
    input_params.height = STREAM_SIZE.height();
    input_params.colorFormat = NVBUF_COLOR_FORMAT_NV12;
    input_params.layout = NVBUF_LAYOUT_PITCH;
    input_params.memType = NVBUF_MEM_SURFACE_ARRAY;
    input_params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;

    if(-1 == NvBufSurf::NvAllocate(&input_params, 1, &m_compositedFrame))
        ORIGINATE_ERROR("Failed to create NvBuffer");

    if (!m_compositedFrame)
        ORIGINATE_ERROR("Failed to allocate composited buffer");

    if (-1 == NvBufSurfaceFromFd(m_compositedFrame, (void**)(&pdstSurf)))
        ORIGINATE_ERROR("Cannot get NvBufSurface from fd");

    /* Initialize composite parameters */
    memset(&m_compositeParam, 0, sizeof(m_compositeParam));
    m_compositeParam.params.composite_blend_flag = NVBUFSURF_TRANSFORM_COMPOSITE;
    m_compositeParam.params.input_buf_count = (m_streams.size()<6) ? m_streams.size() : 6;
    m_compositeParam.params.composite_blend_filter = NvBufSurfTransformInter_Algo3;
    m_compositeParam.dst_comp_rect = static_cast<NvBufSurfTransformRect*>
                  (malloc(sizeof(NvBufSurfTransformRect) * 6));
    m_compositeParam.src_comp_rect = static_cast<NvBufSurfTransformRect*>
                  (malloc(sizeof(NvBufSurfTransformRect)
                  * m_compositeParam.params.input_buf_count));
    memcpy(m_compositeParam.dst_comp_rect, &dstCompRect[0],
                sizeof(NvBufSurfTransformRect) * 6);
    for (uint32_t i = 0; i < m_compositeParam.params.input_buf_count; i++)
    {
        m_compositeParam.src_comp_rect[i].top = 0;
        m_compositeParam.src_comp_rect[i].left = 0;
        m_compositeParam.src_comp_rect[i].width = STREAM_SIZE.width();
        m_compositeParam.src_comp_rect[i].height = STREAM_SIZE.height();
    }

    /* Initialize buffer handles. Buffer will be created by FrameConsumer */
    memset(m_dmabufs, 0, sizeof(m_dmabufs));

    /* Create the FrameConsumer */
    for (uint32_t i = 0; i < m_streams.size(); i++)
    {
        m_consumers[i].reset(FrameConsumer::create(m_streams[i]));
    }

    return true;
}

bool ConsumerThread::threadExecute()
{
    IEGLOutputStream *iEglOutputStreams[MAX_CAMERA_NUM];
    IFrameConsumer *iFrameConsumers[MAX_CAMERA_NUM];
    int render_fd = 0;

    for (uint32_t i = 0; i < m_streams.size(); i++)
    {
        iEglOutputStreams[i] = interface_cast<IEGLOutputStream>(m_streams[i]);
        iFrameConsumers[i] = interface_cast<IFrameConsumer>(m_consumers[i]);
        if (!iFrameConsumers[i])
            ORIGINATE_ERROR("Failed to get IFrameConsumer interface");

        /* Wait until the producer has connected to the stream */
        CONSUMER_PRINT("Waiting until producer is connected...\n");
        if (iEglOutputStreams[i]->waitUntilConnected() != STATUS_OK)
            ORIGINATE_ERROR("Stream failed to connect.");
        CONSUMER_PRINT("Producer has connected; continuing.\n");
    }

    NvBufSurface ** batch_surf = new NvBufSurface*[m_streams.size()];

    while (m_framesRemaining--)
    {
        for (uint32_t i = 0; i < m_streams.size(); i++)
        {
            /* Acquire a frame */
            UniqueObj<Frame> frame(iFrameConsumers[i]->acquireFrame());
            IFrame *iFrame = interface_cast<IFrame>(frame);
            if (!iFrame)
                break;

            /* Get the IImageNativeBuffer extension interface */
            NV::IImageNativeBuffer *iNativeBuffer =
                interface_cast<NV::IImageNativeBuffer>(iFrame->getImage());
            if (!iNativeBuffer)
            {
                delete [] batch_surf;
                ORIGINATE_ERROR("IImageNativeBuffer not supported by Image.");
            }

            /* If we don't already have a buffer, create one from this image.
               Otherwise, just blit to our buffer */
            if (!m_dmabufs[i])
            {
                batch_surf[i] = NULL;
                m_dmabufs[i] = iNativeBuffer->createNvBuffer(iEglOutputStreams[i]->getResolution(),
                                                          NVBUF_COLOR_FORMAT_YUV420,
                                                          NVBUF_LAYOUT_BLOCK_LINEAR);
                if (!m_dmabufs[i])
                    CONSUMER_PRINT("\tFailed to create NvBuffer\n");
                if (-1 == NvBufSurfaceFromFd(m_dmabufs[i], (void**)(&batch_surf[i])))
                {
                    delete [] batch_surf;
                    ORIGINATE_ERROR("Cannot get NvBufSurface from fd");
                }
            }
            else if (iNativeBuffer->copyToNvBuffer(m_dmabufs[i]) != STATUS_OK)
            {
                delete [] batch_surf;
                ORIGINATE_ERROR("Failed to copy frame to NvBuffer.");
            }
        }

        CONSUMER_PRINT("Render frame %d\n", g_frame_count - m_framesRemaining);
        if (m_streams.size() > 1)
        {
            /* Composite multiple input to one frame */
            NvBufSurfTransformMultiInputBufCompositeBlend(batch_surf, pdstSurf, &m_compositeParam);
            render_fd = m_compositedFrame;
        }
        else
            render_fd = m_dmabufs[0];

        if (g_mode == MODE_DUAL_SENDER)
        {
            NvBufSurface *nvbuf_surf = 0;
            NvBufSurfaceMapParams buf_par = {0};

            NvBufSurfaceFromFd(render_fd, (void**)(&nvbuf_surf));
            NvBufSurfaceGetMapParams(nvbuf_surf, 0, &buf_par);
            sendFd(&render_fd, 1);
            sendNvBufPar(buf_par, (m_framesRemaining == 0));
            waitForAck();
        }
        else // MODE_SINGLE
            m_renderer->render(render_fd);

    }

    delete [] batch_surf;

    CONSUMER_PRINT("Done.\n");

    requestShutdown();

    return true;
}

bool ConsumerThread::threadShutdown()
{
    return true;
}

bool ConsumerThread::initSocket()
{
    struct sockaddr_un addr;

    if (unlink(FD_SOCKET_PATH) == -1 && errno != ENOENT)
        ORIGINATE_ERROR("Removing socket file failed");

    m_sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_sfd == -1)
        ORIGINATE_ERROR("Failed to create socket");

    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, FD_SOCKET_PATH, sizeof(addr.sun_path) - 1);
    if (bind(m_sfd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un)) == -1)
        ORIGINATE_ERROR("Failed to bind to socket");

    if (listen(m_sfd, 2) == -1)
        ORIGINATE_ERROR("Failed to listen on socket");

    m_cfd = accept(m_sfd, NULL, NULL);

    return true;
}

void ConsumerThread::sendFd(int *fds, int n)
{
    char buf[CMSG_SPACE(n * sizeof(int))], data[256];
    memset(buf, '\0', sizeof(buf));

    struct iovec io = {
        iov_base : &data,
        iov_len : sizeof(data)
    };

    struct msghdr msg = {0};
    msg.msg_iov = &io;
    msg.msg_iovlen = 1;
    msg.msg_control = buf;
    msg.msg_controllen = sizeof(buf);

    struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(n * sizeof(int));

    memcpy((int *) CMSG_DATA(cmsg), fds, n * sizeof(int));

    if (sendmsg(m_cfd, &msg, 0) < 0)
        CONSUMER_PRINT("Failed to send message\n");
}

void ConsumerThread::sendNvBufPar(NvBufSurfaceMapParams buf_par,
                                  bool lastBuf) {
    char data[PACKET_SIZE];
    uint32_t parSize;

    parSize = sizeof(NvBufSurfaceMapParams);
    if ((parSize + 1) > PACKET_SIZE)
    {
        CONSUMER_PRINT("packet size exteeds data size\n");
        return;
    }
    memset(data, 0, sizeof(data));
    memcpy(data, &buf_par, parSize);
    if (lastBuf) // notify this is last buffer
        data[parSize] = 1;
    if (send(m_cfd, data, sizeof(data), 0) < 0)
        CONSUMER_PRINT("Failed to send params\n");
}

void ConsumerThread::waitForAck()
{
    char data[PACKET_SIZE];
    memset(data, 0, sizeof(data));
    if (recv(m_cfd, data, sizeof(data), 0) < 0)
        CONSUMER_PRINT("Failed to receive ack\n");
}


/*
 * Argus Producer Thread:
 * Open the Argus camera driver and detect how many camera devices available.
 * Create one OutputStream for each camera device. Launch consumer thread
 * and then submit FRAME_COUNT capture requests.
 */
static bool execute()
{
    UniqueObj<CameraProvider> cameraProvider;
    NvEglRenderer *renderer = NULL;

    if (g_mode == MODE_SINGLE)
    {
        /* Initialize EGL renderer */
        renderer = NvEglRenderer::createEglRenderer("renderer0", STREAM_SIZE.width(),
                                                STREAM_SIZE.height(), 0, 0);
        if (!renderer)
            ORIGINATE_ERROR("Failed to create EGLRenderer.");
    }

    /* Initialize the Argus camera provider */
    cameraProvider = UniqueObj<CameraProvider>(CameraProvider::create());
    ICameraProvider *iCameraProvider = interface_cast<ICameraProvider>(cameraProvider);
    if (!iCameraProvider)
        ORIGINATE_ERROR("Failed to get ICameraProvider interface");
    printf("Argus Version: %s\n", iCameraProvider->getVersion().c_str());

    /* Get the camera devices */
    std::vector<CameraDevice*> cameraDevices;
    iCameraProvider->getCameraDevices(&cameraDevices);
    if (cameraDevices.size() == 0)
        ORIGINATE_ERROR("No cameras available");

    UniqueObj<CaptureHolder> captureHolders[MAX_CAMERA_NUM];
    uint32_t streamCount = cameraDevices.size() < MAX_CAMERA_NUM ?
            cameraDevices.size() : MAX_CAMERA_NUM;
    if (streamCount > g_stream_num)
        streamCount = g_stream_num;
    for (uint32_t i = 0; i < streamCount; i++)
    {
        captureHolders[i].reset(new CaptureHolder);
        if (!captureHolders[i].get()->initialize(cameraDevices[i], iCameraProvider, renderer))
            ORIGINATE_ERROR("Failed to initialize Camera session %d", i);

    }

    std::vector<OutputStream*> streams;
    for (uint32_t i = 0; i < streamCount; i++)
        streams.push_back(captureHolders[i].get()->getStream());

    /* Start the rendering thread */
    ConsumerThread consumerThread(renderer, streams);
    PROPAGATE_ERROR(consumerThread.initialize());
    PROPAGATE_ERROR(consumerThread.waitRunning());

    /* Submit capture requests */
    for (uint32_t i = 0; i < g_frame_count; i++)
    {
        for (uint32_t j = 0; j < streamCount; j++)
        {
            ICaptureSession *iCaptureSession =
                    interface_cast<ICaptureSession>(captureHolders[j].get()->getSession());
            Request *request = captureHolders[j].get()->getRequest();
            uint32_t frameId = iCaptureSession->capture(request);
            if (frameId == 0)
                ORIGINATE_ERROR("Failed to submit capture request");
        }
    }

    /* Wait for idle */
    for (uint32_t i = 0; i < streamCount; i++)
    {
        ICaptureSession *iCaptureSession =
            interface_cast<ICaptureSession>(captureHolders[i].get()->getSession());
        iCaptureSession->waitForIdle();
    }

    /* Destroy the capture resources */
    for (uint32_t i = 0; i < streamCount; i++)
    {
        captureHolders[i].reset();
    }

    /* Wait for the rendering thread to complete */
    PROPAGATE_ERROR(consumerThread.shutdown());

    /* Shut down Argus */
    cameraProvider.reset();

    /* Cleanup EGL Renderer */
    if (renderer)
        delete renderer;

    return true;
}

}; /* namespace ArgusSamples */

/*
 * Frame Receiver Process
 */
class FrameReceiver
{
public:
    int run();

private:
    uint8_t lastBuf;
    int m_sfd;

    void recvFd(int *fds, int n)
    {
        char buf[CMSG_SPACE(n * sizeof(int))], data[256];
        memset(buf, '\0', sizeof(buf));
        memset(data, '\0', sizeof(data));

        struct iovec io = {
            .iov_base=&data,
            .iov_len=sizeof(data)
        };

        struct msghdr msg = {0};
        msg.msg_iov = &io;
        msg.msg_iovlen = 1;
        msg.msg_control = buf;
        msg.msg_controllen = sizeof(buf);

        if (recvmsg(m_sfd, &msg, 0) < 0)
        {
            RENDERER_PRINT("Failed to receive message\n");
            return;
        }

        struct cmsghdr *cmsg = CMSG_FIRSTHDR(&msg);
        memcpy(fds, (int *) CMSG_DATA(cmsg), n * sizeof(int));
    }

    void recvNvBufPar(NvBufSurfaceMapParams *buf_par) {
        char data[PACKET_SIZE];
        uint32_t parSize;

        parSize = sizeof(NvBufSurfaceMapParams);
        memset(data, 0, sizeof(data));
        if (recv(m_sfd, data, sizeof(data), 0) < 0)
        {
            RENDERER_PRINT("Failed to receive params\n");
            return;
        }

        memcpy((char *)buf_par, data, parSize);
        lastBuf = data[parSize];
    }

    void sendAck()
    {
        char data[PACKET_SIZE];
        memset(data, 0, sizeof(data));
        snprintf(data, sizeof(data), "ack from pid: %u", getpid());

        if (send(m_sfd, data, sizeof(data), 0) < 0)
            RENDERER_PRINT("Failed to send ack\n");
    }

} ;

int FrameReceiver::run(void)
{
    int retry = 0;
    struct sockaddr_un addr;
    NvBufSurface *nvbuf_surf = 0;
    NvBufSurfaceMapParams buf_par;
    int received_fd = 0;
    int ret;
    NvEglRenderer *renderer;

    m_sfd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (m_sfd < 0)
    {
        RENDERER_PRINT("Failed to create socket\n");
        return -1;
    }

    memset(&addr, 0, sizeof(struct sockaddr_un));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, FD_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    do {
        usleep(200000); // wait for sneder at accept()
        ret = connect(m_sfd, (struct sockaddr *) &addr, sizeof(struct sockaddr_un));
        if (ret < 0)
            retry++;
    } while ((ret < 0) && (retry < 10));
    if (ret < 0)
    {
        RENDERER_PRINT("Failed to connect to socket");
        return -1;
    }

    /* Initialize EGL renderer */
    renderer = NvEglRenderer::createEglRenderer("renderer0", STREAM_SIZE.width(),
                                            STREAM_SIZE.height(), 0, 0);

    lastBuf = 0;
    while (1)
    {
        recvFd(&received_fd, 1);
        RENDERER_PRINT("Received fd = %d\n", received_fd);
        recvNvBufPar(&buf_par);

        /* import the buffer */
        buf_par.fd = received_fd;
        NvBufSurfaceImport(&nvbuf_surf, &buf_par);
        nvbuf_surf->numFilled = 1;

        /* render the buffer */
        renderer->render(received_fd);

        if (received_fd)
            NvBufSurf::NvDestroy(received_fd);

        RENDERER_PRINT("Sending ack\n");
        sendAck();

        if (lastBuf == 1)
        {
            RENDERER_PRINT("Received last frame\n");
            break;
        }
    }
    close(m_sfd);

    /* Cleanup EGL Renderer */
    delete renderer;

    return 0;
}

static void printHelp()
{
    printf("Usage: multi_camera [OPTIONS]\n"
           "Examples:\n"
           "$ ./argus_multi_camera\n"
           "$ ./argus_multi_camera -m 1 & ./argus_multi_camera -m 2 &\n"
           "Options:\n"
           "  -n <num>      Max number of output streams (1 to 6)\n"
           "  -c <count>    Total frame count\n"
           "  -m <mode>     Operation mode\n"
           "                0: Single process\n"
           "                1: Dual processes as sender\n"
           "                2: Dual processes as receiver\n"
           "  -h            Print this help\n");
}

static bool parseCmdline(int argc, char * argv[])
{
    int c;
    while ((c = getopt(argc, argv, "n:c:m:h")) != -1)
    {
        switch (c)
        {
            case 'n':
                g_stream_num = atoi(optarg);
                if (g_stream_num < 1 || g_stream_num > MAX_CAMERA_NUM)
                {
                    printf("Invalid number of streams\n");
                    return false;
                }
                break;
            case 'c':
                g_frame_count = atoi(optarg);
                if (g_frame_count < 1)
                {
                    printf("Invalid frame count\n");
                    return false;
                }
                break;
            case 'm':
                g_mode = atoi(optarg);
                if (g_mode > 2)
                {
                    printf("Invalid mode\n");
                    return false;
                }
                break;
            default:
                return false;
        }
    }
    return true;
}

int main(int argc, char * argv[])
{
    if (!parseCmdline(argc, argv))
    {
        printHelp();
        return EXIT_FAILURE;
    }

    if (g_mode == MODE_DUAL_RECEIVER)
    {
        FrameReceiver receiverProc;
        receiverProc.run();
    }
    else
    {
        if (!ArgusSamples::execute())
            return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
