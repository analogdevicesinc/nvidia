/*
 * Copyright (c) 2020-2023, NVIDIA CORPORATION. All rights reserved.
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

#include "ArgusHelpers.h"
#include "CommonOptions.h"
#include "Thread.h"
#include <pthread.h>
#include <time.h>
#include <Argus/Argus.h>
#include <EGLStream/EGLStream.h>

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <Argus/Ext/SyncSensorCalibrationData.h>
#include <Argus/Ext/SensorTimestampTsc.h>

using namespace Argus;
using namespace EGLStream;

namespace ArgusSamples
{

// Debug print macros.
#define PERF_PRINT(...) printf("PERF: " __VA_ARGS__)
#define THRESHOLD 100.0f

pthread_mutex_t eventMutex;
pthread_cond_t eventCondition;
/*******************************************************************************
 * Argus perf class
 * This class will analyze frames from all the synchronized sensors
 ******************************************************************************/
class SyncStereoPerfThread : public Thread
{
public:
    explicit SyncStereoPerfThread(std::vector<uint64_t>* buf,
                                  uint8_t sensorCount,
                                  uint16_t *sessionMask)
    {
        modCount = sensorCount;
        perfBuf = buf;
        oosCount = syncCount = 0;
        max_dev = avgDifference = 0;
        sessionsMask = sessionMask;
        PERF_PRINT("performance thread contructed for #%d sensors\n", modCount);
    }
    ~SyncStereoPerfThread() {}

private:
    /** @name Thread methods */
    /**@{*/
    virtual bool threadInitialize();
    virtual bool threadExecute();
    virtual bool threadShutdown();
    /**@}*/

    uint8_t modCount;
    std::vector<uint64_t> *perfBuf;
    bool run;
    uint64_t oosCount;
    uint64_t syncCount;
    uint64_t max_dev;
    uint64_t avgDifference;
    uint16_t *sessionsMask;
};

bool SyncStereoPerfThread::threadInitialize()
{
    int ret = pthread_cond_init(&eventCondition, NULL);
    if (ret) {
        printf(" Error in cond variable init \n");
        return false;
    }

    ret = pthread_mutex_init(&eventMutex, NULL);
    if (ret) {
        printf(" Error in mutex init \n");
        return false;
    }

    PERF_PRINT("Performance thread initialized\n");

    return run = true;
}

bool SyncStereoPerfThread::threadExecute()
{
    while(run)
    {
        uint64_t t_min = __UINT64_MAX__;
        uint64_t t_max = 0, diff_us = 0;
        struct timespec to;

        clock_gettime(CLOCK_REALTIME, &to);
        to.tv_sec += 1;
        pthread_mutex_lock(&eventMutex);
        int err = pthread_cond_timedwait(&eventCondition, &eventMutex, &to);
        if (err == ETIMEDOUT) {
            break;
        }

        if (perfBuf->size() < modCount){
            pthread_mutex_unlock(&eventMutex);
            continue;
        }

        for (int i = 0; i < modCount; i++)
        {
            if (perfBuf->at(i) < t_min)
                t_min = perfBuf->at(i);
            else if (perfBuf->at(i) > t_max)
                t_max = perfBuf->at(i);
        }
        /** Reset the timestamp queue and sensor masks */
        perfBuf->clear();
        *sessionsMask = 0;

        pthread_mutex_unlock(&eventMutex);

        diff_us = (t_max - t_min)/1000;
        avgDifference += diff_us;
        if (diff_us > THRESHOLD) {
            PERF_PRINT(" Out of sync detected! Difference between the earliest received "
                       "frame and latest is %lu us \n", diff_us);
            oosCount++;
            max_dev = max_dev < diff_us ? diff_us : max_dev;
        }
        else{
            syncCount++;
        }
    }
    requestShutdown();
    return true;
}

bool SyncStereoPerfThread::threadShutdown()
{
    PERF_PRINT(" ********************* KPI Summary *************************** \n"
                "Number of out of sync frames captured ##############  %lu \n"
                "Number of synced frames captured ###################  %lu \n"
                "The maximum timestamp difference between the earliest \n"
                "and latest frames across modules has been recorded as %lu us\n"
                "The average timestamp difference between the earliest \n"
                "and latest frames is ################################ %lu us\n"
                ,oosCount, syncCount, max_dev, avgDifference/(oosCount + syncCount));

    run = false;
    return true;
}
}; // ArgusSamples