/*
 * SPDX-FileCopyrightText: Copyright (c) 2016-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#include <pthread.h>
#include "NvBufSurface.h"

typedef struct
{
    uint32_t num_thread;
    bool create_session;
    bool perf;
    bool async;

    char *in_file_path;
    uint32_t in_width;
    uint32_t in_height;
    NvBufSurfaceColorFormat in_pixfmt;

    char *out_file_path;
    uint32_t out_width;
    uint32_t out_height;
    NvBufSurfaceColorFormat out_pixfmt;

    NvBufSurfTransform_Flip flip_method;
    NvBufSurfTransform_Inter interpolation_method;
    NvBufSurfTransformRect crop_rect;

    struct
    {
        uint8_t horiz;
        uint8_t vert;
    } in_chroma_loc, out_chroma_loc;

} context_t;

int parse_csv_args(context_t * ctx, int argc, char *argv[]);
