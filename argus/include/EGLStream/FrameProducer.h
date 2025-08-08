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

#ifndef _EGLSTREAM_FRAME_PRODUCER_H
#define _EGLSTREAM_FRAME_PRODUCER_H

#include "FrameBuf.h"
#include "Argus/Types.h"

namespace EGLStream
{

/**
 * A FrameProducer object acts as a producer point to an InputStream or
 * EGLStream, provided during creation, and exposes interfaces to return
 * Frame objects that provide various image buffer related interfaces.
 *
 * The FrameProducer is responsible for allocating and managing all FrameBuf objects
 * that are used in the stream. and these FrameBuf objects will exist in one of three states:
 *
 *   Available: Unused FrameByuf objects that may be immediately retrieved for use with getFrame().
 *              No other components hold references to Frame objects in this state.
 *
 *   Pending:   FrameBuf objects that have been returned by getFrame() and are currently being used
 *               in libargus. FrameBuf objects in this state will be assigned to capture request
 *              and should not be modified by the FrameProducer until presented/aborted.
 *
 *   Presented: FrameBuf objects that have been presented to EGL Input Stream. FrameBuf objects
 *              in this state are being used by the EGLStream and/or the consumer, and should not 
 *              be modified until the frame is returned by EGL Input Stream, and FrameBuf will be
 *              locked while in this state.
 *
 * ===== FrameBuf State Diagram =====
 *
 *       FrameProducer::create()
 *           *
 *           |
 *           |  allocate and register FrameBuf
 *           |
 *           v
 *       +------------------------------------+
 *       |                                    |
 *       |        Available FrameBuf          |----+
 *       |                                    |    |
 *       +------------------------------------+    |
 *                         | ^                     |
 *            getFrame()   | | aborted FrameBuf    |
 *                         v |                     |
 *       +------------------------------------+    |
 *       |                                    |    |
 *       |        Pending FrameBuf            |    | getFrame()
 *       |                                    |    |  [unlocks FrameBuf]
 *       +------------------------------------+    |
 *                         |                       |
 *        presentFrame()   |                       |
 *         [locks buffer]  |                       |
 *                         v                       |
 *       +------------------------------------+    |
 *       |                                    |    |
 *       |        Presented FrameBuf          |----+
 *       |                                    |
 *       | These buffers have been presented  |
 *       |   to the EGLStream and will be     |
 *       |   returned by the consumer/EGL     |
 *       |                                    |
 *       +------------------------------------+
 */

/**
 * Destroying a Producer will disconnect the producer from the EGLStream, but
 * Frame objects returned by IFrameProducer::presentFrame will persist until
 * the application explicitly destroys those objects.
 */
class FrameProducer : public Argus::InterfaceProvider, public Argus::Destructable
{
public:
    /**
     * Creates a new FrameProducer to produce frames for an Argus InputStream.
     *
     * @param[in] inputStream The input stream to write into.
     * @param[in] phase The input stream raw bayer data phase.
     * @param[out] status An optional pointer to return an error status code.
     *
     * @returns A new FrameProducer object, or NULL on error.
     */
    static FrameProducer* create(Argus::InputStream* inputStream,
                                 const Argus::BayerPhase &phase,
                                 Argus::Status* status = NULL);

    /**
     * Creates a new FrameProducer to write frames into an EGLStream.
     *
     * @param[in] eglDisplay The EGLDisplay the stream belongs to.
     * @param[in] eglStream The EGLStream to connect to.
     * @param[in] size The EGLStream buffer size.
     * @param[in] format The EGLStream buffer pixel format.
     * @param[in] phase The input stream raw bayer data phase.
     * @param[out] status An optional pointer to return an error status code.
     *
     * @returns A new FrameProducer object, or NULL on error.
     */
    static FrameProducer* create(EGLDisplay eglDisplay,
                                 EGLStreamKHR eglStream,
                                 const Argus::Size2D<uint32_t> &size,
                                 const Argus::PixelFormat &format,
                                 const Argus::BayerPhase &phase,
                                 Argus::Status* status = NULL);
protected:
    ~FrameProducer() {}
};

/**
 * @class IFrameProducer
 *
 * Exposes the methods used to present Frames from a FrameProducer.
 */
DEFINE_UUID(Argus::InterfaceID, IID_FRAME_PRODUCER, b94a7bd1,c3c8,11e5,a837,08,00,20,0c,9a,66);
class IFrameProducer : public Argus::Interface
{
public:
    static const Argus::InterfaceID& id() { return IID_FRAME_PRODUCER; }

    /**
     * Get a new frame from the EGL Stream, returning a Frame object. This Frame object
     * behaves as its own entity, and may persist even after the FrameProducer is destroyed.
     * It is the application's responsibility to destroy any Frame returned by this method.
     *
     * Destroying a Frame causes all resources held by that frame to be returned to the EGLStream
     * so that they may be used to produce another frame. If too many Frames are held
     * by the producer, or these frames are presented at a slower rate than the consumer is
     * consuming frames, it may be possible to stall the producer. Frame objects should always be
     * be destroyed as soon as possible to minimize resource overhead.
     *
     * If NULL is returned and the status code is STATUS_DISCONNECTED, the producer has
     * disconnected from the stream and no more frames can ever be acquired from this consumer.
     *
     * @param[in] timeout The timeout (in nanoseconds) to wait for a frame if one isn't available.
     * @param[out] status An optional pointer to return an error status code.
     *
     * @returns A pointer to the frame acquired from the stream, or NULL on error.
     */
    virtual  Argus::Status getFrame(FrameBuf** frame,
                                    uint64_t timeout = Argus::TIMEOUT_INFINITE) = 0;

    /**
     * Presents a pending buffer to the EGLStream.
     *
     * @param[in] frame The buffer to present. This must have been previously
     *                  returned by getFrame.
     * @returns success/status of this call.
     */
    virtual Argus::Status presentFrame(FrameBuf *frame) = 0;

    /**
     * Return a aborted buffer to the free queue.
     *
     * @param[in] buffer The buffer to return. This must have been previously
     *                   returned by getFrame.
     */
    virtual Argus::Status returnAbortedFrame(FrameBuf *frame) = 0;
protected:
    ~IFrameProducer() {}
};

} // namespace EGLStream

#endif // _EGLSTREAM_FRAME_PRODUCER_H
