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

#include <iostream>
#include <cstring>

#include "video_osd.h"

#define CHECK_OPTION_VALUE(argp) \
    if(!*argp || (*argp)[0] == '-') \
    { \
        cerr << "Error: value not specified for option " << arg << endl; \
        goto error; \
    }

#define CSV_PARSE_CHECK_ERROR(condition, str) \
    if (condition) {\
    cerr << "Error: " << str << endl; \
    goto error; \
    }

using namespace std;

static void
print_help(void)
{
    cerr <<
        "\nvideo_convert <in-file> <in-width> <in-height> <in-format> <out-file-prefix> <process-format> [OPTIONS]\n\n"
        "Supported formats:\n"
        "\tNV12\n"
        "\tNV12_ER\n"
        "\tRGBA\n"
        "\tNV12_709\n"
        "\tNV12_709_ER\n"
        "\tNV12_2020\n"
        "OPTIONS:\n"
        "\t-h,--help            Prints this text\n\n"
        "\t-t,--num-thread <number>     Number of thread to process [Default = 1]\n"
        "\t-m --osd-mode           OSD process mode: 0 CPU/2 VIC(only support RGBA format), 1 GPU [Default = 0]\n"
        "\t--bl                    OSD process on NV12 block linear for GPU mode\n"
        "\t-p,--perf            Calculate performance\n";
}

static NvBufSurfaceColorFormat
get_color_format(const char* userdefined_fmt)
{
    if (!strcmp(userdefined_fmt, "NV12"))
        return NVBUF_COLOR_FORMAT_NV12;
    if (!strcmp(userdefined_fmt, "NV12_ER"))
        return NVBUF_COLOR_FORMAT_NV12_ER;
    if (!strcmp(userdefined_fmt, "RGBA"))
        return NVBUF_COLOR_FORMAT_RGBA;
    if (!strcmp(userdefined_fmt, "NV12_709"))
        return NVBUF_COLOR_FORMAT_NV12_709;
    if (!strcmp(userdefined_fmt, "NV12_709_ER"))
        return NVBUF_COLOR_FORMAT_NV12_709_ER;
    if (!strcmp(userdefined_fmt, "NV12_2020"))
        return NVBUF_COLOR_FORMAT_NV12_2020;
    return NVBUF_COLOR_FORMAT_INVALID;
}

int
parse_csv_args(context_t * ctx, int argc, char *argv[])
{
    char **argp = argv;
    char *arg = *(++argp);

    if (argc == 1 || (arg && (!strcmp(arg, "-h") || !strcmp(arg, "--help"))))
    {
        print_help();
        exit(EXIT_SUCCESS);
    }

    CSV_PARSE_CHECK_ERROR(argc < 7, "Insufficient arguments");

    ctx->in_file_path = strdup(*argp);
    CSV_PARSE_CHECK_ERROR(!ctx->in_file_path, "Input file not specified");

    ctx->in_width = atoi(*(++argp));
    CSV_PARSE_CHECK_ERROR(ctx->in_width == 0, "Input width should be > 0");

    ctx->in_height = atoi(*(++argp));
    CSV_PARSE_CHECK_ERROR(ctx->in_height == 0, "Input height should be > 0");

    ctx->in_pixfmt = get_color_format(*(++argp));
    CSV_PARSE_CHECK_ERROR(ctx->in_pixfmt == NVBUF_COLOR_FORMAT_INVALID, "Incorrect input format");

    ctx->out_file_path = strdup(*(++argp));
    CSV_PARSE_CHECK_ERROR(!ctx->out_file_path, "Output file not specified");

    ctx->process_pixfmt = get_color_format(*(++argp));
    CSV_PARSE_CHECK_ERROR(ctx->process_pixfmt == NVBUF_COLOR_FORMAT_INVALID, "Incorrect processput format");

    while ((arg = *(++argp)))
    {
        if (!strcmp(arg, "-h") || !strcmp(arg, "--help"))
        {
            print_help();
            exit(EXIT_SUCCESS);
        }
        else if (!strcmp(arg, "-t") || !strcmp(arg, "--num-thread"))
        {
            argp++;
            CHECK_OPTION_VALUE(argp);
            ctx->num_thread = atoi(*argp);
        }
        else if (!strcmp(arg, "-m") || !strcmp(arg, "--osd_mode"))
        {
            argp++;
            CHECK_OPTION_VALUE(argp);
            ctx->osd_mode = (NvOSD_Mode)atoi(*argp);
        }
        else if (!strcmp(arg, "--bl"))
        {
            ctx->is_bl = true;
        }
        else if (!strcmp(arg, "-p") || !strcmp(arg, "--perf"))
        {
            ctx->perf = true;
        }
        else
        {
            CSV_PARSE_CHECK_ERROR(ctx->out_file_path, "Unknown option " << arg);
        }
    }

    return 0;

error:
    print_help();
    return -1;
}
