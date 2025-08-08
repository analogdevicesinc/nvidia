/*
 * SPDX-FileCopyrightText: Copyright (c) 2016-2024 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "trt_inference.h"
#include <stdlib.h>
#include <sys/time.h>
#include <assert.h>
#include <sstream>
#include <iostream>
#include <sys/stat.h>
#include <cmath>
#include <cuda_runtime_api.h>
#include <algorithm>
#include <iterator>

static const int TIMING_ITERATIONS = 1;
static const int NUM_BINDINGS = 3;
static const int FILTER_NUM = 6;

#define CHECK(status)                                   \
{                                                       \
    if (status != 0)                                    \
    {                                                   \
        std::cout << "Cuda failure: " << status;        \
        abort();                                        \
    }                                                   \
}

// Logger for TRT info/warning/errors
class Logger : public ILogger
{
    void log(Severity severity, const char* msg) noexcept override
    {
        // suppress info-level messages
        if (severity != Severity::kINFO)
            std::cout << msg << std::endl;
    }
};

class Profiler : public IProfiler
{
    typedef std::pair<std::string, float> Record;
    std::vector<Record> mProfile;

    virtual void reportLayerTime(const char* layerName, float ms) noexcept
    {
        auto record = std::find_if(mProfile.begin(), mProfile.end(),
                        [&](const Record& r){ return r.first == layerName; });
        if (record == mProfile.end())
            mProfile.push_back(std::make_pair(layerName, ms));
        else
            record->second += ms;
    }

    void printLayerTimes()
    {
        float totalTime = 0;
        for (size_t i = 0; i < mProfile.size(); i++)
        {
            printf("%-40.40s %4.3fms\n", mProfile[i].first.c_str(),
                    mProfile[i].second / TIMING_ITERATIONS);
            totalTime += mProfile[i].second;
        }
        printf("Time over all layers: %4.3f\n", totalTime / TIMING_ITERATIONS);
    }

};

int
TRT_Context::getNetWidth() const
{
    return net_width;
}

int
TRT_Context::getNetHeight() const
{
    return net_height;
}

int
TRT_Context::getFilterNum() const
{
    return filter_num;
}

void
TRT_Context::setFilterNum(const unsigned int& filter_num)
{
    this->filter_num = filter_num;
}

void*&
TRT_Context::getBuffer(const int& index)
{
    assert(index >= 0 && index < num_bindings);
    return buffers[index];
}

float*&
TRT_Context::getInputBuf()
{
    return input_buf;
}

uint32_t
TRT_Context::getNumTrtInstances() const
{
    return trtinstance_num;
}

uint32_t
TRT_Context::getBatchSize() const
{
    return batch_size;
}

int
TRT_Context::getModelClassCnt() const
{
    return g_pModelNetAttr->classCnt;
}


void*
TRT_Context::getScales() const
{
    return scales_gpu;
}

void*
TRT_Context::getOffsets() const
{
    return offset_gpu;
}

void
TRT_Context::setBatchSize(const uint32_t& batchsize)
{
    this->batch_size = batchsize;
}

void
TRT_Context::setDumpResult(const bool& dump_result)
{
    this->dump_result = dump_result;
}

void
TRT_Context::setTrtProfilerEnabled(const bool& enable_trt_profiler)
{
    this->enable_trt_profiler = enable_trt_profiler;
}

int
TRT_Context::getChannel() const
{
    return channel;
}


TRT_Context::TRT_Context()
{
    net_width = 0;
    net_height = 0;
    filter_num = FILTER_NUM;
    buffers = new void *[NUM_BINDINGS];
    for (int i = 0; i < NUM_BINDINGS; i++)
    {
        buffers[i] = NULL;
    }
    input_buf = NULL;
    output_cov_buf = NULL;
    output_bbox_buf = NULL;

    runtime = NULL;
    engine = NULL;
    context = NULL;
    pResultArray = new uint32_t[100*4];

    channel = 0;
    num_bindings = NUM_BINDINGS;

    batch_size = 1;

    trtinstance_num = 1;

    elapsed_frame_num = 0;
    elapsed_time = 0;
    enable_trt_profiler = 1;
    dump_result = 0;
    frame_num = 0;
    result_file = "result.txt";
    pLogger = new Logger;
    pProfiler = new Profiler;
}


void
TRT_Context::allocateMemory(bool bUseCPUBuf)
{
    const ICudaEngine& cuda_engine = context->getEngine();
    // input and output buffer pointers that we pass to the engine
    // the engine requires exactly IEngine::getNbIOTensors() of these
    // but in this case we know that there is exactly one input and one output
    assert(cuda_engine.getNbIOTensors() == num_bindings);

    inputIndex = -1;
    outputIndex = -1;
    outputIndexBBOX = -1;
    // In order to bind the buffers, we need to know the names of the input
    // and output tensors. note that indices are guaranteed to be less than
    // IEngine::getNbIOTensors()
    for (int idx = 0; idx < cuda_engine.getNbIOTensors(); idx++)
    {
        if (!strcmp(cuda_engine.getIOTensorName(idx), g_pModelNetAttr->INPUT_BLOB_NAME))
        {
            inputIndex = idx;
        }
        else if (!strcmp(cuda_engine.getIOTensorName(idx), g_pModelNetAttr->OUTPUT_BLOB_NAME))
        {
            outputIndex = idx;
        }
        else if (!strcmp(cuda_engine.getIOTensorName(idx), g_pModelNetAttr->OUTPUT_BBOX_NAME))
        {
            outputIndexBBOX = idx;
        }
        else
        {
            cout<<"Unknow IO Tensor: " << cuda_engine.getIOTensorName(idx) <<endl;
        }
    }

    assert(inputIndex >= 0 && outputIndex >= 0 && outputIndexBBOX >= 0);

    // allocate GPU buffers
    Dims inputD = cuda_engine.getTensorShape(g_pModelNetAttr->INPUT_BLOB_NAME);
    Dims outputD = cuda_engine.getTensorShape(g_pModelNetAttr->OUTPUT_BLOB_NAME);
    Dims outputDB = cuda_engine.getTensorShape(g_pModelNetAttr->OUTPUT_BBOX_NAME);
    inputDims = Dims3{inputD.d[1], inputD.d[2], inputD.d[3]};
    outputDims = Dims3{outputD.d[1], outputD.d[2], outputD.d[3]};
    outputDimsBBOX = Dims3{outputDB.d[1], outputDB.d[2], outputDB.d[3]};

    net_height = inputDims.d[1];
    net_width = inputDims.d[2];

    inputSize = batch_size * inputDims.d[0] * inputDims.d[1] * inputDims.d[2] * sizeof(float);
    outputSize = batch_size * outputDims.d[0] * outputDims.d[1] *
                            outputDims.d[2] * sizeof(float);
    outputSizeBBOX = batch_size * outputDimsBBOX.d[0] * outputDimsBBOX.d[1] *
                            outputDimsBBOX.d[2] * sizeof(float);
    if (bUseCPUBuf && input_buf == NULL)
    {
        input_buf = (float *)malloc(inputSize);
        assert(input_buf != NULL);
    }

    if (output_cov_buf == NULL)
    {
        output_cov_buf = (float *)malloc(outputSize);
        assert(output_cov_buf != NULL);
    }
    if (outputIndexBBOX >= 0)
    {
        if (output_bbox_buf == NULL)
        {
            output_bbox_buf = (float *)malloc(outputSizeBBOX);
            assert(output_bbox_buf != NULL);
        }
    }
    // create GPU buffers and a stream
    if (buffers[inputIndex] == NULL)
    {
        CHECK(cudaMalloc(&buffers[inputIndex], inputSize));
    }
    if (buffers[outputIndex] == NULL)
    {
        CHECK(cudaMalloc(&buffers[outputIndex], outputSize));
    }
    if (outputIndexBBOX >= 0)
    {
        if (buffers[outputIndexBBOX] == NULL)
        {
            CHECK(cudaMalloc(&buffers[outputIndexBBOX], outputSizeBBOX));
        }
    }
    CHECK(cudaMalloc(&offset_gpu, sizeof(int) * 3));
    CHECK(cudaMalloc(&scales_gpu, sizeof(float) * 3));
    CHECK(cudaMemcpy(offset_gpu, (void*)g_pModelNetAttr->offsets,
                                sizeof(int) * 3,
                                cudaMemcpyHostToDevice));
    CHECK(cudaMemcpy(scales_gpu, (void*)g_pModelNetAttr->input_scale,
                                sizeof(float) * 3,
                                cudaMemcpyHostToDevice));
    if (dump_result)
    {
        fstream.open(result_file.c_str(), ios::out);
    }
}

void
TRT_Context::releaseMemory(bool bUseCPUBuf)
{
    for (int i = 0; i < NUM_BINDINGS; i++)
    {
        if (buffers[i] != NULL)
        {
            CHECK(cudaFree(buffers[i]));
            buffers[i] = NULL;
        }
    }
    if (bUseCPUBuf && input_buf != NULL)
    {
        free(input_buf);
        input_buf = NULL;
    }
    if (output_cov_buf != NULL)
    {
        free(output_cov_buf);
        output_cov_buf = NULL;
    }
    if (output_bbox_buf != NULL)
    {
        free(output_bbox_buf);
        output_bbox_buf = NULL;
    }

    if (pResultArray != NULL)
    {
        delete []pResultArray;
        pResultArray = NULL;
    }

    if (dump_result)
    {
        fstream.close();
    }
    CHECK(cudaFree(offset_gpu));
    CHECK(cudaFree(scales_gpu));
}

TRT_Context::~TRT_Context()
{

    delete pLogger;
    delete pProfiler;
    delete []buffers;
}

void
TRT_Context::setModelIndex(int index)
{
    assert(index == GOOGLENET_SINGLE_CLASS ||
           index == GOOGLENET_THREE_CLASS ||
           index == RESNET_THREE_CLASS);
    g_pModelNetAttr = gModelNetAttr + index;
    assert(g_pModelNetAttr->classCnt > 0);
    assert(g_pModelNetAttr->STRIDE > 0);
    assert(g_pModelNetAttr->WORKSPACE_SIZE > 0);
}

void
TRT_Context::buildTrtContext(const string& modelfile, bool bUseCPUBuf)
{
    ifstream trtModelFile(modelfile);
    if (trtModelFile.good())
    {
        // get cache file length
        size_t size = 0;
        size_t i = 0;

        cout<<"Using cached TRT model" <<endl;

        // Get the length
        trtModelFile.seekg(0, ios::end);
        size = trtModelFile.tellg();
        trtModelFile.seekg(0, ios::beg);

        char * buff = new char [size];
        while (trtModelFile.get(buff[i])) i++;
        trtModelFile.close();
        runtime = createInferRuntime(*pLogger);
        engine = runtime->deserializeCudaEngine((void *)buff, size);
    }
    else
    {
        cout<<"Please set TRT engine path"<<endl;
    }
    context = engine->createExecutionContext();
    allocateMemory(bUseCPUBuf);
}

void
TRT_Context::destroyTrtContext(bool bUseCPUBuf)
{
    releaseMemory(bUseCPUBuf);
    delete context;
    delete engine;
    delete runtime;
}

void
TRT_Context::doInference(
    queue< vector<cv::Rect> >* rectList_queue,
    float *input)
{
    struct timeval input_time;
    struct timeval output_time;

    if (!enable_trt_profiler)
    {
        cudaStream_t stream;
        CHECK(cudaStreamCreate(&stream));

        // DMA the input to the GPU,  execute the batch asynchronously
        // and DMA it back
        if (input != NULL)   //NULL means we have use GPU to map memory
        {
            CHECK(cudaMemcpyAsync(buffers[inputIndex], input, inputSize,
                                cudaMemcpyHostToDevice, stream));
        }

        context->setInputShape(g_pModelNetAttr->INPUT_BLOB_NAME, Dims4{batch_size, inputDims.d[0], inputDims.d[1], inputDims.d[2]});
        context->executeV2(buffers);

        CHECK(cudaMemcpyAsync(output_cov_buf, buffers[outputIndex], outputSize,
                                cudaMemcpyDeviceToHost, stream));
        if (outputIndexBBOX >= 0)
        {
            CHECK(cudaMemcpyAsync(output_bbox_buf, buffers[outputIndexBBOX],
                            outputSizeBBOX, cudaMemcpyDeviceToHost, stream));
        }

        cudaStreamSynchronize(stream);

        // release the stream and the buffers
        cudaStreamDestroy(stream);
    }
    else
    {
        // DMA the input to the GPU,  execute the batch synchronously
        // and DMA it back
        if (input != NULL)   //NULL means we have use GPU to map memory
        {
            CHECK(cudaMemcpy(buffers[inputIndex], input, inputSize,
                                cudaMemcpyHostToDevice));
        }

        gettimeofday(&input_time, NULL);
        context->setInputShape(g_pModelNetAttr->INPUT_BLOB_NAME, Dims4{batch_size, inputDims.d[0], inputDims.d[1], inputDims.d[2]});
        context->executeV2(buffers);
        gettimeofday(&output_time, NULL);
        CHECK(cudaMemcpy(output_cov_buf, buffers[outputIndex], outputSize,
                                cudaMemcpyDeviceToHost));
        if (outputIndexBBOX >= 0)
        {
            CHECK(cudaMemcpy(output_bbox_buf, buffers[outputIndexBBOX],
                            outputSizeBBOX, cudaMemcpyDeviceToHost));
        }
        elapsed_frame_num += batch_size;
        elapsed_time += (output_time.tv_sec - input_time.tv_sec) * 1000 +
                        (output_time.tv_usec - input_time.tv_usec) / 1000;
        if (elapsed_frame_num >= 100)
        {
            printf("Time elapsed:%ld ms per frame in past %ld frames\n",
                elapsed_time / elapsed_frame_num, elapsed_frame_num);
            elapsed_frame_num = 0;
            elapsed_time = 0;
        }
    }

    vector<cv::Rect> rectList[getModelClassCnt()];
    for (int i = 0; i < batch_size; i++)
    {
        if (g_pModelNetAttr->ParseFunc_ID == 0)
            parseBbox(rectList, i);
        else if(g_pModelNetAttr->ParseFunc_ID == 1)
            ParseResnet10Bbox(rectList, i);
        if (dump_result)
        {
            for (int class_num = 0;
                     class_num < (g_pModelNetAttr->ParseFunc_ID == 1 ? getModelClassCnt() - 1 : getModelClassCnt());
                     class_num++)
            {
                fstream << "frame:" << frame_num << " class num:" << class_num
                        << " has rect:" << rectList[class_num].size() << endl;
                for (uint32_t i = 0; i < rectList[class_num].size(); i++)
                {
                    fstream << "\tx,y,w,h:"
                            << (float) rectList[class_num][i].x / net_width << " "
                            << (float) rectList[class_num][i].y / net_height << " "
                            << (float) rectList[class_num][i].width / net_width << " "
                            << (float) rectList[class_num][i].height / net_height << endl;
                }
                fstream << endl;
            }
            frame_num++;
        }

        for (int class_num = 0; class_num < getModelClassCnt(); class_num++)
        {
            rectList_queue[class_num].push(rectList[class_num]);
        }
    }
}

void
TRT_Context::parseBbox(vector<cv::Rect>* rectList, int batch_th)
{
    int gridsize = outputDims.d[1] * outputDims.d[2];
    int gridoffset = outputDims.d[0] * outputDims.d[1] * outputDims.d[2] * batch_th;

    for (int class_num = 0; class_num < getModelClassCnt(); class_num++)
    {
        float *output_x1 = output_bbox_buf +
                outputDimsBBOX.d[0] * outputDimsBBOX.d[1] * outputDimsBBOX.d[2] * batch_th +
                class_num * 4 * outputDimsBBOX.d[1] * outputDimsBBOX.d[2];
        float *output_y1 = output_x1 + outputDimsBBOX.d[1] * outputDimsBBOX.d[2];
        float *output_x2 = output_y1 + outputDimsBBOX.d[1] * outputDimsBBOX.d[2];
        float *output_y2 = output_x2 + outputDimsBBOX.d[1] * outputDimsBBOX.d[2];

        for (int i = 0; i < gridsize; ++i)
        {
            if (output_cov_buf[gridoffset + class_num * gridsize + i] >=
                                          g_pModelNetAttr->THRESHOLD[class_num])
            {
                int g_x = i % outputDims.d[2];
                int g_y = i / outputDims.d[2];
                int i_x = g_x * g_pModelNetAttr->STRIDE;
                int i_y = g_y * g_pModelNetAttr->STRIDE;
                int rectx1 = g_pModelNetAttr->bbox_output_scales[0] * output_x1[i] + i_x;
                int recty1 = g_pModelNetAttr->bbox_output_scales[1] * output_y1[i] + i_y;
                int rectx2 = g_pModelNetAttr->bbox_output_scales[2] * output_x2[i] + i_x;
                int recty2 = g_pModelNetAttr->bbox_output_scales[3] * output_y2[i] + i_y;
                if (rectx1 < 0)
                {
                    rectx1 = 0;
                }
                if (rectx2 < 0)
                {
                    rectx2 = 0;
                }
                if (recty1 < 0)
                {
                    recty1 = 0;
                }
                if (recty2 < 0)
                {
                    recty2 = 0;
                }
                if (rectx1 >= (int)net_width)
                {
                    rectx1 = net_width - 1;
                }
                if (rectx2 >= (int)net_width)
                {
                    rectx2 = net_width - 1;
                }
                if (recty1 >= (int)net_height)
                {
                    recty1 = net_height - 1;
                }
                if (recty2 >= (int)net_height)
                {
                    recty2 = net_height - 1;
                }
                rectList[class_num].push_back(cv::Rect(rectx1, recty1,
                                                      rectx2 - rectx1, recty2 - recty1));
            }
        }

        cv::groupRectangles(rectList[class_num], 3, 0.2);
    }
}

void
TRT_Context::ParseResnet10Bbox(vector<cv::Rect>* rectList, int batch_th)
{
    int grid_x_ = outputDims.d[2];
    int grid_y_ = outputDims.d[1];
    int gridsize_ = grid_x_ * grid_y_;

    int target_shape[2] = {grid_x_, grid_y_};
    float bbox_norm[2] = {35.0, 35.0};
    float gc_centers_0[target_shape[0]];
    float gc_centers_1[target_shape[1]];
    for (int i = 0; i < target_shape[0]; i++)
        gc_centers_0[i] = (float)(i * 16 + 0.5)/bbox_norm[0];
    for (int i = 0; i < target_shape[1]; i++)
        gc_centers_1[i] = (float)(i * 16 + 0.5)/bbox_norm[1];
    for (int class_num = 0;
             class_num  < (g_pModelNetAttr->ParseFunc_ID == 1 ? getModelClassCnt() - 1 : getModelClassCnt());
             class_num++)
    {
        float *output_x1 = output_bbox_buf + class_num * 4 * outputDimsBBOX.d[1] * outputDimsBBOX.d[2];
        float *output_y1 = output_x1 + outputDimsBBOX.d[1] * outputDimsBBOX.d[2];
        float *output_x2 = output_y1 + outputDimsBBOX.d[1] * outputDimsBBOX.d[2];
        float *output_y2 = output_x2 + outputDimsBBOX.d[1] * outputDimsBBOX.d[2];

        for (int h = 0; h < grid_y_; h++)
        {
            for (int w = 0; w < grid_x_; w++)
            {
                int i = w + h * grid_x_;
                if (output_cov_buf[class_num * gridsize_ + i] >=
                        g_pModelNetAttr->THRESHOLD[class_num])
                {

                    float rectx1_f, recty1_f, rectx2_f, recty2_f;
                    int rectx1, recty1, rectx2, recty2;

                    rectx1_f = recty1_f = rectx2_f = recty2_f = 0.0;

                    rectx1_f = output_x1[w + h * grid_x_] - gc_centers_0[w];
                    recty1_f = output_y1[w + h * grid_x_] - gc_centers_1[h];
                    rectx2_f = output_x2[w + h * grid_x_] + gc_centers_0[w];
                    recty2_f = output_y2[w + h * grid_x_] + gc_centers_1[h];

                    rectx1_f *= (float)(-bbox_norm[0]);
                    recty1_f *= (float)(-bbox_norm[1]);
                    rectx2_f *= (float)(bbox_norm[0]);
                    recty2_f *= (float)(bbox_norm[1]);

                    rectx1 = (int)rectx1_f;
                    recty1 = (int)recty1_f;
                    rectx2 = (int)rectx2_f;
                    recty2 = (int)recty2_f;

                    rectx1 = rectx1 < 0 ? 0 : (rectx1 >= net_width ? (net_width - 1) : rectx1);
                    rectx2 = rectx2 < 0 ? 0 : (rectx2 >= net_width ? (net_width - 1) : rectx2);
                    recty1 = recty1 < 0 ? 0 : (recty1 >= net_height ? (net_height - 1) : recty1);
                    recty2 = recty2 < 0 ? 0 : (recty2 >= net_height ? (net_height - 1) : recty2);

                    rectList[class_num].push_back(cv::Rect(rectx1, recty1,
                                rectx2 - rectx1, recty2 - recty1));
                }
            }
        }
        cv::groupRectangles(rectList[class_num], 1, 0.1);
    }
}
