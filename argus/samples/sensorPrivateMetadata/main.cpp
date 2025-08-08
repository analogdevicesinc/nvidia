/*
 * Copyright (c) 2021, NVIDIA CORPORATION. All rights reserved.
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

#include <stdio.h>
#include <stdlib.h>
#include <Argus/Argus.h>
#include "ArgusHelpers.h"
#include "CommonOptions.h"
#include <EGLStream/EGLStream.h>
#include <Argus/Ext/SensorPrivateMetadata.h>

#define EXIT_IF_NULL(val,msg)   \
        {if (!val) {printf("%s\n", msg); return EXIT_FAILURE;}}
#define EXIT_IF_NOT_OK(val,msg) \
        {if (val != Argus::STATUS_OK) {printf("%s\n", msg); return EXIT_FAILURE;}}

/*
 * Program: sensorPrivateMetadata
 * Function: Check if Private sensor metadata is supported or not
 * Purpose: To demonstrate the most simplistic approach to get the Argus Framework
 *          running, check if private metadata is supported by the sensor.
 *          To verify if sensor private metadata is being updated,
 *          non-zero number of bytes confirms it.
 */

int main(int argc, char** argv)
{
    ArgusSamples::CommonOptions options(basename(argv[0]),
                                        ArgusSamples::CommonOptions::Option_D_CameraDevice |
                                        ArgusSamples::CommonOptions::Option_M_SensorMode);
    if (!options.parse(argc, argv))
        return EXIT_FAILURE;
    if (options.requestedExit())
        return EXIT_SUCCESS;

    const uint64_t FIVE_SECONDS_IN_NANOSECONDS = 5000000000;

    /*
     * Set up Argus API Framework, identify available camera devices,
     * check if private sensor metadata is supported or not
     */

    Argus::UniqueObj<Argus::CameraProvider> cameraProvider(Argus::CameraProvider::create());

    Argus::ICameraProvider *iCameraProvider =
        Argus::interface_cast<Argus::ICameraProvider>(cameraProvider);

    EXIT_IF_NULL(iCameraProvider, "Failed to get ICameraProvider interface.");
    printf("Argus Version: %s\n", iCameraProvider->getVersion().c_str());

    Argus::CameraDevice *device = ArgusSamples::ArgusHelpers::getCameraDevice(
            cameraProvider.get(), options.cameraDeviceIndex());

    EXIT_IF_NULL(device, "Selected camera device is not available");

    // creating ISensorPrivateMetadataCaps interface
    Argus::Ext::ISensorPrivateMetadataCaps *iSensorMetadataCaps =
            Argus::interface_cast<Argus::Ext::ISensorPrivateMetadataCaps> (device);

    EXIT_IF_NULL(iSensorMetadataCaps, "Failed to get ISensorPrivateMetadataCaps interface."
                             " Sensor private metadata not supported.");

    printf("Sensor private metadata is supported.\n");

    size_t metadataSize = iSensorMetadataCaps->getMetadataSize();

    // printing size of metadata
    printf("Size of sensor private metadata = %ld bytes.\n",metadataSize);

    // creating ICameraProperties interface
    Argus::ICameraProperties *iCameraProperties =
        Argus::interface_cast<Argus::ICameraProperties>(device);

    EXIT_IF_NULL(iCameraProperties,"Failed to get ICameraProperties interface.");

    // creating ISensorMode interface
    Argus::SensorMode* sensorMode = ArgusSamples::ArgusHelpers::getSensorMode(
            device, options.sensorModeIndex());

    Argus::ISensorMode *iSensorMode =
        Argus::interface_cast<Argus::ISensorMode>(sensorMode);

    EXIT_IF_NULL(iSensorMode,"Failed to get ISensorMode interface.");

    // creating capture session
    Argus::Status status;
    Argus::UniqueObj<Argus::CaptureSession> captureSession(
                        iCameraProvider->createCaptureSession(device, &status));

    EXIT_IF_NOT_OK(status, "Failed to create capture session!");

    // capture session interface
    Argus::ICaptureSession *iSession =
        Argus::interface_cast<Argus::ICaptureSession>(captureSession);

    EXIT_IF_NULL(iSession, "Failed to get ICaptureSession interface.");

    // creating OutputStreamSettings interface
    Argus::UniqueObj<Argus::OutputStreamSettings> streamSettings(
        iSession->createOutputStreamSettings(Argus::STREAM_TYPE_EGL));

    Argus::IEGLOutputStreamSettings *iEGLStreamSettings =
        Argus::interface_cast<Argus::IEGLOutputStreamSettings>(streamSettings);

    EXIT_IF_NULL(iEGLStreamSettings, "Failed to get IEGLOutputStreamSettings interface.");

    iEGLStreamSettings->setPixelFormat(Argus::PIXEL_FMT_YCbCr_420_888);
    iEGLStreamSettings->setResolution(iSensorMode->getResolution());
    iEGLStreamSettings->setMetadataEnable(true);

    Argus::UniqueObj<Argus::OutputStream> stream(
        iSession->createOutputStream(streamSettings.get()));

    EXIT_IF_NULL(stream, "Failed to create EGLOutputStream.");

    // creating request
    Argus::UniqueObj<Argus::Request> request(
        iSession->createRequest(Argus::CAPTURE_INTENT_STILL_CAPTURE));

    // Enabling the output of sensor private metadata for a request
    Argus::Ext::ISensorPrivateMetadataRequest *iSensorMetadataRequest =
            Argus::interface_cast<Argus::Ext::ISensorPrivateMetadataRequest>(request);

    EXIT_IF_NULL(iSensorMetadataRequest, "Failed to get ISensorPrivateMetadataRequest interface.");
    iSensorMetadataRequest->setMetadataEnable(true);

    if(!iSensorMetadataRequest->getMetadataEnable()){
        printf("Sensor private metadata is not enabled.\n");
        return EXIT_FAILURE;
    }

    // creating request interface
    Argus::IRequest *iRequest = Argus::interface_cast<Argus::IRequest>(request);
    EXIT_IF_NULL(iRequest, "Failed to get IRequest interface.")

    // enabling output stream from iRequest
    status = iRequest->enableOutputStream(stream.get());
    EXIT_IF_NOT_OK(status, "IRequest::enableOutputStream() returned NULL.");

    // creating FrameConsumer object
    Argus::UniqueObj<EGLStream::FrameConsumer> consumer(
        EGLStream::FrameConsumer::create(stream.get()));

    // creating IFrameConsumer interface
    EGLStream::IFrameConsumer *iFrameConsumer =
        Argus::interface_cast<EGLStream::IFrameConsumer>(consumer);

    EXIT_IF_NULL(iFrameConsumer, "Failed to initialize IFrameConsumer interface.");

    // creating ISourceSettings interface
    Argus::ISourceSettings *iSourceSettings =
          Argus::interface_cast<Argus::ISourceSettings>(request);

    EXIT_IF_NULL(iSourceSettings, "Failed to get ISourceSettings interface.");

    iSourceSettings->setSensorMode(sensorMode);

    uint32_t requestId = iSession->capture(request.get());
    EXIT_IF_NULL(requestId, "ISession::capture() returned NULL.");

    // Acquiring a frame from EGLStream
    Argus::UniqueObj<EGLStream::Frame> frame(
        iFrameConsumer->acquireFrame(FIVE_SECONDS_IN_NANOSECONDS, &status));

    // creating IArgusCaptureMetadata to capture metadata from EGL frame
    EGLStream::IArgusCaptureMetadata *iArgusCaptureMetadata =
        Argus::interface_cast<EGLStream::IArgusCaptureMetadata>(frame);

    EXIT_IF_NULL(iArgusCaptureMetadata, "Failed to get IArgusCaptureMetadata interface.");

    Argus::CaptureMetadata* metadata = iArgusCaptureMetadata->getMetadata();

    EXIT_IF_NULL(metadata, "IArgusCaptureMetadata::getMetadata() returned NULL.");

    // Accessing sensor private metadata
    Argus::Ext::ISensorPrivateMetadata *iSensorMetadata =
            Argus::interface_cast<Argus::Ext::ISensorPrivateMetadata>(metadata);

    EXIT_IF_NULL(iSensorMetadata, "Failed to get ISensorPrivateMetadata interface.");

    char* address = new char[metadataSize];
    EXIT_IF_NULL(address, "Failed to allocate memory for metadata.");
    memset(address, 0, metadataSize);

    status = iSensorMetadata->getMetadata(address, metadataSize);
    EXIT_IF_NOT_OK(status, "getMetadata() function returned STATUS_NOT_OK.");

    bool flag = false;
    for(int i = 0; i < (int)metadataSize; i++){
        if(address[i] != 0){
            flag = true;
            break;
        }
    }

    if(flag){
        printf("Sensor private metadata contains non-zero values.\n");
    }
    else{
        printf("Sensor private metadata contains all zero values.\n");
    }

    // free address
    delete[] address;

    // Shut down Argus.
    cameraProvider.reset();

    return EXIT_SUCCESS;
}
