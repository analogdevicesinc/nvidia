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

#include <stdio.h>
#include <stdlib.h>
#include <Argus/Argus.h>
#include <EGLStream/EGLStream.h>
#include "ArgusHelpers.h"
#include "CommonOptions.h"

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
 * demosaicOuptut
 * Function: Capture a single/multiple simultaneous image(s) from a camera device and write to a RGB/YUV file
 * Purpose: To demonstrate the most simplistic approach to getting the Argus Framework
 *          running, submitting a capture request, retrieving the resulting image and
 *          then writing the image as a raw image in .RGB/YUV formatted file.
 */

static bool singleCapture(Argus::PixelFormatType pixelFormatType, Argus::CVOutput cvOutput, ArgusSamples::CommonOptions &options)
{
    const uint64_t FIVE_SECONDS_IN_NANOSECONDS = 5000000000;
    char rgbaOutputFileName[] = "argus_demosaicOutputRGBA.rgb";
    char yuvOutputFileName[] = "argus_demosaicOutputYUV.yuv";

    /*
     * Set up Argus API Framework, identify available camera devices, and create
     * a capture session for the first available device
     */

    Argus::UniqueObj<Argus::CameraProvider> cameraProvider(Argus::CameraProvider::create());

    Argus::ICameraProvider *iCameraProvider =
        Argus::interface_cast<Argus::ICameraProvider>(cameraProvider);
    EXIT_IF_NULL(iCameraProvider, "Cannot get core camera provider interface");
    printf("Argus Version: %s\n", iCameraProvider->getVersion().c_str());

    Argus::CameraDevice *device = ArgusSamples::ArgusHelpers::getCameraDevice(
            cameraProvider.get(), options.cameraDeviceIndex());
    Argus::ICameraProperties *iCameraProperties =
        Argus::interface_cast<Argus::ICameraProperties>(device);
    if (!iCameraProperties)
    {
        REPORT_ERROR("Failed to get ICameraProperties interface");
        return EXIT_FAILURE;
    }

    Argus::SensorMode* sensorMode = ArgusSamples::ArgusHelpers::getSensorMode(
            device, options.sensorModeIndex());
    Argus::ISensorMode *iSensorMode =
        Argus::interface_cast<Argus::ISensorMode>(sensorMode);
    if (!iSensorMode)
    {
        REPORT_ERROR("Failed to get sensor mode interface");
        return EXIT_FAILURE;
    }

    printf("Capturing from device %d using sensor mode %d (%dx%d)\n",
           options.cameraDeviceIndex(), options.sensorModeIndex(),
           iSensorMode->getResolution().width(), iSensorMode->getResolution().height());

    Argus::Status status;
    Argus::UniqueObj<Argus::CaptureSession> captureSession(
        iCameraProvider->createCaptureSession(device, &status));
    EXIT_IF_NOT_OK(status, "Failed to create capture session");

    Argus::ICaptureSession *iSession =
        Argus::interface_cast<Argus::ICaptureSession>(captureSession);
    EXIT_IF_NULL(iSession, "Cannot get Capture Session Interface");

    EGLStream::IFrameConsumer *iFrameConsumer;

    /*
     * Creates the stream between the Argus camera image capturing
     * sub-system (producer) and the image acquisition code (consumer).  A consumer object is
     * created from the stream to be used to request the image frame.  A successfully submitted
     * capture request activates the stream's functionality to eventually make a frame available
     * for acquisition.
     */

    Argus::UniqueObj<Argus::OutputStreamSettings> streamSettings(
        iSession->createOutputStreamSettings(Argus::STREAM_TYPE_EGL));

    Argus::IEGLOutputStreamSettings *iEGLStreamSettings =
        Argus::interface_cast<Argus::IEGLOutputStreamSettings>(streamSettings);
    EXIT_IF_NULL(iEGLStreamSettings, "Cannot get IEGLOutputStreamSettings Interface");
    if (pixelFormatType == Argus::PixelFormatType_RgbOnly)
        iEGLStreamSettings->setPixelFormat(Argus::PIXEL_FMT_LegacyRGBA);
    else
        iEGLStreamSettings->setPixelFormat(Argus::PIXEL_FMT_YCbCr_420_888);
    iEGLStreamSettings->setResolution(iSensorMode->getResolution());
    iEGLStreamSettings->setMetadataEnable(true);

    Argus::UniqueObj<Argus::OutputStream> stream(iSession->createOutputStream(streamSettings.get()));
    EXIT_IF_NULL(stream, "Failed to create EGLOutputStream");

    Argus::UniqueObj<EGLStream::FrameConsumer> consumer(
        EGLStream::FrameConsumer::create(stream.get()));

    iFrameConsumer = Argus::interface_cast<EGLStream::IFrameConsumer>(consumer);
    EXIT_IF_NULL(iFrameConsumer, "Failed to initialize Consumer");

    Argus::UniqueObj<Argus::Request> request(
        iSession->createRequest(Argus::CAPTURE_INTENT_STILL_CAPTURE));

    Argus::IRequest *iRequest = Argus::interface_cast<Argus::IRequest>(request);
    EXIT_IF_NULL(iRequest, "Failed to get capture request interface");

    status = iRequest->setPixelFormatType(pixelFormatType);
    EXIT_IF_NOT_OK(status, "Failed to set PixelFormatType");

    status = iRequest->setCVOutput(cvOutput);
    EXIT_IF_NOT_OK(status, "Failed to set CVOutput");

    status = iRequest->enableOutputStream(stream.get());
    EXIT_IF_NOT_OK(status, "Failed to enable stream in capture request");

    Argus::ISourceSettings *iSourceSettings =
        Argus::interface_cast<Argus::ISourceSettings>(request);
    EXIT_IF_NULL(iSourceSettings, "Failed to get source settings request interface");
    iSourceSettings->setSensorMode(sensorMode);

    uint32_t requestId = iSession->capture(request.get());
    EXIT_IF_NULL(requestId, "Failed to submit capture request");

    /*
     * Acquire a frame generated by the capture request, get the image from the frame
     * and create a .rgb/.yuv file of the captured image
     */

    Argus::UniqueObj<EGLStream::Frame> frame(
        iFrameConsumer->acquireFrame(FIVE_SECONDS_IN_NANOSECONDS, &status));

    EGLStream::IFrame *iFrame = Argus::interface_cast<EGLStream::IFrame>(frame);
    EXIT_IF_NULL(iFrame, "Failed to get RGBA IFrame interface");

    EGLStream::Image *image = iFrame->getImage();
    EXIT_IF_NULL(image, "Failed to get RGBA Image from iFrame->getImage()");

    EGLStream::IImage *iImage = Argus::interface_cast<EGLStream::IImage>(image);
    EXIT_IF_NULL(iImage, "Failed to get RGBA IImage");

    EGLStream::IImage2D *iImage2D = Argus::interface_cast<EGLStream::IImage2D>(image);
    EXIT_IF_NULL(iImage2D, "Failed to get RGBA iImage2D");

    EGLStream::IImageHeaderlessFile *iImageHeadelessFile =
        Argus::interface_cast<EGLStream::IImageHeaderlessFile>(image);
    EXIT_IF_NULL(iImageHeadelessFile, "Failed to get RGBA IImageHeaderlessFile");

    if (pixelFormatType == Argus::PixelFormatType_RgbOnly) {
        status = iImageHeadelessFile->writeHeaderlessFile(rgbaOutputFileName);
        printf("Wrote RGBA file : %s\n", rgbaOutputFileName);
    }
    else {
        status = iImageHeadelessFile->writeHeaderlessFile(yuvOutputFileName);
        printf("Wrote YUV file : %s\n", yuvOutputFileName);
    }

    // Shut down Argus.
    cameraProvider.reset();
    return EXIT_SUCCESS;
}

static bool simultaneousCaptures(Argus::PixelFormatType pixelFormatType, Argus::CVOutput cvOutput, ArgusSamples::CommonOptions &options)
{
    const uint64_t FIVE_SECONDS_IN_NANOSECONDS = 5000000000;
    char rgbaOutputFileName[] = "argus_demosaicOutputRGBA.rgb";
    char yuvOutputFileName[] = "argus_demosaicOutputYUV.yuv";

    /*
     * Set up Argus API Framework, identify available camera devices, and create
     * a capture session for the first available device
     */

    Argus::UniqueObj<Argus::CameraProvider> cameraProvider(Argus::CameraProvider::create());

    Argus::ICameraProvider *iCameraProvider =
        Argus::interface_cast<Argus::ICameraProvider>(cameraProvider);
    EXIT_IF_NULL(iCameraProvider, "Cannot get core camera provider interface");
    printf("Argus Version: %s\n", iCameraProvider->getVersion().c_str());

    Argus::CameraDevice *device = ArgusSamples::ArgusHelpers::getCameraDevice(
            cameraProvider.get(), options.cameraDeviceIndex());
    Argus::ICameraProperties *iCameraProperties =
        Argus::interface_cast<Argus::ICameraProperties>(device);
    if (!iCameraProperties)
    {
        REPORT_ERROR("Failed to get ICameraProperties interface");
        return EXIT_FAILURE;
    }

    Argus::SensorMode* sensorMode = ArgusSamples::ArgusHelpers::getSensorMode(
            device, options.sensorModeIndex());
    Argus::ISensorMode *iSensorMode =
        Argus::interface_cast<Argus::ISensorMode>(sensorMode);
    if (!iSensorMode)
    {
        REPORT_ERROR("Failed to get sensor mode interface");
        return EXIT_FAILURE;
    }

    printf("Capturing from device %d using sensor mode %d (%dx%d)\n",
           options.cameraDeviceIndex(), options.sensorModeIndex(),
           iSensorMode->getResolution().width(), iSensorMode->getResolution().height());

    Argus::Status status;
    Argus::UniqueObj<Argus::CaptureSession> captureSession(
        iCameraProvider->createCaptureSession(device, &status));
    EXIT_IF_NOT_OK(status, "Failed to create capture session");

    Argus::ICaptureSession *iSession =
        Argus::interface_cast<Argus::ICaptureSession>(captureSession);
    EXIT_IF_NULL(iSession, "Cannot get Capture Session Interface");

    EGLStream::IFrameConsumer *iFrameRGBAConsumer;
    EGLStream::IFrameConsumer *iFrameYUVConsumer;

    /*
     * Creates the stream between the Argus camera image capturing
     * sub-system (producer) and the image acquisition code (consumer).  A consumer object is
     * created from the stream to be used to request the image frame.  A successfully submitted
     * capture request activates the stream's functionality to eventually make a frame available
     * for acquisition.
     */

    Argus::UniqueObj<Argus::OutputStreamSettings> rgbaStreamSettings(
        iSession->createOutputStreamSettings(Argus::STREAM_TYPE_EGL));

    Argus::IEGLOutputStreamSettings *rgbaIEGLStreamSettings =
        Argus::interface_cast<Argus::IEGLOutputStreamSettings>(rgbaStreamSettings);
    EXIT_IF_NULL(rgbaIEGLStreamSettings, "Cannot get IEGLOutputStreamSettings Interface");
    rgbaIEGLStreamSettings->setPixelFormat(Argus::PIXEL_FMT_LegacyRGBA);
    rgbaIEGLStreamSettings->setResolution(iSensorMode->getResolution());
    rgbaIEGLStreamSettings->setMetadataEnable(true);

    Argus::UniqueObj<Argus::OutputStream> rgbaStream(
        iSession->createOutputStream(rgbaStreamSettings.get()));
    EXIT_IF_NULL(rgbaStream, "Failed to create RGBA EGLOutputStream");

    Argus::UniqueObj<EGLStream::FrameConsumer> rgbaConsumer(
        EGLStream::FrameConsumer::create(rgbaStream.get()));

    iFrameRGBAConsumer = Argus::interface_cast<EGLStream::IFrameConsumer>(rgbaConsumer);
    EXIT_IF_NULL(iFrameRGBAConsumer, "Failed to initialize RGBA Consumer");

    Argus::UniqueObj<Argus::OutputStreamSettings> yuvStreamSettings(
        iSession->createOutputStreamSettings(Argus::STREAM_TYPE_EGL));

    Argus::IEGLOutputStreamSettings *yuvIEGLStreamSettings =
        Argus::interface_cast<Argus::IEGLOutputStreamSettings>(yuvStreamSettings);
    EXIT_IF_NULL(yuvIEGLStreamSettings, "Cannot get IEGLOutputStreamSettings Interface");
    yuvIEGLStreamSettings->setPixelFormat(Argus::PIXEL_FMT_YCbCr_420_888);
    yuvIEGLStreamSettings->setResolution(iSensorMode->getResolution());
    yuvIEGLStreamSettings->setMetadataEnable(true);

    Argus::UniqueObj<Argus::OutputStream> yuvStream(
        iSession->createOutputStream(yuvStreamSettings.get()));
    EXIT_IF_NULL(yuvStream, "Failed to create YUV EGLOutputStream");

    Argus::UniqueObj<EGLStream::FrameConsumer> yuvConsumer(
        EGLStream::FrameConsumer::create(yuvStream.get()));

    iFrameYUVConsumer = Argus::interface_cast<EGLStream::IFrameConsumer>(yuvConsumer);
    EXIT_IF_NULL(iFrameYUVConsumer, "Failed to initialize YUV Consumer");

    Argus::UniqueObj<Argus::Request> request(
        iSession->createRequest(Argus::CAPTURE_INTENT_STILL_CAPTURE));

    Argus::IRequest *iRequest = Argus::interface_cast<Argus::IRequest>(request);
    EXIT_IF_NULL(iRequest, "Failed to get capture request interface");

    status = iRequest->setPixelFormatType(pixelFormatType);
    EXIT_IF_NOT_OK(status, "Failed to set PixelFormatType");

    status = iRequest->setCVOutput(cvOutput);
    EXIT_IF_NOT_OK(status, "Failed to set CVOutput");

    status = iRequest->enableOutputStream(rgbaStream.get());
    EXIT_IF_NOT_OK(status, "Failed to enable RGBA stream in capture request");

    status = iRequest->enableOutputStream(yuvStream.get());
    EXIT_IF_NOT_OK(status, "Failed to enable YUV stream in capture request");

    Argus::ISourceSettings *iSourceSettings =
        Argus::interface_cast<Argus::ISourceSettings>(request);
    EXIT_IF_NULL(iSourceSettings, "Failed to get source settings request interface");
    iSourceSettings->setSensorMode(sensorMode);

    uint32_t requestId = iSession->capture(request.get());
    EXIT_IF_NULL(requestId, "Failed to submit capture request");

    /*
     * Acquire a frame generated by the capture request, get the image from the frame
     * and create a .rgb/.yuv file of the captured image
     */

    //write RGBA ouptut
    Argus::UniqueObj<EGLStream::Frame> rgbaFrame(
        iFrameRGBAConsumer->acquireFrame(FIVE_SECONDS_IN_NANOSECONDS, &status));

    EGLStream::IFrame *rgbaIFrame = Argus::interface_cast<EGLStream::IFrame>(rgbaFrame);
    EXIT_IF_NULL(rgbaIFrame, "Failed to get RGBA IFrame interface");

    EGLStream::Image *rgbaImage = rgbaIFrame->getImage();
    EXIT_IF_NULL(rgbaImage, "Failed to get RGBA Image from iFrame->getImage()");

    EGLStream::IImage *rgbaIImage = Argus::interface_cast<EGLStream::IImage>(rgbaImage);
    EXIT_IF_NULL(rgbaIImage, "Failed to get RGBA IImage");

    EGLStream::IImage2D *rgbaIImage2D = Argus::interface_cast<EGLStream::IImage2D>(rgbaImage);
    EXIT_IF_NULL(rgbaIImage2D, "Failed to get RGBA iImage2D");

    EGLStream::IImageHeaderlessFile *rgbaIImageHeadelessFile =
        Argus::interface_cast<EGLStream::IImageHeaderlessFile>(rgbaImage);
    EXIT_IF_NULL(rgbaIImageHeadelessFile, "Failed to get RGBA IImageHeaderlessFile");

    status = rgbaIImageHeadelessFile->writeHeaderlessFile(rgbaOutputFileName);
    EXIT_IF_NOT_OK(status, "Failed to write RGB File");
    printf("Wrote RGBA file : %s\n", rgbaOutputFileName);

    //write YUV output
    Argus::UniqueObj<EGLStream::Frame> yuvFrame(
        iFrameYUVConsumer->acquireFrame(FIVE_SECONDS_IN_NANOSECONDS, &status));

    EGLStream::IFrame *yuvIFrame = Argus::interface_cast<EGLStream::IFrame>(yuvFrame);
    EXIT_IF_NULL(yuvIFrame, "Failed to get YUV IFrame interface");

    EGLStream::Image *yuvImage = yuvIFrame->getImage();
    EXIT_IF_NULL(yuvImage, "Failed to get YUV Image from iFrame->getImage()");

    EGLStream::IImage *yuvIImage = Argus::interface_cast<EGLStream::IImage>(yuvImage);
    EXIT_IF_NULL(yuvIImage, "Failed to get YUV IImage");

    EGLStream::IImage2D *yuvIImage2D = Argus::interface_cast<EGLStream::IImage2D>(yuvImage);
    EXIT_IF_NULL(yuvIImage2D, "Failed to get YUV iImage2D");

    EGLStream::IImageHeaderlessFile *yuvIImageHeadelessFile =
        Argus::interface_cast<EGLStream::IImageHeaderlessFile>(yuvImage);
    EXIT_IF_NULL(yuvIImageHeadelessFile, "Failed to get YUV IImageHeaderlessFile");

    status = yuvIImageHeadelessFile->writeHeaderlessFile(yuvOutputFileName);
    EXIT_IF_NOT_OK(status, "Failed to write YUV File");
    printf("Wrote YUV file : %s\n", yuvOutputFileName);

    // Shut down Argus.
    cameraProvider.reset();
    return EXIT_SUCCESS;
}

int main(int argc, char** argv)
{
    ArgusSamples::CommonOptions options(basename(argv[0]),
                                        ArgusSamples::CommonOptions::Option_D_CameraDevice |
                                        ArgusSamples::CommonOptions::Option_M_SensorMode |
                                        ArgusSamples::CommonOptions::Option_C_CvOutput |
                                        ArgusSamples::CommonOptions::Option_I_PixelFormatType);
    if (!options.parse(argc, argv))
        return EXIT_FAILURE;
    if (options.requestedExit())
        return EXIT_SUCCESS;

    Argus::PixelFormatType pixelFormatType = Argus::PixelFormatType_None;
    Argus::CVOutput cvOutput = Argus::CVOutput_None;
    printf("Capturing RGBA/YUV Image with CVOutput(%d) & PixelFormatType(%d)\n",
        options.cvOutputIndex(), options.pixelFormatTypeIndex());


    switch (options.pixelFormatTypeIndex()) {
        case 0:
            pixelFormatType = Argus::PixelFormatType_YuvOnly;
            printf("Using default as PixelFormatType_YuvOnly\n");
            break;
        case 1:
            pixelFormatType = Argus::PixelFormatType_RgbOnly;
            printf("Using PixelFormatType_RgbOnly\n");
            break;
        case 2:
            pixelFormatType = Argus::PixelFormatType_Both;
            printf("Using PixelFormatType_Bothn");
            break;
        default:
            REPORT_ERROR("Pixel Format Type Index should be in range [0,2]");
            return EXIT_FAILURE;
    }

    switch (options.cvOutputIndex()) {
        case 0:
            if (pixelFormatType != Argus::PixelFormatType_YuvOnly) {
                REPORT_ERROR("No CVOuput enabled and main isp output must be Yuv format");
                return EXIT_FAILURE;
            }
            cvOutput = Argus::CVOutput_None;
            break;
        case 1:
            if (pixelFormatType == Argus::PixelFormatType_YuvOnly) {
                REPORT_ERROR("Wrong PixelFormatType(Yuv) and CVOutput(Linear) combination");
                return EXIT_FAILURE;
            }
            cvOutput = Argus::CVOutput_Linear;
            break;
        case 2:
            REPORT_ERROR("Non Linear Ouptut is not supported for CVOutput");
                return EXIT_FAILURE;
        default:
            REPORT_ERROR("Cv Output Port Index should be in range [0,2]");
            return EXIT_FAILURE;
    }

    if (pixelFormatType == Argus::PixelFormatType_Both) {
        if (!simultaneousCaptures(pixelFormatType, cvOutput, options))
            return EXIT_FAILURE;
    }
    else {
        if (!singleCapture(pixelFormatType, cvOutput, options))
            return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
