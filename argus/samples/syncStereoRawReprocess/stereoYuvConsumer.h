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
#ifndef STEREO_YUV_CONSUMER_H
#define STEREO_YUV_CONSUMER_H
#include <Argus/Argus.h>
#include <EGLStream/EGLStream.h>
#include "ArgusHelpers.h"
#include "Thread.h"
#include "EGLGlobal.h"
#include <iostream>
#include <fstream>

namespace ArgusSamples
{
using namespace Argus;
using namespace EGLStream;
#define MAX_MODULE_STRING 32
#define MAX_CAM_DEVICE 6
#define CONSUMER_PRINT(...) printf("CONSUMER: " __VA_ARGS__)
//#define CONSUMER_PRINT(...)
// forward declaration
class StereoYuvConsumerThread;

typedef struct
{
    char moduleName[MAX_MODULE_STRING];
    int camDevice[MAX_CAM_DEVICE];
    ICameraProperties *iCameraProperties[MAX_CAM_DEVICE];
    Argus::SensorMode *sensorMode[MAX_CAM_DEVICE];
    ISensorMode *iSensorMode[MAX_CAM_DEVICE];
    UniqueObj<OutputStream> stream[MAX_CAM_DEVICE];
    UniqueObj<InputStream> inStream[MAX_CAM_DEVICE];
    UniqueObj<CaptureSession> captureSession;
    ICaptureSession* iCaptureSession;
    UniqueObj<OutputStreamSettings> streamSettings;
    UniqueObj<InputStreamSettings> inStreamSettings;
    StereoYuvConsumerThread *stereoYuvConsumer;
    int sensorCount;
    bool initialized;
} ModuleInfo;
/*******************************************************************************
 * Sync Stereo Raw Dump Consumer thread:
 *   Creates a FrameConsumer object to consume frames from a stream and write
 *   .raw/.nvraw files to disk along with Metadata and Calibration Data.
 ******************************************************************************/
class StereoYuvConsumerThread : public Thread
{
public:
    explicit StereoYuvConsumerThread(uint32_t numFramesToSave,
                                IEGLOutputStreamSettings *iEGLStreamSettings,
                                CameraDevice *cameraDevice,
                                ModuleInfo *modInfo,
                                OutputStream *yuvStream,
                                bool isHawkModule)
        : m_numFramesToSave(numFramesToSave),
          m_iEGLStreamSettings(iEGLStreamSettings),
          m_cameraDevice(cameraDevice)
    {
        m_isHawkModule = isHawkModule;
        if (m_isHawkModule)
        {
            m_leftStream = modInfo->stream[0].get();
            if (modInfo->sensorCount > 1)
                m_rightStream = modInfo->stream[1].get();
            else
                m_rightStream = NULL;
            strcpy(m_moduleName, modInfo->moduleName);
        }
        else
        {
            
           m_leftStream = yuvStream;
           m_rightStream = NULL;
        }
    }
    ~StereoYuvConsumerThread()
    {
    }
private:
    /** @name Thread methods */
    /**@{*/
    virtual bool threadInitialize();
    virtual bool threadExecute();
    virtual bool threadShutdown();
    /**@}*/
    uint32_t m_numFramesToSave;
    IEGLOutputStreamSettings* m_iEGLStreamSettings;
    CameraDevice* m_cameraDevice;
    OutputStream *m_leftStream;
    OutputStream *m_rightStream;
    char m_moduleName[MAX_MODULE_STRING];
    UniqueObj<FrameConsumer> m_leftConsumer;
    UniqueObj<FrameConsumer> m_rightConsumer;
    bool m_isHawkModule;
};
} // namespace ArgusSamples
#endif // STEREO_YUV_CONSUMER_H
