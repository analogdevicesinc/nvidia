/*
 * Copyright (c) 2022, NVIDIA CORPORATION.  All rights reserved.
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */

#include "NvBufSurface.h"

using namespace std;

int
NvBufSurf::NvDestroy(int fd)
{
    int ret = 0;
    NvBufSurface *nvbuf_surf = 0;
    if (fd <= 0)
      return -1;
    NvBufSurfaceFromFd(fd, (void**)(&nvbuf_surf));
    if (nvbuf_surf != NULL)
    {
        ret = NvBufSurfaceDestroy(nvbuf_surf);
    }
    return ret;
}

int
NvBufSurf::NvAllocate(NvCommonAllocateParams *allocateParams, uint32_t numBuffers, int *fd)
{
    int ret = 0;
    NvBufSurfaceAllocateParams input_params = {{0}};

    if (numBuffers < 1 || allocateParams == NULL)
      return -1;
    input_params.params.width = allocateParams->width;
    input_params.params.height = allocateParams->height;
    input_params.params.memType = allocateParams->memType;
    input_params.params.layout = allocateParams->layout;
    input_params.params.colorFormat = allocateParams->colorFormat;
    input_params.memtag = allocateParams->memtag;

    for (uint32_t index = 0; index < numBuffers; index++) {
      NvBufSurface *nvbuf_surf = 0;
      ret = NvBufSurfaceAllocate(&nvbuf_surf, 1, &input_params);
      fd[index] = nvbuf_surf->surfaceList[0].bufferDesc;
      nvbuf_surf->numFilled = 1;
    }

    return ret;
}

int
NvBufSurf::NvTransform(NvCommonTransformParams *transformParams, int src_fd, int dst_fd)
{
    int ret = 0;
    if (transformParams == NULL)
      return -1;
    NvBufSurfTransformRect src_rect = {0};
    NvBufSurfTransformRect dest_rect = {0};
    NvBufSurfTransformParams transform_params;
    NvBufSurface *nvbuf_surf_src = 0;
    NvBufSurface *nvbuf_surf_dst = 0;
    src_rect.top = transformParams->src_top;
    src_rect.left = transformParams->src_left;
    src_rect.width = transformParams->src_width;
    src_rect.height = transformParams->src_height;
    dest_rect.top = transformParams->dst_top;
    dest_rect.left = transformParams->dst_left;
    dest_rect.width = transformParams->dst_width;
    dest_rect.height = transformParams->dst_height;

    memset(&transform_params,0,sizeof(transform_params));
    /* Indicates which of the transform parameters are valid. */
    transform_params.transform_flag = transformParams->flag;
    transform_params.transform_flip = transformParams->flip;
    transform_params.transform_filter = transformParams->filter;
    transform_params.src_rect = &src_rect;
    transform_params.dst_rect = &dest_rect;
    NvBufSurfaceFromFd(src_fd, (void**)(&nvbuf_surf_src));
    NvBufSurfaceFromFd(dst_fd, (void**)(&nvbuf_surf_dst));

    ret = NvBufSurfTransform(nvbuf_surf_src, nvbuf_surf_dst, &transform_params);

    return ret;
}

int
NvBufSurf::NvTransformAsync(NvCommonTransformParams *transformParams, NvBufSurfTransformSyncObj_t *sync_obj, int src_fd, int dst_fd)
{
    int ret = 0;
    NvBufSurfTransformRect dest_rect, src_rect;
    NvBufSurfTransformParams transform_params;
    NvBufSurface *nvbuf_surf_src = 0;
    NvBufSurface *nvbuf_surf_dst = 0;
    src_rect.top = transformParams->src_top;
    src_rect.left = transformParams->src_left;
    src_rect.width = transformParams->src_width;
    src_rect.height = transformParams->src_height;
    dest_rect.top = transformParams->dst_top;
    dest_rect.left = transformParams->dst_left;
    dest_rect.width = transformParams->dst_width;
    dest_rect.height = transformParams->dst_height;

    memset(&transform_params,0,sizeof(transform_params));
    /* Indicates which of the transform parameters are valid. */
    transform_params.transform_flag = transformParams->flag;
    transform_params.transform_flip = transformParams->flip;
    transform_params.transform_filter = transformParams->filter;
    transform_params.src_rect = &src_rect;
    transform_params.dst_rect = &dest_rect;
    NvBufSurfaceFromFd(src_fd, (void**)(&nvbuf_surf_src));
    NvBufSurfaceFromFd(dst_fd, (void**)(&nvbuf_surf_dst));
    ret = NvBufSurfTransformAsync(nvbuf_surf_src, nvbuf_surf_dst, &transform_params, sync_obj);

    return ret;
}

