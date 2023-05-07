/*
 * Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved.
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

#include "CudaBayerDemosaicKernel.h"
#include <stdio.h>

#ifndef CU_EGL_COLOR_FORMAT_BAYER_RGGB
#define CU_EGL_COLOR_FORMAT_BAYER_RGGB (0x2D)
#define CU_EGL_COLOR_FORMAT_BAYER_BGGR (0x2E)
#define CU_EGL_COLOR_FORMAT_BAYER_GRBG (0x2F)
#define CU_EGL_COLOR_FORMAT_BAYER_GBRG (0x30)
#endif

// Constant used to store the component ordering of the Bayer input (used by bayerToRgba).
// These values provide the indexes into the original data that will provide an RGGB ordering.
__constant__ unsigned int bayerPattern[4];

// Converts a 16-bit Bayer quad to 32bit RGBA. The Bayer components are provided
// in the order they're stored in the buffer, as this function will also handle
// the component ordering during conversion using the 'bayerPattern' constant.
static __device__ uchar4
bayerToRgba(unsigned short bayerQuad[4])
{
    // Signed 16-bit Bayer maps 1<<14 to white.
    unsigned int whitePoint = 0xfff;
    unsigned int maxRgba = 0xff;

    // Order the Bayer components based on the format component ordering.
    unsigned int shift = 4;
    unsigned int mask = 0xfff;
    unsigned int r  = (bayerQuad[bayerPattern[0]] >> shift) & mask;
    unsigned int g1 = (bayerQuad[bayerPattern[1]] >> shift) & mask;
    unsigned int g2 = (bayerQuad[bayerPattern[2]] >> shift) & mask;
    unsigned int b  = (bayerQuad[bayerPattern[3]] >> shift) & mask;

    unsigned int rb = b * maxRgba / whitePoint;
    unsigned int rg = (g1 + g2) * maxRgba / whitePoint / 2;
    unsigned int rr = r * maxRgba / whitePoint;

    if (r > whitePoint || g1 > whitePoint || g2 > whitePoint || b > whitePoint ||
        rr > maxRgba || rg > maxRgba || rb > maxRgba) {
        printf("wp: %04x, r: %04x, g1: %04x, g2: %04x, b: %04x, rr: %04x, rg: %04x, rb: %04x\n",
               whitePoint, r, g1, g2, b, rr, rg, rb);
    }

    // Map [0, 1<<14] to [0, 255].
    uchar4 rgba;
    rgba.x = rb;
    rgba.y = rg;
    rgba.z = rr;

    return rgba;
}

// Demosaics a Bayer buffer into an RGBA output.
__global__ void
bayerDemosaicKernel(unsigned short* bayerSrc,
                    int bayerWidth,
                    int bayerHeight,
                    int bayerPitch,
                    uchar4* rgbaDst)
{
    int x = blockIdx.x * blockDim.x + threadIdx.x;
    int y = blockIdx.y * blockDim.y + threadIdx.y;
    int stepX = blockDim.x * gridDim.x;
    int stepY = blockDim.y * gridDim.y;

    int rgbaWidth = bayerWidth / 2;
    int rgbaHeight = bayerHeight / 2;

    for (int col = x; col < rgbaWidth; col += stepX)
    {
        for (int row = y; row < rgbaHeight; row += stepY)
        {
            // Extract the Bayer quad.
            unsigned short* bayerOffset = bayerSrc + (col * 2) + (row * bayerPitch);
            unsigned short bayerQuad[4];
            bayerQuad[0] = *(bayerOffset);
            bayerQuad[1] = *(bayerOffset + 1);
            bayerQuad[2] = *(bayerOffset + (bayerPitch / 2));
            bayerQuad[3] = *(bayerOffset + (bayerPitch / 2) + 1);

            // Demosaic the Bayer quad to RGBA.
            uchar4 rgba = bayerToRgba(bayerQuad);

            // Optional: Add gain (useful to debug dark output).
            unsigned int gain = 1;
            rgba.x = umin(rgba.x * gain, 255);
            rgba.y = umin(rgba.y * gain, 255);
            rgba.z = umin(rgba.z * gain, 255);

            // Output the RGBA pixels to the output buffer.
            rgbaDst[rgbaWidth * row + col] = rgba;
        }
    }
}

// Sets the Bayer pattern constant used to order Bayer components.
static void setBayerPatternConstant(int bayerFormat)
{
    unsigned int pattern[4];
    if (bayerFormat == CU_EGL_COLOR_FORMAT_BAYER_RGGB)
    {
        pattern[0] = 0;
        pattern[1] = 1;
        pattern[2] = 2;
        pattern[3] = 3;
    }
    else if (bayerFormat == CU_EGL_COLOR_FORMAT_BAYER_BGGR)
    {
        pattern[0] = 3;
        pattern[1] = 1;
        pattern[2] = 2;
        pattern[3] = 0;
    }
    else if (bayerFormat == CU_EGL_COLOR_FORMAT_BAYER_GRBG)
    {
        pattern[0] = 1;
        pattern[1] = 0;
        pattern[2] = 3;
        pattern[3] = 2;
    }
    else // bayerFormat == CU_EGL_COLOR_FORMAT_BAYER_GBRG
    {
        pattern[0] = 2;
        pattern[1] = 0;
        pattern[2] = 3;
        pattern[3] = 1;
    }
    cudaMemcpyToSymbol(bayerPattern, pattern, sizeof(pattern));
}

// Entrypoint to the CUDA Bayer Demosaic.
int cudaBayerDemosaic(CUdeviceptr bayerSrc,
                      int bayerWidth,
                      int bayerHeight,
                      int bayerPitch,
                      int bayerFormat,
                      CUdeviceptr rgbaDst)
{
    setBayerPatternConstant(bayerFormat);

    dim3 threadsPerBlock(32, 32);
    dim3 blocks(2, 2);

    cudaEvent_t start;
    cudaEvent_t stop;

    cudaEventCreate(&start);
    cudaEventCreate(&stop);

    cudaEventRecord(start, 0);

    bayerDemosaicKernel<<<blocks, threadsPerBlock>>>(
            (unsigned short*)bayerSrc, bayerWidth, bayerHeight, bayerPitch, (uchar4*)rgbaDst);

    cudaEventRecord(stop, 0);

    cudaEventSynchronize(stop);
    float elapsedMillis;
    cudaEventElapsedTime(&elapsedMillis, start, stop);

    printf("CUDA KERNEL:      Processed frame in %fms\n", elapsedMillis);

    return 0;
}
