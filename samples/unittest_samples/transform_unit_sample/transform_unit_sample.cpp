/*
 * Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions, and the following disclaimer.
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
 * Execution command
 * ./transform_sample input_raw input_format width height output_raw output_format enable_gpu
 * Example:
 * ./transform_sample test_file.yuv yuv420 1920 1080 converted_file.yuv nv12 0
 * ./transform_sample test_file.yuv yuv420 1920 1080 converted_file.yuv nv12 1
**/

#include <fstream>
#include <string>
#include <iostream>
#include <malloc.h>
#include <assert.h>
#include <string.h>
#include <sstream>
#include <cuda.h>
#include <cuda_runtime.h>

using namespace std;

#include "transform_unit_sample.hpp"

/**
 * Video Transform/Filter using NVIDIA buffering utility.
 *
 * The NVIDIA buffering utility or nvbufsurface provide a wrapper to simplify
 * the use case of applications/plugins for Buffering and
 * Transform or Composite or Blending.
 *
 * This is one such sample demonstration to use nvbufsurface for conversion from
 * one pixel-format to another using either VIC or GPU.
 * Supported pixel-formats, filters, composites and other properties are
 * described in the nvbufsurface header.
 *
 * For transformation:
 * ## Specify parameters of input and output in NvBufSurfaceCreateParams for
 *    hardware buffers creation.
 * ## Create the HW buffer calling NvBufSurfaceCreate, which returns the
 *    DMABUF FD of the buffer allocated.
 * ## Define the transformation parameters in NvBufSurfTransformParams.
 * ## Call the NvBufSurfTransform which transforms the input DMA buffer
 *    to the output DMA buffer, both exported as fd.
**/

#define CHECK_ERROR(condition, error_str, label) if (condition) { \
                                                        cerr << error_str << endl; \
                                                        in_error = 1; \
                                                        goto label; }

static int bytes_per_pixel_destfmt[MAX_PLANES] = {0};
static int bytes_per_pixel_srcfmt[MAX_PLANES] = {0};

static void
print_help(void)
{
    cout << "Help:" << endl;
    cout << "Execution cmd:\n"
         << "./transform_sample input_file.yuv input_pixfmt "
         << "width height output_file.yuv output_pixfmt enable_gpu\n"
         << endl;
    cout << "Supported pixel formats:\n"
         << "\tnv12  nv21  nv12_709\n"
         << "\targb32  xrgb32\n"
         << "\tyuv420  yvu420  yuv420_709"
         << endl;
    cout << "Enable GPU : 0 or 1\n"
         << endl;
}

static void
get_color_format(const char* userdefined_fmt, NvBufSurfaceColorFormat* pixel_format)
{
    if (!strcmp(userdefined_fmt, "nv12"))
        *pixel_format = NVBUF_COLOR_FORMAT_NV12;
    else if (!strcmp(userdefined_fmt, "nv21"))
        *pixel_format = NVBUF_COLOR_FORMAT_NV21;
    else if (!strcmp(userdefined_fmt,"nv12_709"))
        *pixel_format = NVBUF_COLOR_FORMAT_NV12_709;
    else if (!strcmp(userdefined_fmt,"argb32"))
        *pixel_format = NVBUF_COLOR_FORMAT_ARGB;
    else if (!strcmp(userdefined_fmt,"xrgb32"))
        *pixel_format = NVBUF_COLOR_FORMAT_xRGB;
    else if (!strcmp(userdefined_fmt,"yuv420"))
        *pixel_format = NVBUF_COLOR_FORMAT_YUV420;
    else if (!strcmp(userdefined_fmt,"yvu420"))
        *pixel_format = NVBUF_COLOR_FORMAT_YVU420;
    else if (!strcmp(userdefined_fmt,"yuv420_709"))
        *pixel_format = NVBUF_COLOR_FORMAT_YUV420_709;
    else
        *pixel_format = NVBUF_COLOR_FORMAT_INVALID;

}

static void
fill_bytes_per_pixel(NvBufSurfaceColorFormat pixel_format, int * bytes_per_pixel_req)
{
    switch (pixel_format)
    {
        case NVBUF_COLOR_FORMAT_NV12:
        case NVBUF_COLOR_FORMAT_NV21:
        case NVBUF_COLOR_FORMAT_NV12_709:
        {
            bytes_per_pixel_req[0] = 1;
            bytes_per_pixel_req[1] = 2;
            break;
        }
        case NVBUF_COLOR_FORMAT_ARGB:
        case NVBUF_COLOR_FORMAT_xRGB:
        {
            bytes_per_pixel_req[0] = 4;
            break;
        }
        case NVBUF_COLOR_FORMAT_YUV420:
        case NVBUF_COLOR_FORMAT_YVU420:
        case NVBUF_COLOR_FORMAT_YUV420_709:
        {
            bytes_per_pixel_req[0] = 1;
            bytes_per_pixel_req[1] = 1;
            bytes_per_pixel_req[2] = 1;
            break;
        }
        default:
            return;
    }
}

/**
 * This function reads the video frame from the input file stream
 * and writes to the source HW buffer exported as FD.
 * Using the FD, HW buffer parameters are filled by calling
 * NvBufSurfaceFromFd. The parameters recieved from the buffer are
 * then used to write the raw stream in planar form into the buffer.
 *
 * For writing in the HW buffer:
 * A void data-pointer in created which stores the memory-mapped
 * virtual addresses of the planes.
 * For each plane, NvBufSurfaceMap is called which gets the
 * memory-mapped virtual address of the plane with the access
 * pointed by the flag.
 * Before the mapped memory is accessed, a call to NvBufSurfaceSyncForDevice()
 * with the virtual address must be present, before any modification
 * from CPU to the buffer is performed.
 * After writing the data, the memory-mapped virtual address of the
 * plane is unmapped.
**/

static int
read_video_frame(int src_dma_fd, ifstream * input_stream, unsigned int enable_gpu)
{
    int ret = 0;
    NvBufSurface *nvbuf_surf = 0;

    ret = NvBufSurfaceFromFd (src_dma_fd, (void**)(&nvbuf_surf));
    if (ret)
    {
        cerr << "NvBufSurfaceFromFd failed" << endl;
        return -1;
    }

    /* Void data pointer to store memory-mapped
    ** virtual addresses of the planes.
    */
    void *virtualip_data_addr;
    unsigned int plane = 0;

    for (plane = 0; plane < nvbuf_surf->surfaceList[0].planeParams.num_planes ; ++plane)
    {
        ret = NvBufSurfaceMap(nvbuf_surf, 0, plane, NVBUF_MAP_READ_WRITE);
        if (ret == 0)
        {
            unsigned int i = 0;
            virtualip_data_addr = (void*)nvbuf_surf->surfaceList[0].mappedAddr.addr[plane];

            for (i = 0; i < nvbuf_surf->surfaceList[0].planeParams.height[plane]; ++i)
            {
                streamsize bytes_to_read = nvbuf_surf->surfaceList[0].planeParams.width[plane] * bytes_per_pixel_srcfmt[plane];
                input_stream->read ((char*)virtualip_data_addr + i * nvbuf_surf->surfaceList[0].planeParams.pitch[plane],
                    bytes_to_read);
                if (input_stream->gcount() < bytes_to_read)
                {
                    cout << "End of File" << endl;
                    return -1;
                }
            }
            /* Syncs HW memory for writing to the buffer.
            ** This call must be called before any HW device
            ** accesses the buffer.
            ** Sync is not required when transformation
            ** is done using GPU as CPU and GPU memory
            ** are different.
            */
            if (!enable_gpu)
                NvBufSurfaceSyncForDevice(nvbuf_surf, 0, plane);
        }
        NvBufSurfaceUnMap(nvbuf_surf, 0, plane);
    }
    return 0;
}

/**
 * This function writes the video frame from the HW buffer
 * exported as FD into the destination file.
 * Using the FD, HW buffer parameters are filled by calling
 * NvBufSurfaceFromFd. The parameters recieved from the buffer are
 * then used to read the planar stream from the HW buffer into the
 * output filestream.
 *
 * For reading from the HW buffer:
 * A void data-pointer in created which stores the memory-mapped
 * virtual addresses of the planes.
 * For each plane, NvBufSurfaceMap is called which gets the
 * memory-mapped virtual address of the plane with the access
 * pointed by the flag.
 * Before the mapped memory is accessed, a call to NvBufSurfaceSyncForCpu()
 * with the virtual address must be present, before any access is made
 * by the CPU to the buffer.
 *
 * After reading the data, the memory-mapped virtual address of the
 * plane is unmapped.
**/
static int
write_video_frame(int dest_dma_fd, ofstream * output_stream, unsigned int enable_gpu)
{
    int ret = 0;
    NvBufSurface *nvbuf_surf = 0;

    ret = NvBufSurfaceFromFd (dest_dma_fd, (void**)(&nvbuf_surf));
    if (ret)
    {
        cerr << "NvBufSurfaceFromFd failed" << endl;
        return -1;
    }

    /* Void data pointer to store memory-mapped
    ** virtual addresses of the planes.
    */

    void *virtualop_data_addr;
    unsigned int plane = 0;

    for ( plane = 0; plane < nvbuf_surf->surfaceList[0].planeParams.num_planes ; ++plane)
    {
        ret = NvBufSurfaceMap(nvbuf_surf, 0, plane, NVBUF_MAP_READ_WRITE);
        if (ret == 0)
        {
            unsigned int i = 0;
            virtualop_data_addr = (void*)nvbuf_surf->surfaceList[0].mappedAddr.addr[plane];

            /* Syncs HW memory for reading from
            ** the buffer.
            ** Sync is not required when transformation
            ** is done using GPU as CPU and GPU memory
            ** are different.
            */
            if (!enable_gpu)
                NvBufSurfaceSyncForCpu (nvbuf_surf, 0, plane);
            for (i = 0; i < nvbuf_surf->surfaceList[0].planeParams.height[plane]; ++i)
            {
                streamsize bytes_to_write = nvbuf_surf->surfaceList[0].planeParams.width[plane] * bytes_per_pixel_destfmt[plane];
                output_stream->write ((char*)virtualop_data_addr + i * nvbuf_surf->surfaceList[0].planeParams.pitch[plane],
                    bytes_to_write);
                if (!output_stream->good())
                {
                    cerr << "File write failure" << endl;
                    return -1;
                }
            }

        }
        NvBufSurfaceUnMap(nvbuf_surf, 0, plane);
    }
    return 0;
}

int
main(int argc, char const *argv[])
{
    ifstream *input_file = NULL;
    string input_file_path;
    ofstream *output_file = NULL;
    string output_file_path;
    int ret = 0;
    bool in_error = 0;
    unsigned enable_gpu = 0;

    // Initialisation.
    int width = 0;
    int height = 0;
    int source_dmabuf_fd = -1;
    int dest_dmabuf_fd = -1;
    bool eos = false;

    NvBufSurfaceAllocateParams input_params = {{0}};
    NvBufSurfaceAllocateParams input_file_params = {{0}};
    NvBufSurfaceAllocateParams output_params = {{0}};
    NvBufSurfaceAllocateParams output_file_params = {{0}};
    NvBufSurface *src_nvbuf_surf = 0;
    NvBufSurface *src_file_nvbuf_surf = 0;
    NvBufSurface *dst_nvbuf_surf = 0;
    NvBufSurface *dst_file_nvbuf_surf = 0;
    NvBufSurfaceColorFormat input_color_format = NVBUF_COLOR_FORMAT_INVALID;
    NvBufSurfaceColorFormat output_color_format = NVBUF_COLOR_FORMAT_INVALID;
    NvBufSurfTransformParams transform_params = {0};
    NvBufSurfTransformRect src_rect = {0}, dest_rect = {0};
    NvBufSurfTransformConfigParams config_params = {NvBufSurfTransformCompute_VIC, 0, NULL};

    if (!strcmp(argv[1], "-h") || !strcmp(argv[1],"--help"))
    {
        print_help();
        return 0;
    }

    assert (argc >= 7);
    input_file_path = argv[1];
    get_color_format(argv[2], &input_color_format);
    width = atoi(argv[3]);
    height = atoi(argv[4]);
    output_file_path = argv[5];
    get_color_format(argv[6], &output_color_format);
    if (argc >= 8)
        enable_gpu = atoi(argv[7]);

    if (width <= 0 || height  <= 0)
    {
       cerr << "Width and Height should be positive integers" << endl;
       return -1;
    }

    if (input_color_format == NVBUF_COLOR_FORMAT_INVALID ||
        output_color_format == NVBUF_COLOR_FORMAT_INVALID)
    {
        cerr << "Error, invalid input or output pixel format" << endl;
        print_help();
        return -1;
    }

    if (enable_gpu)
        cout << "GPU path is selected" << endl;

    // I/O file operations.

    input_file = new ifstream(input_file_path);
    CHECK_ERROR(!input_file->is_open(),
        "Error in opening input file", cleanup);

    output_file = new ofstream(output_file_path);
    CHECK_ERROR(!output_file->is_open(),
        "Error in opening output file", cleanup);


    /* Define the parameter for the HW Buffer.
    ** @memType defines the memory handle
    ** for the NvBufSurface, here defined for the
    ** set of planese.
    ** @layout defines memory layout for the
    ** surfaces, either Pitch/BLockLinear
    ** (Note: The BlockLinear surfaces allocated
    ** needs to be again transformed to Pitch
    ** for dumping the buffer).
    */

    if (enable_gpu) {
        input_file_params.params.width = width;
        input_file_params.params.height = height;
        input_file_params.params.memType = NVBUF_MEM_CUDA_PINNED;
        input_file_params.params.gpuId = 0;
        input_file_params.params.colorFormat = input_color_format;
        input_file_params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;
    }

    input_params.params.width = width;
    input_params.params.height = height;
    if (enable_gpu) {
        input_params.params.memType = NVBUF_MEM_CUDA_DEVICE;
        input_params.params.gpuId = 0;
    } else {
        input_params.params.layout = NVBUF_LAYOUT_PITCH;
        input_params.params.memType = NVBUF_MEM_SURFACE_ARRAY;
    }
    input_params.params.colorFormat = input_color_format;
    input_params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;

    if (enable_gpu) {
        output_file_params.params.width = width;
        output_file_params.params.height = height;
        output_file_params.params.memType = NVBUF_MEM_CUDA_PINNED;
        output_file_params.params.gpuId = 0;
        output_file_params.params.colorFormat = output_color_format;
        output_file_params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;
    }

    output_params.params.width = width;
    output_params.params.height = height;
    if (enable_gpu) {
        output_params.params.memType = NVBUF_MEM_CUDA_DEVICE;
        output_params.params.gpuId = 0;
    } else {
        output_params.params.layout = NVBUF_LAYOUT_PITCH;
        output_params.params.memType = NVBUF_MEM_SURFACE_ARRAY;
    }
    output_params.params.colorFormat = output_color_format;
    output_params.memtag = NvBufSurfaceTag_VIDEO_CONVERT;

    /* Store th bpp required for each color
    ** format to read/write properly to raw
    ** buffers.
    */

    fill_bytes_per_pixel(input_params.params.colorFormat, bytes_per_pixel_srcfmt);
    fill_bytes_per_pixel(output_params.params.colorFormat, bytes_per_pixel_destfmt);

    /* Create the HW Buffer. It is exported as
    ** an FD by the hardware.
    */

    ret = NvBufSurfaceAllocate(&src_nvbuf_surf, 1, &input_params);
    CHECK_ERROR(ret,
        "Error in creating the source buffer.", cleanup);
    src_nvbuf_surf->numFilled = 1;
    source_dmabuf_fd = src_nvbuf_surf->surfaceList[0].bufferDesc;

    ret = NvBufSurfaceAllocate(&dst_nvbuf_surf, 1, &output_params);
    CHECK_ERROR(ret,
        "Error in creating the destination buffer.", cleanup);
    dst_nvbuf_surf->numFilled = 1;
    dest_dmabuf_fd = dst_nvbuf_surf->surfaceList[0].bufferDesc;

    if (enable_gpu) {
        ret = NvBufSurfaceAllocate(&src_file_nvbuf_surf, 1, &input_file_params);
        CHECK_ERROR(ret,"Error in creating the source buffer.", cleanup);
        src_file_nvbuf_surf->numFilled = 1;
        source_dmabuf_fd = src_file_nvbuf_surf->surfaceList[0].bufferDesc;

        ret = NvBufSurfaceAllocate(&dst_file_nvbuf_surf, 1, &output_file_params);
        CHECK_ERROR(ret,"Error in creating the destination buffer.", cleanup);
        dst_file_nvbuf_surf->numFilled = 1;
        dest_dmabuf_fd = dst_file_nvbuf_surf->surfaceList[0].bufferDesc;
    }

    if (!enable_gpu)
        config_params.compute_mode = NvBufSurfTransformCompute_VIC;
    else {
        config_params.gpu_id = 0;
        config_params.compute_mode = NvBufSurfTransformCompute_GPU;
        cudaStreamCreateWithFlags(&(config_params.cuda_stream), cudaStreamNonBlocking);
    }

    /* Set the session parameters */
    ret = NvBufSurfTransformSetSessionParams (&config_params);
    CHECK_ERROR(ret, "Error in NvBufSurfTransformSetSessionParams", cleanup);

    /* Transformation parameters are now defined
    ** which is passed to the NvBuuferTransform
    ** for required conversion.
    */

    src_rect.top = 0;
    src_rect.left = 0;
    src_rect.width = width;
    src_rect.height = height;
    dest_rect.top = 0;
    dest_rect.left = 0;
    dest_rect.width = width;
    dest_rect.height = height;

    /* @transform_flag defines the flags for
    ** enabling the valid transforms.
    ** All the valid parameters are present in
    ** the nvbufsurface header.
    */

    memset(&transform_params,0,sizeof(transform_params));
    transform_params.transform_flag = NVBUFSURF_TRANSFORM_FILTER | NVBUFSURF_TRANSFORM_FLIP;
    transform_params.transform_flip = NvBufSurfTransform_Rotate180;
    transform_params.transform_filter = NvBufSurfTransformInter_Algo4;
    transform_params.src_rect = &src_rect;
    transform_params.dst_rect = &dest_rect;

    /* The main loop for reading the data from
    ** file into the HW source buffer, calling
    ** the transform and writing the output
    ** bytestream back to the destination file.
    */

    while (!eos)
    {
        if (read_video_frame(source_dmabuf_fd, input_file, enable_gpu) < 0)
        {
            cout << "File read complete." << endl;
            eos = true;
            break;
        }

        if (enable_gpu) {
            ret = NvBufSurfaceCopy (src_file_nvbuf_surf, src_nvbuf_surf);
            CHECK_ERROR(ret, "Error in NvBufSurfaceCopy", cleanup);
        }

        ret = NvBufSurfTransform(src_nvbuf_surf, dst_nvbuf_surf, &transform_params);
        CHECK_ERROR(ret, "Error in transformation.", cleanup);

        if (enable_gpu) {
            ret = NvBufSurfaceCopy (dst_nvbuf_surf, dst_file_nvbuf_surf);
            CHECK_ERROR(ret, "Error in NvBufSurfaceCopy", cleanup);
        }

        ret = write_video_frame(dest_dmabuf_fd, output_file, enable_gpu);
        CHECK_ERROR(ret,
        "Error in dumping the output raw buffer.", cleanup);

    }

cleanup:
    if (input_file->is_open())
    {
        delete input_file;
    }
    if (output_file->is_open())
    {
        delete output_file;
    }

    /* HW allocated buffers must be destroyed
    ** at the end of execution.
    */

    if (source_dmabuf_fd != -1)
    {
        NvBufSurfaceDestroy(src_nvbuf_surf);
        if (enable_gpu)
            NvBufSurfaceDestroy(src_file_nvbuf_surf);
        source_dmabuf_fd = -1;
    }

    if (dest_dmabuf_fd != -1)
    {
        NvBufSurfaceDestroy(dst_nvbuf_surf);
        if (enable_gpu)
            NvBufSurfaceDestroy(dst_file_nvbuf_surf);
        dest_dmabuf_fd = -1;
    }

    if (enable_gpu)
        cudaStreamDestroy(config_params.cuda_stream);

    if (in_error)
    {
        cerr << "Transform Failed" << endl;
    }
    else
    {
        cout << "Transform Successful" << endl;
    }

    return ret;
}
