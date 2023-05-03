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
#include <Argus/Argus.h>
#include "ArgusHelpers.h"
#include "CommonOptions.h"
#include "Error.h"
#include "stereoYuvConsumer.h"
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string.h>
#include <sstream>
#include <iomanip>
#include <EGLStream/FrameProducer.h>
#include "CommonOptions.h"

using namespace Argus;
using namespace EGLStream;
namespace ArgusSamples
{
#define EXIT_IF_TRUE(val,msg)   \
        {if ((val)) {USER_AUTO_EXPOSURE_PRINT("%s\n",msg); return false;}}

#define EXIT_IF_NULL(val,msg)   \
        {if (!val) {printf("%s\n",msg); return EXIT_FAILURE;}}
#define EXIT_IF_NOT_OK(val,msg) \
        {if (val!=Argus::STATUS_OK) {printf("%s\n",msg); return EXIT_FAILURE;}}
/*
 * This sample opens sessions based on the number of stereo/sensor modules
 * connected. Each module can have 1 sensor or multiple (2) sensors connected.
 * The processing of the images happens in the worker thread, while the main
 * app thread is used to drive the captures.
 */
// Constants.
static const Size2D<uint32_t> STREAM_SIZE(640, 480);
/// Fix the FPS to 30.
static const uint64_t    DEFAULT_FRAME_DURATION = 33000000U;
static const uint32_t    DEFAULT_CAPTURE_TIME  = 10; // In seconds.
/// maximum number of RAW captures to do, default is to save all the frames as long
/// as the preview is running
static const uint32_t    MAX_NUM_RAW_CAPTURES = 0xFFFFFFFF;
static const bool        DEFAULT_NVRAW_CAPTURE = false;

// Debug print macros.
#define APP_PRINT(...) printf("SYNC STEREO RAW INJ APP: " __VA_ARGS__)
#define PRODUCER_PRINT(...) printf("PRODUCER: " __VA_ARGS__)

#ifdef ANDROID
#define RAW_INPUT_STREAM_LEFT "/sdcard/DCIM/Argus_Raw_InputStream_Left_1920_1080_RGGB.raw"
#define RAW_INPUT_STREAM_RIGHT "/sdcard/DCIM/Argus_Raw_InputStream_Right_1920_1080_RGGB.raw"
#else
#define RAW_INPUT_STREAM_LEFT "Argus_Raw_Input_Left_1920_1080_RGGB.raw"
#define RAW_INPUT_STREAM_RIGHT "Argus_Raw_Input_Right_1920_1080_RGGB.raw"
#endif

struct ExecuteOptions
{
    //uint32_t reprocessCamIndex;
    uint32_t numCaptures;
    uint32_t sensorModeIndex;
    uint32_t hawkModuleCount;
    bool isNvRAWCapture;
};
ModuleInfo moduleInfo[MAX_CAM_DEVICE];
uint32_t reprocessHawkModuleCount = 0;
uint32_t reprocessSensorCount = 0;
int monoCameraIdx = -1;
std::vector<CameraDevice*> cameraDevices;
ICameraProvider *iCameraProvider;

static bool executeForHawkModule(const ArgusSamples::ExecuteOptions& options)
{
    uint32_t i = 0;

    UniqueObj<Request> request;
    std::vector <CameraDevice*> lrCameras;
    const Argus::BayerPhase phase = Argus::BAYER_PHASE_RGGB;
    // Group camera devices to identify no. of sessions to be created
    for (int a = 0; a < moduleInfo[i].sensorCount; a++)
    {
        lrCameras.push_back(cameraDevices[moduleInfo[i].camDevice[a]]);
        APP_PRINT("Session[%d] : add cameraDevices[%d]\n", i, moduleInfo[i].camDevice[a]);
        moduleInfo[i].iCameraProperties[a] =
            interface_cast<ICameraProperties>(cameraDevices[moduleInfo[i].camDevice[a]]);
        if (!moduleInfo[i].iCameraProperties[a])
            ORIGINATE_ERROR("Failed to get cameraProperties interface");

        // Get the sensor mode to determine the video output stream resolution.
        std::vector<SensorMode*> sensorModes;
        moduleInfo[i].iCameraProperties[a]->getAllSensorModes(&sensorModes);
        if (sensorModes.size() == 0)
            ORIGINATE_ERROR("Failed to get sensor modes");
        if (sensorModes.size() <= options.sensorModeIndex)
            ORIGINATE_ERROR("Given sensor mode doesn't exist");
        moduleInfo[i].sensorMode[a] = sensorModes[options.sensorModeIndex];
        moduleInfo[i].iSensorMode[a] = interface_cast<ISensorMode>(moduleInfo[i].sensorMode[a]);
        if (!moduleInfo[i].iSensorMode[a])
            ORIGINATE_ERROR("Failed to get sensor mode interface");
        PRODUCER_PRINT("Using sensor mode %d (%dx%d)\n",
            options.sensorModeIndex,
            moduleInfo[i].iSensorMode[a]->getResolution().width(),
            moduleInfo[i].iSensorMode[a]->getResolution().height());
        
        // set reprocessing info
        Argus::IReprocessInfo* reprocessInfo =
                Argus::interface_cast<Argus::IReprocessInfo>(cameraDevices[moduleInfo[i].camDevice[a]]);
        printf("Reprocessing infterface is created \n");
        if (!reprocessInfo)
        {
             REPORT_ERROR("Failed to get reprocessInfo interface");
             return EXIT_FAILURE;
        }

        reprocessInfo->setReprocessingEnable(true);
        const Argus::Size2D<uint32_t>& resolution = 
            {moduleInfo[i].iSensorMode[a]->getResolution().width(),
             moduleInfo[i].iSensorMode[a]->getResolution().height()};
        reprocessInfo->setReprocessingModeResolution(resolution);
        reprocessInfo->setReprocessingModeColorFormat(phase);
        reprocessInfo->setReprocessingModePixelBitDepth(12);
        reprocessInfo->setReprocessingModeDynamicPixelBitDepth(12);
        APP_PRINT("Reprocessing is set\n");
    }
    /**
     * Create the capture session for each set of camera devices identified above,
     * Each session will comprise of two devices (for now) in case of HAWK module.
     * AutoControl will be based on what the 1st device sees.
     * In case of non-HAWK module, there will be a single session for single camera device.
     */
    moduleInfo[i].captureSession =
        UniqueObj<CaptureSession>(iCameraProvider->createCaptureSession(lrCameras));
    moduleInfo[i].iCaptureSession = interface_cast<ICaptureSession>(moduleInfo[i].captureSession);
    if (!moduleInfo[i].iCaptureSession)
        ORIGINATE_ERROR("Failed to get capture session interface");
    APP_PRINT("Session is created\n");

    /**
     * Create stream settings object and set settings common to both streams in case of HAWK module,
     * else single stream will be created for non-HAWK module.
     *
     */
    moduleInfo[i].streamSettings = UniqueObj<OutputStreamSettings>(
        moduleInfo[i].iCaptureSession->createOutputStreamSettings(STREAM_TYPE_EGL));
    IOutputStreamSettings* iStreamSettings =
        interface_cast<IOutputStreamSettings>(moduleInfo[i].streamSettings);
    IEGLOutputStreamSettings* iEGLStreamSettings =
        interface_cast<IEGLOutputStreamSettings>(moduleInfo[i].streamSettings);
    if (!iStreamSettings || !iEGLStreamSettings)
        ORIGINATE_ERROR("Failed to create OutputStreamSettings");

    if (iEGLStreamSettings->setResolution(moduleInfo[i].iSensorMode[0]->getResolution()) != STATUS_OK)
        ORIGINATE_ERROR("Failed to set resolution for raw stream");
    if (iEGLStreamSettings->setMetadataEnable(true) != STATUS_OK)
        ORIGINATE_ERROR("Failed to set metadata enable for raw stream");
    if (iEGLStreamSettings->setPixelFormat(PIXEL_FMT_YCbCr_420_888) != STATUS_OK)
        ORIGINATE_ERROR("Failed to set pixel format for yuv stream");

    // Create EGL streams based on stream settings created above for HAWK/non-HAWK modules.
    for (int a = 0; a < moduleInfo[i].sensorCount; a++)
    {
        PRODUCER_PRINT("Creating yuv stream[%d]\n", a);
        iStreamSettings->setCameraDevice(lrCameras[a]);
        moduleInfo[i].stream[a] = UniqueObj<OutputStream>(
            moduleInfo[i].iCaptureSession->createOutputStream(moduleInfo[i].streamSettings.get()));
        if (!moduleInfo[i].stream[a])
            ORIGINATE_ERROR("Failed to create output stream");
    }

    PRODUCER_PRINT("Launching syncsensor consumer for numCaptures %d\n", options.numCaptures);
    moduleInfo[i].stereoYuvConsumer =
        new StereoYuvConsumerThread(
            options.numCaptures,
            iEGLStreamSettings,
            lrCameras[0],
            &moduleInfo[i],
            nullptr, //for mono case only, not used for hawk case
            true);
    PROPAGATE_ERROR(moduleInfo[i].stereoYuvConsumer->initialize());
    PROPAGATE_ERROR(moduleInfo[i].stereoYuvConsumer->waitRunning());
    // Create a request
    request = UniqueObj<Request>(moduleInfo[i].iCaptureSession->createRequest(CAPTURE_INTENT_VIDEO_RECORD));
    IRequest *iRequest = interface_cast<IRequest>(request);
    if (!iRequest)
        ORIGINATE_ERROR("Failed to create Request");

    /*
     * Creates the input stream.
     * Enable reprocessing in iRequest.
     * Create and connect to input stream consumer.
     * Connect input stream producer.
     */

    /*
     * Creates the input stream for loading raw bayer input buffers for reprocessing between
     * raw bayer producer cleint and argus as raw bayer consumer.
     * Input Stream Settings which is same all sensors.
     */
    moduleInfo[i].inStreamSettings = UniqueObj<InputStreamSettings>(
        moduleInfo[i].iCaptureSession->createInputStreamSettings(STREAM_TYPE_EGL));

    Argus::IEGLInputStreamSettings *iEGLInputStreamSettings =
        Argus::interface_cast<Argus::IEGLInputStreamSettings>(moduleInfo[i].inStreamSettings);
    EXIT_IF_NULL(iEGLInputStreamSettings, "Cannot get IEGLInputStreamSettings Interface");
    iEGLInputStreamSettings->setPixelFormat(Argus::PIXEL_FMT_RAW16);
    iEGLInputStreamSettings->setResolution(moduleInfo[i].iSensorMode[0]->getResolution());
    APP_PRINT("inStreamSettings is set\n");

    /*
     * Input Stream creates for all sensors with same settings
     */
    Argus::Status status;
    for (int a = 0; a < moduleInfo[i].sensorCount; a++)
    {
        APP_PRINT("Creating input raw stream[%d]\n", a);
        moduleInfo[i].inStream[a] = UniqueObj<InputStream>(
            moduleInfo[i].iCaptureSession->createInputStream(moduleInfo[i].inStreamSettings.get()));
        if (!moduleInfo[i].inStream[a])
            ORIGINATE_ERROR("Failed to create input stream");

        status = iRequest->enableInputStream(moduleInfo[i].inStream[a].get(), moduleInfo[i].inStreamSettings.get());
        EXIT_IF_NOT_OK(status, "Failed to enable stream in capture request");
    }

    /*
     * Enable Input Stream
     */
    status = iRequest->setReprocessingEnable(true);
    EXIT_IF_NOT_OK(status, "Failed to set Reprocessing enable in request");
    /*
     * create and connect the input stream consumer
     */
    status = moduleInfo[i].iCaptureSession->connectAllRequestInputStreams(request.get(), 1);
    EXIT_IF_NOT_OK(status, "Failed to connect input stream");
    APP_PRINT("inStream consumer is connected \n");

    /*
     * Input Stream FrameProducer for both the hawk sensor pair
     */
     UniqueObj<EGLStream::FrameProducer> inProducer[2];
     EGLStream::IFrameProducer *iFrameProducer[2];
    for (int a = 0; a < moduleInfo[i].sensorCount; a++)
    {
        inProducer[a] = 
            UniqueObj<EGLStream::FrameProducer>(EGLStream::FrameProducer::create(moduleInfo[i].inStream[a].get(), phase));

        iFrameProducer[a] = interface_cast<EGLStream::IFrameProducer>(inProducer[a]);
        EXIT_IF_NULL(iFrameProducer[a], "Failed to initialize inProducer");
        APP_PRINT("inProducer[%d] is set\n", a);
    }
    const char *inputFileLeft = RAW_INPUT_STREAM_LEFT;
    const char *inputFileRight = RAW_INPUT_STREAM_RIGHT;

    for (uint32_t ii = 0;ii < options.numCaptures; ii++)
    {
        for (int a = 0; a < moduleInfo[i].sensorCount; a++)
        {
            EGLStream::FrameBuf *buffer = NULL;

            EXIT_IF_NOT_OK(iFrameProducer[a]->getFrame(&buffer), "Failed to getFrame from inProducer");
            APP_PRINT("inProducer[%d]->getBuffer %p\n", a, buffer);
            EXIT_IF_NULL(buffer, "Failed to get Buffer from input stream producer");

            //Read raw bayer image
            EGLStream::IFrameBuf *ibuffer = Argus::interface_cast<EGLStream::IFrameBuf>(buffer);
            EXIT_IF_NULL( ibuffer, "No more ibuffer. Cleaning up.\n");

            // This samples uses single raw image, if needed video stream can be used and accordingly
            // input file should be modified.
            if (a == 0) // left sensor raw stream
            {
                EXIT_IF_NOT_OK(ibuffer->loadInputImageFromFile(inputFileLeft),
                    "Loading raw file failed");
            }
            else // right sensor raw stream
            {
                EXIT_IF_NOT_OK(ibuffer->loadInputImageFromFile(inputFileRight),
                    "Loading raw file failed");
            }

            EXIT_IF_NOT_OK(iFrameProducer[a]->presentFrame(buffer), 
                "Preset Buffer failed for input stream");
            APP_PRINT("inProducer->presentBuffer %p\n", buffer);

            if (iRequest->enableOutputStream(moduleInfo[i].stream[a].get()) != STATUS_OK)
                ORIGINATE_ERROR("Failed to enable RAW stream in Request");
            APP_PRINT("Enable stream[%d].\n", a);
        }

        /**
         * Set up ac, sensor and isp settings to reprocess the raw buffer.
         * For this sample, default settings are used, if needed settings can be read 
         * from metadata of raw buffers.
        */
        ISourceSettings *iSourceSettings =
                interface_cast<ISourceSettings>(iRequest->getSourceSettings());
        APP_PRINT("setSensorMode: %d (%dx%d), bitDepth %d output bitDepth %d\n",
            options.sensorModeIndex,
            moduleInfo[i].iSensorMode[0]->getResolution().width(),
            moduleInfo[i].iSensorMode[0]->getResolution().height(), 
            moduleInfo[i].iSensorMode[0]->getInputBitDepth(),
            moduleInfo[i].iSensorMode[0]->getOutputBitDepth());
        if (iSourceSettings->setSensorMode(moduleInfo[i].sensorMode[0]) != STATUS_OK)
            ORIGINATE_ERROR("Failed to set sensor mode in source settings");

        // Submit capture requests
        APP_PRINT(" argus app: capture no. %d requested \n", i);
        if (moduleInfo[i].iCaptureSession->capture(request.get()) == 0)
            ORIGINATE_ERROR("Failed to submit capture request");
    }

    // Wait for specified number of seconds.
    sleep(1);

    // Stop the capture requests and wait until they are complete.
    moduleInfo[i].iCaptureSession->waitForIdle();
    // Destroy the output streams to end the consumer thread.
    PRODUCER_PRINT("Captures complete, disconnecting producers\n" );
    for (int a = 0; a < moduleInfo[i].sensorCount; a++)
    {
        moduleInfo[i].inStream[a].reset();
        moduleInfo[i].stream[a].reset();
    }

    // Wait for the consumer thread to complete.
    PRODUCER_PRINT("Wait for consumer thread to complete\n");
    PROPAGATE_ERROR(moduleInfo[i].stereoYuvConsumer->shutdown());

    return true;
}

static bool executeForMonoCamera(const ArgusSamples::ExecuteOptions& options,
    CameraDevice* cameraDevice)
{

    // get sensormode.
    Argus::SensorMode* sensorMode = ArgusSamples::ArgusHelpers::getSensorMode(
            cameraDevice, options.sensorModeIndex);
    Argus::ISensorMode *iSensorMode =
        Argus::interface_cast<Argus::ISensorMode>(sensorMode);
    if (!iSensorMode)
    {
        REPORT_ERROR("Failed to get sensor mode interface");
        return EXIT_FAILURE;
    }

    APP_PRINT("+++ Capturing from mono device using sensor mode %d (%dx%d +++)\n",
           options.sensorModeIndex,
           iSensorMode->getResolution().width(), iSensorMode->getResolution().height());

    // set reprocessing info
    Argus::IReprocessInfo* reprocessInfo =
            Argus::interface_cast<Argus::IReprocessInfo>(cameraDevice);
    if (!reprocessInfo)
    {
         REPORT_ERROR("Failed to get reprocessInfo interface");
         return EXIT_FAILURE;
    }

    reprocessInfo->setReprocessingEnable(true);
    const Argus::Size2D<uint32_t>& resolution =
        {iSensorMode->getResolution().width(), iSensorMode->getResolution().height()};
    const Argus::BayerPhase phase = Argus::BAYER_PHASE_RGGB;
    reprocessInfo->setReprocessingModeResolution(resolution);
    reprocessInfo->setReprocessingModeColorFormat(phase);
    reprocessInfo->setReprocessingModePixelBitDepth(12);
    reprocessInfo->setReprocessingModeDynamicPixelBitDepth(12);
    APP_PRINT("Reprocessing is set\n");

    // Create CaptureSession.
    ICameraProperties *iCameraProperties = interface_cast<ICameraProperties>(cameraDevice);
    if (!iCameraProperties)
        ORIGINATE_ERROR("Failed to get ICameraProperties interface");
    UniqueObj<CaptureSession> captureSession(iCameraProvider->createCaptureSession(cameraDevice));
    ICaptureSession *iSession = interface_cast<ICaptureSession>(captureSession);
    if (!iSession)
        ORIGINATE_ERROR("Failed to create CaptureSession");
    APP_PRINT("Session is created\n");


    // Set common output stream settings.
    UniqueObj<OutputStreamSettings> streamSettingsYuv(
            iSession->createOutputStreamSettings(STREAM_TYPE_EGL));
    IEGLOutputStreamSettings *iEGLStreamSettingsYuv =
            interface_cast<IEGLOutputStreamSettings>(streamSettingsYuv);
    if (!iEGLStreamSettingsYuv)
        ORIGINATE_ERROR("Failed to create OutputStreamSettings");
    iEGLStreamSettingsYuv->setPixelFormat(PIXEL_FMT_YCbCr_420_888);
    iEGLStreamSettingsYuv->setResolution(iSensorMode->getResolution());
    iEGLStreamSettingsYuv->setMetadataEnable(true);

    // Create YUV stream and yuv consumer
    UniqueObj<OutputStream> yuvStream(
        iSession->createOutputStream(streamSettingsYuv.get()));
    if (!yuvStream)
        ORIGINATE_ERROR("Failed to create YUV OutputStream");
    APP_PRINT("yuvStream is created\n");
    StereoYuvConsumerThread stereoYuvConsumerThread(
        options.numCaptures,
        iEGLStreamSettingsYuv,
        cameraDevice,
        nullptr,
        yuvStream.get(),
        false);
        
    PROPAGATE_ERROR(stereoYuvConsumerThread.initialize());
    PROPAGATE_ERROR(stereoYuvConsumerThread.waitRunning());

    // Create capture Request and enable the streams in the Request.
    UniqueObj<Request> request(iSession->createRequest(CAPTURE_INTENT_VIDEO_RECORD));
    IRequest *iRequest = interface_cast<IRequest>(request);

   // create input stream
    // enable reprocessing in iRequest
    // create and connect to input stream consumer
    // connect input stream producer

    /*
     * Creates the input stream for loading raw bayer input buffers for reprocessing between
     * raw bayer producer cleint and argus as raw bayer consumer.
     */
    /*
     * Input Stream Settings
     */
    Argus::UniqueObj<Argus::InputStreamSettings> inStreamSettings(
        iSession->createInputStreamSettings(Argus::STREAM_TYPE_EGL));

    Argus::IEGLInputStreamSettings *iEGLInputStreamSettings =
        Argus::interface_cast<Argus::IEGLInputStreamSettings>(inStreamSettings);
    EXIT_IF_NULL(iEGLInputStreamSettings, "Cannot get IEGLInputStreamSettings Interface");
    iEGLInputStreamSettings->setPixelFormat(Argus::PIXEL_FMT_RAW16);
    iEGLInputStreamSettings->setResolution(iSensorMode->getResolution());
    APP_PRINT("inStreamSettings is set\n");
    /*
     * Input Stream creates
     */
    Argus::Status status;
    Argus::UniqueObj<Argus::InputStream> inStream(
        iSession->createInputStream(inStreamSettings.get()));
    EXIT_IF_NULL(inStream, "Failed to create EGLInputStream");
    status = iRequest->enableInputStream(inStream.get(), inStreamSettings.get());
    EXIT_IF_NOT_OK(status, "Failed to enable stream in capture request");

    status = iRequest->setReprocessingEnable(true);
    EXIT_IF_NOT_OK(status, "Failed to set Reprocessing enable in request");

    // create and connect the input stream consumer
    status = iSession->connectAllRequestInputStreams(request.get(), 1);
    EXIT_IF_NOT_OK(status, "Failed to connect input stream");
    APP_PRINT("inStream consumer is connected\n");

    /*
     * Input Stream FrameProducer
     */
    Argus::UniqueObj<EGLStream::FrameProducer> inProducer(
        EGLStream::FrameProducer::create(inStream.get(), phase));

    EXIT_IF_NOT_OK(status, "Failed to enable stream in capture request");
    EGLStream::IFrameProducer *iFrameProducer =
        Argus::interface_cast<EGLStream::IFrameProducer>(inProducer);
    EXIT_IF_NULL(iFrameProducer, "Failed to initialize inProducer");
    APP_PRINT("inProducer is set\n");

    const char *inputFileName = RAW_INPUT_STREAM_LEFT;

    for (uint32_t i = 0;i < options.numCaptures; i++)
    {
        EGLStream::FrameBuf *buffer = NULL;
        EXIT_IF_NOT_OK(iFrameProducer->getFrame(&buffer), "Failed to getFrame from inProducer");
        APP_PRINT("inProducer->getBuffer %p\n", buffer);

        // Add the buffer to the pending list.
        EXIT_IF_NULL(buffer, "Failed to get Buffer from input stream producer");

        //load raw bayer image
        EGLStream::IFrameBuf *ibuffer = Argus::interface_cast<EGLStream::IFrameBuf>(buffer);
        EXIT_IF_NULL( ibuffer, "No more ibuffer. Cleaning up.\n");
        EXIT_IF_NOT_OK(ibuffer->loadInputImageFromFile(inputFileName),
            "Loading raw file failed");

        EXIT_IF_NOT_OK(iFrameProducer->presentFrame(buffer), "Preset Buffer failed for input stream");
        APP_PRINT("inProducer->presentBuffer %p\n", buffer);

        // Enbale output stream and set sensor mode
        if (iRequest->enableOutputStream(yuvStream.get()) != STATUS_OK)
            ORIGINATE_ERROR("Failed to enable YUV stream in Request");

        Argus::ISourceSettings *iSourceSettings =
            Argus::interface_cast<Argus::ISourceSettings>(request);
        EXIT_IF_NULL(iSourceSettings, "Failed to get source settings request interface");
        iSourceSettings->setSensorMode(sensorMode);

        /**
         * Set up ac, sensor and isp settings to reprocess the raw buffer.
         * For this sample, settings are hardcoded but when this sample is integrated with
         * GXF framework then these settings should be read from metadata of raw buffers.
        */

        // Submit capture requests for requested number of seconds.
        APP_PRINT(" argus app: capture no. %d requested \n", i);
        if (iSession->capture(request.get()) == 0)
            ORIGINATE_ERROR("Failed to submit capture request");
    }
    
    sleep(1);
    iSession->waitForIdle();
    inStream.reset();
    yuvStream.reset();
    PROPAGATE_ERROR(stereoYuvConsumerThread.shutdown());
    APP_PRINT(" argus app: stereoYuvConsumerThread stereoYuvConsumerThread done \n");
    return true;
}

static bool execute(const ArgusSamples::ExecuteOptions& options)
{
    memset(&moduleInfo, 0, MAX_CAM_DEVICE*sizeof(ModuleInfo));
    for (int i = 0; i < MAX_CAM_DEVICE; i++)
        moduleInfo[i].initialized = false;
    // Initialize the Argus camera provider.fromScfSensorPlacement
    UniqueObj<CameraProvider> cameraProvider(CameraProvider::create());
    // Get the ICameraProvider interface from the global CameraProvider.
    iCameraProvider = interface_cast<ICameraProvider>(cameraProvider);
    if (!iCameraProvider)
        ORIGINATE_ERROR("Failed to get ICameraProvider interface");
    APP_PRINT("Argus Version: %s\n", iCameraProvider->getVersion().c_str());
    // Get the camera devices.
    if (iCameraProvider->getCameraDevices(&cameraDevices) != STATUS_OK)
        ORIGINATE_ERROR("Failed to get CameraDevices");
    if (cameraDevices.size() == 0)
        ORIGINATE_ERROR("No CameraDevices available");
    /**
     * For multiple HAWK modules, we need to map the available sensors
     * to identify which sensor belongs to which HAWK module.
     * In case we have non-HAWK modules, the sensors would be mapped accordingly.
     * Current assumption is that each HAWK module has only 2 sensors and
     * each non-HAWK module has only a single sensor.
     *
     */
    //char syncSensorId[MAX_MODULE_STRING];
    for (uint32_t i = 0; i < cameraDevices.size(); i++)
    {
        ICameraProperties *iCameraProperties =
            interface_cast<ICameraProperties>(cameraDevices[i]);
        if (!iCameraProperties)
            ORIGINATE_ERROR("Failed to get cameraProperties interface");
        APP_PRINT("getSensorPlacement for sensor i %d is %s\n",
                i, iCameraProperties->getSensorPlacement().getName());

        reprocessSensorCount++;
        APP_PRINT("rawReproceCamera for cameraDevices number %d\n", i);
        for (uint32_t j = 0; j <= reprocessHawkModuleCount; j++)
        {
            // Run through every camera device to populate the hawkModule information.
            // if ony one camera device is supported then mono usecase will be executed.
            if (i == 0)
            {
                if (moduleInfo[j].initialized == false)
                {
                    moduleInfo[j].initialized = true;
                    moduleInfo[j].camDevice[moduleInfo[j].sensorCount++] = i;
                    APP_PRINT("rawReproceCamera: Master for camDevice index %d \n", i);
                }
                else
                {
                    continue;
                }
                reprocessHawkModuleCount++;
                break;
            }
            else if (i == 1)
            {
                moduleInfo[j].camDevice[moduleInfo[j].sensorCount++] = i;
                APP_PRINT("rawReproceCamera: slave for camDevice index %d \n", i);
                break;
            }
        }
    }

    printf("Orginal reprocessHawkModuleCount %d with camera index %d\n", 
        reprocessHawkModuleCount, moduleInfo[0].camDevice[1]);


    for (uint32_t i = 0; i < reprocessHawkModuleCount; i++)
    {
        printf("/**************************/\n");
        printf("Identified %s module with %d sensors connected\n", moduleInfo[i].moduleName
                                                                 , moduleInfo[i].sensorCount);
        printf("/**************************/\n");
    }

    if ((options.hawkModuleCount > 0) && (options.hawkModuleCount <= reprocessHawkModuleCount))
    {
        printf("executeForHawkModule for camera Index %d\n", moduleInfo[0].camDevice[0]);
        return executeForHawkModule(options);
    }
    else if (reprocessSensorCount > 0)
    {
        printf("executeForMonoCamera for camera Index %d\n", moduleInfo[0].camDevice[0]);
        return executeForMonoCamera(options, cameraDevices[moduleInfo[0].camDevice[0]]);
    }
    else
    {
        ORIGINATE_ERROR("Failed to get any reprocess camera dsevice");
    }
    // Shut down Argus.
    cameraProvider.reset();
    APP_PRINT("Done -- exiting.\n");
    return true;
}
}; // namespace ArgusSamples

int main(int argc, char *argv[])
{
    APP_PRINT("Executing Argus Sample: %s\n", basename(argv[0]));
    //ArgusSamples::Value<uint32_t> reprocessCamIndex(0);
    ArgusSamples::Value<uint32_t> numCaptures(1);
    ArgusSamples::Value<uint32_t> sensorModeIndex(0);
    ArgusSamples::Value<uint32_t> hawkModuleCount(0);

    ArgusSamples::Options options(basename(argv[0]));

    //options.addOption(ArgusSamples::createValueOption
        //("reprocessCamIndex", 'd', "DEVICE INDEX", "Camera Index to use for reprocess.", reprocessCamIndex));
    options.addOption(ArgusSamples::createValueOption
        ("num", 'n', "NUMBER", "number of frames to reprocess save.", numCaptures));
    options.addOption(ArgusSamples::createValueOption
        ("sensormode", 'm', "[0 to n]", "Sensor mode to use.", sensorModeIndex));
    options.addOption(ArgusSamples::createValueOption
        ("hawkModuleCount", 's', "[0 to 2]", "hawkModuleCount to use.", hawkModuleCount));

    if (!options.parse(argc, argv))
    {
        return EXIT_FAILURE;
    }
    if (options.requestedExit())
    {
        return EXIT_SUCCESS;
    }
    ArgusSamples::ExecuteOptions executeOptions;
    executeOptions.numCaptures = numCaptures.get();
    executeOptions.sensorModeIndex = sensorModeIndex.get();
    executeOptions.hawkModuleCount = hawkModuleCount.get();
    if (!ArgusSamples::execute(executeOptions))
    {
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
