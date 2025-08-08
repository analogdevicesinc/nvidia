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
#include "NvBuffer.h"
#include "NvLogging.h"
#include <fstream>
#include <sstream>
#include <string>
#include "nvbufsurface.h"

int
read_video_frame(std::ifstream * stream, NvBuffer & buffer)
{
    uint32_t i, j;
    char *data;

    for (i = 0; i < buffer.n_planes; i++)
    {
        NvBuffer::NvBufferPlane &plane = buffer.planes[i];
        std::streamsize bytes_to_read =
            plane.fmt.bytesperpixel * plane.fmt.width;
        data = (char *) plane.data;
        plane.bytesused = 0;
        for (j = 0; j < plane.fmt.height; j++)
        {
            stream->read(data, bytes_to_read);
            if (stream->gcount() < bytes_to_read)
                return -1;
            data += plane.fmt.stride;
        }
        plane.bytesused = plane.fmt.stride * plane.fmt.height;
    }
    return 0;
}

int
write_video_frame(std::ofstream * stream, NvBuffer &buffer)
{
    uint32_t i, j;
    char *data;

    for (i = 0; i < buffer.n_planes; i++)
    {
        NvBuffer::NvBufferPlane &plane = buffer.planes[i];
        size_t bytes_to_write =
            plane.fmt.bytesperpixel * plane.fmt.width;

        data = (char *) plane.data;
        for (j = 0; j < plane.fmt.height; j++)
        {
            stream->write(data, bytes_to_write);
            if (!stream->good())
                return -1;
            data += plane.fmt.stride;
        }
    }
    return 0;
}

int
read_dmabuf(int dmabuf_fd,
                unsigned int plane,
                std::ifstream * stream)
{
    if (dmabuf_fd <= 0)
        return -1;

    int ret = -1;

    NvBufSurface *nvbuf_surf = 0;
    ret = NvBufSurfaceFromFd(dmabuf_fd, (void**)(&nvbuf_surf));
    if (ret != 0)
    {
        return -1;
    }
    NvBufSurfaceMap(nvbuf_surf, 0, plane, NVBUF_MAP_READ_WRITE);
    NvBufSurfaceSyncForCpu (nvbuf_surf, 0, plane);
    for (uint i = 0; i < nvbuf_surf->surfaceList->planeParams.height[plane]; ++i)
    {
        stream->read((char *)nvbuf_surf->surfaceList->mappedAddr.addr[plane] + i * nvbuf_surf->surfaceList->planeParams.pitch[plane],
                      nvbuf_surf->surfaceList->planeParams.width[plane] * nvbuf_surf->surfaceList->planeParams.bytesPerPix[plane]);
        if (!stream->good()) {
            ret = NvBufSurfaceUnMap(nvbuf_surf, 0, plane);
            if (ret < 0)
            {
                printf("Error while Unmapping buffer\n");
                return ret;
            }
            return -1;
	}
    }
    NvBufSurfaceSyncForDevice (nvbuf_surf, 0, plane);
    ret = NvBufSurfaceUnMap(nvbuf_surf, 0, plane);
    if (ret < 0)
    {
        printf("Error while Unmapping buffer\n");
        return ret;
    }
    return 0;
}

int
dump_dmabuf(int dmabuf_fd,
                unsigned int plane,
                std::ofstream * stream)
{
    if (dmabuf_fd <= 0)
        return -1;

    int ret = -1;

    NvBufSurface *nvbuf_surf = 0;
    ret = NvBufSurfaceFromFd(dmabuf_fd, (void**)(&nvbuf_surf));
    if (ret != 0)
    {
        return -1;
    }
    ret = NvBufSurfaceMap(nvbuf_surf, 0, plane, NVBUF_MAP_READ_WRITE);
    if (ret < 0)
    {
        printf("NvBufSurfaceMap failed\n");
        return ret;
    }
    NvBufSurfaceSyncForCpu (nvbuf_surf, 0, plane);
    for (uint i = 0; i < nvbuf_surf->surfaceList->planeParams.height[plane]; ++i)
    {
        stream->write((char *)nvbuf_surf->surfaceList->mappedAddr.addr[plane] + i * nvbuf_surf->surfaceList->planeParams.pitch[plane],
                        nvbuf_surf->surfaceList->planeParams.width[plane] * nvbuf_surf->surfaceList->planeParams.bytesPerPix[plane]);
        if (!stream->good())
            return -1;
    }
    ret = NvBufSurfaceUnMap(nvbuf_surf, 0, plane);
    if (ret < 0)
    {
        printf("NvBufSurfaceUnMap failed\n");
        return ret;
    }
    return 0;
}

int
parse_csv_recon_file(std::ifstream * stream, std::string * recon_params)
{
    int parse_count = 0;
    std::string ref_line, ref_parsed_word;

    getline(*stream, ref_line);
    std::stringstream recon_ref_stream(ref_line);

    while (getline(recon_ref_stream, ref_parsed_word, ','))
    {
        if (parse_count >= 4)
        {
            printf("Only YUV data is supported in reconstructed pictures \n");
            return -1;
        }
        recon_params[parse_count] = ref_parsed_word;
        parse_count++;
    }
    return 0;
}
