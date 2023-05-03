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

/**
 * @file
 * <b>NVIDIA Multimedia API: nvbufsurface Wrapper Class</b>
 *
 * @b Description: This file declares a wrapper class for nvbufsurface.
 * @{
 */

#ifndef __NV_BUF_SURFACE_H__
#define __NV_BUF_SURFACE_H__

#include <errno.h>
#include <fstream>
#include <iostream>
#include <linux/videodev2.h>
#include <malloc.h>
#include <pthread.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>

#include "nvbufsurface.h"
#include "nvbufsurftransform.h"

/**
 * @brief This derived class defines a wrapper class for nvbufsurface and nvbuftransform.
 *
 * Defines a helper class that provides common functionality for nvbufsurface.
 * It includes the file descriptor (FD) of the device
 * and other helper methods, such as Destroy nvbufsurface,
 * create nvbufsurface, buffer transform.
 */
class NvBufSurf
{
public:
    typedef struct {
      /** width of source rectangle coordinates of input buffers for transformation. */
      uint32_t src_width;
      /** height of source rectangle coordinates of input buffers for transformation. */
      uint32_t src_height;
      /** top of source rectangle coordinates of input buffers for transformation. */
      uint32_t src_top;
      /** left of source rectangle coordinates of input buffers for transformation. */
      uint32_t src_left;
      /** width of destination rectangle coordinates of input buffers for transformation. */
      uint32_t dst_width;
      /** height of destination rectangle coordinates of input buffers for transformation. */
      uint32_t dst_height;
      /** top of destination rectangle coordinates of input buffers for transformation. */
      uint32_t dst_top;
      /** left of destination rectangle coordinates of input buffers for transformation. */
      uint32_t dst_left;
      /** flag to indicate which of the transform parameters are valid. */
      NvBufSurfTransform_Transform_Flag flag;
      /** flip method. */
      NvBufSurfTransform_Flip flip;
      /** transform filter. */
      NvBufSurfTransform_Inter filter;
    } NvCommonTransformParams;

    typedef struct {
      /** Holds the width of the buffer. */
      uint32_t width;
      /** Holds the height of the buffer. */
      uint32_t height;
      /** Holds the color format of the buffer. */
      NvBufSurfaceColorFormat colorFormat;
      /** Holds the surface layout. May be Block Linear (BL) or Pitch Linear (PL).
       For a dGPU, only PL is valid. */
      NvBufSurfaceLayout layout;
      /** Holds the type of memory to be allocated. */
      NvBufSurfaceMemType memType;
      /** Holds tag to associate with the buffer. */
      NvBufSurfaceTag memtag;
    } NvCommonAllocateParams;

    /* Destroys a hardware buffer. */
    static int NvDestroy(int fd);
    /* Allocates a hardware buffer */
    static int NvAllocate(NvCommonAllocateParams *allocateParams, uint32_t numBuffers, int *fd);
    /* Transforms one DMA buffer to another DMA buffer. */
    static int NvTransform(NvCommonTransformParams *transformParams, int src_fd, int dst_fd);
    static int NvTransformAsync(NvCommonTransformParams *transformParams, NvBufSurfTransformSyncObj_t *sync_obj, int src_fd, int dst_fd);
};

/** @} */
#endif
