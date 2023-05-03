/*
 * Copyright (c) 2021-2022, NVIDIA CORPORATION. All rights reserved.
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
#include "Error.h"
#include "EGLGlobal.h"
#include "GLContext.h"
#include "JPEGConsumer.h"
#include "PreviewConsumer.h"
#include "Window.h"
#include "Thread.h"
#include <Argus/Argus.h>
#include <unistd.h>
#include <stdlib.h>
#include <sstream>
#include <iomanip>


#define EXIT_IF_NULL(val,msg)   \
        {if (!val) {printf("%s\n",msg); return EXIT_FAILURE;}}
#define EXIT_IF_NOT_OK(val,msg) \
        {if (val!=Argus::STATUS_OK) {printf("%s\n",msg); return EXIT_FAILURE;}}

#ifdef ANDROID
#define FILE_PREFIX "/sdcard/DCIM/"
#else
#define FILE_PREFIX ""
#endif

/*
 * Program: yuvOneShot
 * Function: Capture a single image from a camera device with a specified Pixel Format and write to a YUV file
 * Purpose: To demonstrate the most simplistic approach to getting the Argus Framework
 *          running, submitting a capture request, retrieving the resulting image and
 *          then writing the image as a .yuv formatted file.
 * Pixel Formats: 0 (Default PIXEL_FMT_YCbCr_444_888), 1 (PIXEL_FMT_YCbCr_420_888)
 */

using namespace Argus;

namespace ArgusSamples
{

// Globals.
EGLDisplayHolder g_display;

// Debug print macros.
#define PRODUCER_PRINT(...)         printf("PRODUCER: " __VA_ARGS__)

/*******************************************************************************
 * Extended options class to add additional options specific to this sample.
 ******************************************************************************/
class YuvOneShotSampleOptions : public CommonOptions
{
public:
    YuvOneShotSampleOptions(const char *programName)
        : CommonOptions(programName,
                        ArgusSamples::CommonOptions::Option_D_CameraDevice |
                        ArgusSamples::CommonOptions::Option_M_SensorMode |
                        ArgusSamples::CommonOptions::Option_R_WindowRect |
                        ArgusSamples::CommonOptions::Option_T_CaptureTime |
                        ArgusSamples::CommonOptions::Option_P_PixelFormat)
    {}
};

/*******************************************************************************
 * Argus Producer thread:
 *   Opens the Argus camera driver, creates two OutputStreams -- one for live
 *   preview to display and the other to write YUV file -- and submits capture
 *   requests. Repeat capture is used such that only 1 .yuv file is written and
 *   preview runs in a different stream.
 ******************************************************************************/
static bool execute(const YuvOneShotSampleOptions& options)
{
    const uint64_t FIVE_SECONDS_IN_NANOSECONDS = 5000000000;
    char yuvOutputFileName[] = "argus_demosaicOutputYUV.yuv";

    // Initialize the window and EGL display.
    Window &window = Window::getInstance();
    window.setWindowRect(options.windowRect());
    PROPAGATE_ERROR(g_display.initialize(window.getEGLNativeDisplay()));

    // Initialize the Argus camera provider.
    UniqueObj<CameraProvider> cameraProvider(CameraProvider::create());
    ICameraProvider *iCameraProvider = interface_cast<ICameraProvider>(cameraProvider);
    if (!iCameraProvider)
        ORIGINATE_ERROR("Failed to get ICameraProvider interface");
    printf("Argus Version: %s\n", iCameraProvider->getVersion().c_str());

    // Get the selected camera device and sensor mode.
    CameraDevice* cameraDevice = ArgusHelpers::getCameraDevice(
            cameraProvider.get(), options.cameraDeviceIndex());
    if (!cameraDevice)
        ORIGINATE_ERROR("Selected camera device is not available");
    SensorMode* sensorMode = ArgusHelpers::getSensorMode(cameraDevice, options.sensorModeIndex());
    ISensorMode *iSensorMode = interface_cast<ISensorMode>(sensorMode);
    if (!iSensorMode)
        ORIGINATE_ERROR("Selected sensor mode not available");

    printf("Capturing from device %d using sensor mode %d (%dx%d) and Pixel format %d\n",
           options.cameraDeviceIndex(), options.sensorModeIndex(),
           iSensorMode->getResolution().width(), iSensorMode->getResolution().height(), options.pixelFormatIndex());

    // Create the capture session.
    UniqueObj<CaptureSession> captureSession(iCameraProvider->createCaptureSession(cameraDevice));
    ICaptureSession *iCaptureSession = interface_cast<ICaptureSession>(captureSession);
    if (!iCaptureSession)
        ORIGINATE_ERROR("Failed to create CaptureSession");

    // Create the stream settings and set the common properties.
    UniqueObj<OutputStreamSettings> streamSettings(
        iCaptureSession->createOutputStreamSettings(STREAM_TYPE_EGL));
    IEGLOutputStreamSettings *iStreamSettings =
        interface_cast<IEGLOutputStreamSettings>(streamSettings);
    if (!iStreamSettings)
        ORIGINATE_ERROR("Failed to create OutputStreamSettings");
    /**
     * Set Pixel Format to that specified by the User. PIXEL_FMT_YCbCr_444_888 is Default Format.
     */
    if (options.pixelFormatIndex()) {
        iStreamSettings->setPixelFormat(Argus::PIXEL_FMT_YCbCr_420_888);
    } else {
        iStreamSettings->setPixelFormat(Argus::PIXEL_FMT_YCbCr_444_888);
    }
    iStreamSettings->setEGLDisplay(g_display.get());

    // Create window rect sized OutputStream that is consumed by the preview (OpenGL) consumer.
    PRODUCER_PRINT("Creating preview output stream\n");
    iStreamSettings->setResolution(Size2D<uint32_t>(options.windowRect().width(),
                                                    options.windowRect().height()));
    UniqueObj<OutputStream> previewStream(
            iCaptureSession->createOutputStream(streamSettings.get()));
    IEGLOutputStream *iPreviewStream = interface_cast<IEGLOutputStream>(previewStream);
    if (!iPreviewStream)
        ORIGINATE_ERROR("Failed to create OutputStream");

    PRODUCER_PRINT("Launching preview consumer thread\n");
    PreviewConsumerThread previewConsumerThread(iPreviewStream->getEGLDisplay(),
                                                iPreviewStream->getEGLStream());
    PROPAGATE_ERROR(previewConsumerThread.initialize());
    PROPAGATE_ERROR(previewConsumerThread.waitRunning());

    // Create a full-resolution OutputStream that is consumed by the JPEG Consumer.
    PRODUCER_PRINT("Creating YUV output stream\n");
    iStreamSettings->setResolution(iSensorMode->getResolution());
    iStreamSettings->setMetadataEnable(true);

    Argus::UniqueObj<Argus::OutputStream> stream(
        iCaptureSession->createOutputStream(streamSettings.get()));
    EXIT_IF_NULL(stream, "Failed to create EGLOutputStream");

    Argus::UniqueObj<EGLStream::FrameConsumer> consumer(
        EGLStream::FrameConsumer::create(stream.get()));

    EGLStream::IFrameConsumer *iFrameConsumer =
        Argus::interface_cast<EGLStream::IFrameConsumer>(consumer);
    EXIT_IF_NULL(iFrameConsumer, "Failed to initialize Consumer");

    Argus::UniqueObj<Argus::Request> request(
        iCaptureSession->createRequest(Argus::CAPTURE_INTENT_STILL_CAPTURE));

    Argus::IRequest *iRequest = Argus::interface_cast<Argus::IRequest>(request);
    EXIT_IF_NULL(iRequest, "Failed to get capture request interface");

    Argus::Status status;
    status = iRequest->enableOutputStream(stream.get());
    EXIT_IF_NOT_OK(status, "Failed to enable YUV stream in capture request");
    status =iRequest->enableOutputStream(previewStream.get());
    EXIT_IF_NOT_OK(status, "Failed to enable Preview stream in capture request");

    Argus::ISourceSettings *iSourceSettings =
        Argus::interface_cast<Argus::ISourceSettings>(request);
    EXIT_IF_NULL(iSourceSettings, "Failed to get source settings request interface");
    iSourceSettings->setSensorMode(sensorMode);

    status = iCaptureSession->repeat(request.get());
    EXIT_IF_NOT_OK(status, "Failed to submit capture request");

    //write YUV output
    Argus::UniqueObj<EGLStream::Frame> yuvFrame(
        iFrameConsumer->acquireFrame(FIVE_SECONDS_IN_NANOSECONDS, &status));

    EGLStream::IFrame *yuvIFrame = Argus::interface_cast<EGLStream::IFrame>(yuvFrame);
    EXIT_IF_NULL(yuvIFrame, "Failed to get YUV IFrame interface");

    EGLStream::Image *yuvImage = yuvIFrame->getImage();
    EXIT_IF_NULL(yuvImage, "Failed to get YUV Image from iFrame->getImage()");

    EGLStream::IImage *yuvIImage = Argus::interface_cast<EGLStream::IImage>(yuvImage);
    EXIT_IF_NULL(yuvIImage, "Failed to get YUV IImage");

    EGLStream::IImage2D *yuvIImage2D = Argus::interface_cast<EGLStream::IImage2D>(yuvImage);
    EXIT_IF_NULL(yuvIImage2D, "Failed to get YUV iImage2D");

    EGLStream::IImageHeaderlessFile *yuvIImageHeaderlessFile =
        Argus::interface_cast<EGLStream::IImageHeaderlessFile>(yuvImage);
    EXIT_IF_NULL(yuvIImageHeaderlessFile, "Failed to get YUV IImageHeaderlessFile");

    status = yuvIImageHeaderlessFile->writeHeaderlessFile(yuvOutputFileName);
    printf("Wrote YUV file : %s\n", yuvOutputFileName);

    // Wait for CAPTURE_TIME seconds.
    PROPAGATE_ERROR(window.pollingSleep(options.captureTime()));

    // Stop the repeating request and wait for idle.
    iCaptureSession->stopRepeat();
    iCaptureSession->waitForIdle();

    // Destroy the output streams (stops consumer threads).
    previewStream.reset();

    // Wait for the consumer threads to complete.
    PROPAGATE_ERROR(previewConsumerThread.shutdown());

    // Shut down Argus.
    cameraProvider.reset();

    // Shut down the window (destroys window's EGLSurface).
    window.shutdown();

    // Cleanup the EGL display
    PROPAGATE_ERROR(g_display.cleanup());

    PRODUCER_PRINT("Done -- exiting.\n");

    return true;
}

}; // namespace ArgusSamples

int main(int argc, char** argv)
{
    ArgusSamples::YuvOneShotSampleOptions options(basename(argv[0]));
    if (!options.parse(argc, argv))
        return EXIT_FAILURE;
    if (options.requestedExit())
        return EXIT_SUCCESS;

    if (!ArgusSamples::execute(options))
        return EXIT_FAILURE;

    return EXIT_SUCCESS;
}
