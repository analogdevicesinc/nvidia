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

#ifndef _EGLSTREAM_FRAME_BUF_H
#define _EGLSTREAM_FRAME_BUF_H

namespace EGLStream
{

/**
 * FrameBuf objects are acquired and returned by a FrameConsumer, and correspond
 * to frames that have been written to the stream. FrameBufs contain metadata
 * corresponsing to the stream frame as well as the raw buffer data.
 */
class FrameBuf : public Argus::InterfaceProvider, public Argus::Destructable
{
protected:
    ~FrameBuf() {}
};

/**
 * @class IFrameBuf
 *
 * Interface that provides core access to a FrameBuf.
 */
DEFINE_UUID(Argus::InterfaceID, IID_FRAME_BUF, 431CC730,BA8C,11EC,BF84,08,00,20,0C,9A,66);
class IFrameBuf : public Argus::Interface
{
public:
    static const Argus::InterfaceID& id() { return IID_FRAME_BUF; }

    /**
     * Returns the frame number.
     */
    virtual uint64_t getNumber() const = 0;

    /**
     * Returns the timestamp of the frame, in nanoseconds.
     */
    virtual uint64_t getTime() const = 0;

    virtual Argus::Status loadInputImageFromFile(const char *fileName) = 0;
protected:
    ~IFrameBuf() {}
};

} // namespace EGLStream

#endif // _EGLSTREAM_FRAME_BUF_H
