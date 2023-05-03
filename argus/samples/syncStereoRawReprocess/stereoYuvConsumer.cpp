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
#include "stereoYuvConsumer.h"
#include "Error.h"
#include <Argus/Argus.h>
#include <Argus/Ext/SyncSensorCalibrationData.h>
#include <string.h>
#include <stdlib.h>
#include <sstream>
#include <iomanip>
namespace ArgusSamples
{
#ifdef ANDROID
#define YUV_DATA_PREFIX "/sdcard/DCIM/Argus_"
#else
#define YUV_DATA_PREFIX "Argus_"
#endif
std::ofstream m_rawDataOutputFile;
bool StereoYuvConsumerThread::threadInitialize()
{
    // Create the FrameConsumers.
    CONSUMER_PRINT("Creating FrameConsumer for left stream\n");
    m_leftConsumer = UniqueObj<FrameConsumer>(FrameConsumer::create(m_leftStream));
    if (!m_leftConsumer)
        ORIGINATE_ERROR("Failed to create FrameConsumer for left stream");
    if (m_rightStream)
    {
        CONSUMER_PRINT("Creating FrameConsumer for right stream\n");
        m_rightConsumer = UniqueObj<FrameConsumer>(FrameConsumer::create(m_rightStream));
        if (!m_rightConsumer)
            ORIGINATE_ERROR("Failed to create FrameConsumer for right stream");
    }

    m_rawDataOutputFile.open("MetaData.txt");
    if (!m_rawDataOutputFile)
        ORIGINATE_ERROR("Failed to open RawDataOutputFile");
    return true;
}

bool StereoYuvConsumerThread::threadExecute()
{
    IEGLOutputStream *iLeftStream = interface_cast<IEGLOutputStream>(m_leftStream);
    IFrameConsumer* iFrameConsumerLeft = NULL;
    iFrameConsumerLeft = interface_cast<IFrameConsumer>(m_leftConsumer);
    // Wait until the producer has connected to the stream.
    if (!iFrameConsumerLeft)
    {
        ORIGINATE_ERROR("Failed to get left iFrameConsumerLeft interface.");
    }

    CONSUMER_PRINT("Waiting until Argus producer is connected to left stream...\n");
    if (iLeftStream->waitUntilConnected() != STATUS_OK)
        ORIGINATE_ERROR("Argus producer failed to connect to left stream.");
    CONSUMER_PRINT("Argus producer for left stream has connected; continuing.\n");

    IFrameConsumer* iFrameConsumerRight = NULL;
    if (m_rightStream)
    {
        IEGLOutputStream *iRightStream = interface_cast<IEGLOutputStream>(m_rightStream);
        iFrameConsumerRight = interface_cast<IFrameConsumer>(m_rightConsumer);
        if (!iFrameConsumerLeft)
        {
            ORIGINATE_ERROR("Failed to get left iFrameConsumerLeft interface.");
        }
        // Wait until the producer has connected to the stream.
        CONSUMER_PRINT("Waiting until Argus producer is connected to right stream...\n");
        if (iRightStream->waitUntilConnected() != STATUS_OK)
            ORIGINATE_ERROR("Argus producer failed to connect to right stream.");
        CONSUMER_PRINT("Argus producer for right stream has connected; continuing.\n");
    }

    uint32_t numSavedLeftFrames = 0;
    uint32_t numSavedRightFrames = 0;
    uint32_t numCapturedFrames = 0;

    while (numCapturedFrames < m_numFramesToSave)
    {
        // Acquire a Frame.
        UniqueObj<Frame> frameleft(iFrameConsumerLeft->acquireFrame());
        if (!frameleft)
        {
            CONSUMER_PRINT("Acquiring left Frame no is failed \n");
            break;
        }
        // Use the IFrame interface to print out the frame number/timestamp, and
        // to provide access to the Image in the Frame.
        IFrame *iFrameLeft = interface_cast<IFrame>(frameleft);
        if (!iFrameLeft)
        {
            ORIGINATE_ERROR("Failed to get left IFrame interface.");
        }
        CONSUMER_PRINT("Acquired Left Frame: %llu, time %llu \n",
            static_cast<unsigned long long>(iFrameLeft->getNumber()),
            static_cast<unsigned long long>(iFrameLeft->getTime()));

        CaptureMetadata* captureMetadataLeft =
                interface_cast<IArgusCaptureMetadata>(frameleft)->getMetadata();
        ICaptureMetadata* iMetadataLeft = interface_cast<ICaptureMetadata>(captureMetadataLeft);
        if (!iMetadataLeft)
        {
            ORIGINATE_ERROR("Cannot get metadata for left frame");
        }
        IFrame *iFrameRight = nullptr;
        if (iFrameConsumerRight)
        {
            UniqueObj<Frame> frameright(iFrameConsumerRight->acquireFrame());
            if (!frameright)
            {
                CONSUMER_PRINT("Acquiring right Frame no is failed\n");
                break;
            }
            // Use the IFrame interface to print out the frame number/timestamp, and
            // to provide access to the Image in the Frame.
            iFrameRight = interface_cast<IFrame>(frameright);
            if (!iFrameRight)
                ORIGINATE_ERROR("Failed to get right IFrame interface.");
            CONSUMER_PRINT("Acquired Right Frame: %llu, time %llu\n",
                static_cast<unsigned long long>(iFrameRight->getNumber()),
                static_cast<unsigned long long>(iFrameRight->getTime()));
        }
        numCapturedFrames++;

        EGLStream::Image *leftImage = nullptr;
        EGLStream::Image *rightImage = nullptr;
        // Get the Frame's Image.
        leftImage = iFrameLeft->getImage();
        if(!leftImage)
            ORIGINATE_ERROR("Failed to get Left Bayer Image from iFrame->getImage()");
        if (iFrameRight)
        {
            rightImage = iFrameRight->getImage();
            if(!rightImage)
                ORIGINATE_ERROR("Failed to get Right Bayer Image from iFrame->getImage()");
        }

        // Write the Left Yuv Image to disk
        std::ostringstream fileNameLeft;
        fileNameLeft << YUV_DATA_PREFIX;
        fileNameLeft << "LeftYuv_";
        fileNameLeft << std::setfill('0') << std::setw(4) << iFrameLeft->getNumber() << ".yuv";
        EGLStream::IImageHeaderlessFile *leftIImageHeadelessFile =
            Argus::interface_cast<EGLStream::IImageHeaderlessFile>(leftImage);
        if(!leftIImageHeadelessFile)
            ORIGINATE_ERROR("Failed to get Left IImageHeaderlessFile");
        if (iFrameLeft->getNumber() == m_numFramesToSave)
        {
            ///todo: this file writing operation should be made asyncs
            if (leftIImageHeadelessFile->writeHeaderlessFile(fileNameLeft.str().c_str()) == STATUS_OK)
            {
                CONSUMER_PRINT("Captured a yuv image to '%s'\n", fileNameLeft.str().c_str());
                numSavedLeftFrames++;
                m_rawDataOutputFile << "fileNameLeft = " <<
                    fileNameLeft.str() << ";\n";
                m_rawDataOutputFile << "FrameNumberLeft = " <<
                    iFrameLeft->getNumber() << ";\n";
            }
            else
            {
                ORIGINATE_ERROR("Failed to write Yuv to '%s'\n", fileNameLeft.str().c_str());
            }
        }
        if (iFrameRight)
        {
            // Write the Right Image to disk as .yuv file
            std::ostringstream fileNameRight;
            fileNameRight << YUV_DATA_PREFIX;
            fileNameRight << "RightYuv_";
            fileNameRight << std::setfill('0') << std::setw(4) << iFrameRight->getNumber() << ".yuv";
            EGLStream::IImageHeaderlessFile *rightIImageHeadelessFile =
                Argus::interface_cast<EGLStream::IImageHeaderlessFile>(rightImage);
            if(!rightIImageHeadelessFile)
                ORIGINATE_ERROR("Failed to get Right IImageHeaderlessFile");
            if (iFrameLeft->getNumber() == m_numFramesToSave)
            {
         
                if (rightIImageHeadelessFile->writeHeaderlessFile(fileNameRight.str().c_str()) == STATUS_OK)
                {
                    numSavedRightFrames++;
                    CONSUMER_PRINT("Captured a yuv image to '%s'\n", fileNameRight.str().c_str());
                    m_rawDataOutputFile << "fileNameRight = " <<
                        fileNameRight.str() << ";\n";
                    m_rawDataOutputFile << "FrameNumberRight = " <<
                        iFrameRight->getNumber() << ";\n";
                }
                else
                {
                    ORIGINATE_ERROR("Failed to write Yuv to '%s'\n", fileNameRight.str().c_str());
                }
            }
        }
    }
    CONSUMER_PRINT("Saved %u left frame(s) and %u right frame(s)\n",
        numSavedLeftFrames, numSavedRightFrames);
    CONSUMER_PRINT("Cpatured %u frame(s)\n", numCapturedFrames);
    PROPAGATE_ERROR(requestShutdown());
    return true;
}
bool StereoYuvConsumerThread::threadShutdown()
{
    m_rawDataOutputFile.close();
    CONSUMER_PRINT("Done.\n");
    return true;
}
} // namespace ArgusSamples
