/*
 * Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved.
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
#include "CudaBayerDemosaicConsumer.h"

static const uint32_t            MAX_CAMERA_NUM = 6;

namespace ArgusSamples
{

// Globals and derived constants.
EGLDisplayHolder g_display;

/*******************************************************************************
 * Extended options class to add additional options specific to this sample.
 ******************************************************************************/
class SampleOptions : public CommonOptions
{
public:
    SampleOptions(const char *programName)
        : CommonOptions(programName,
                        ArgusSamples::CommonOptions::Option_D_CameraDevice |
                        ArgusSamples::CommonOptions::Option_M_SensorMode |
                        ArgusSamples::CommonOptions::Option_R_WindowRect |
                        ArgusSamples::CommonOptions::Option_F_FrameCount),
        m_numStreams(1),
        m_hCells(1),
        m_vCells(1)
    {
        addOption(createValueOption
            ("num", 'n', "COUNT", "Number of streams", m_numStreams));
        addOption(createValueOption
            ("x", 'x', "COUNT", "Number of horizontal cells", m_hCells));
        addOption(createValueOption
            ("y", 'y', "COUNT", "Number of vertical cells", m_vCells));
    }

    uint32_t numStreams() const { return m_numStreams.get(); }
    uint32_t hCells() const { return m_hCells.get(); }
    uint32_t vCells() const { return m_vCells.get(); }

protected:
    Value<uint32_t> m_numStreams;
    Value<uint32_t> m_hCells;
    Value<uint32_t> m_vCells;
};


/* An utility class to hold all resources of one capture session */
class CaptureHolder : public Destructable
{
public:
    explicit CaptureHolder();
    virtual ~CaptureHolder();

    bool initialize(const SampleOptions& options, ICameraProvider *iCameraProvider, CameraDevice *device);

    CaptureSession* getSession() const
    {
        return m_captureSession.get();
    }

    OutputStream* getStream() const
    {
        return m_outputStream.get();
    }

    Request* getRequest() const
    {
        return m_request.get();
    }

    Size2D<uint32_t> getResolution() const
    {
        return m_iSensorMode->getResolution();
    }

    virtual void destroy()
    {
        delete this;
    }

private:
    UniqueObj<CaptureSession> m_captureSession;
    UniqueObj<OutputStream> m_outputStream;
    UniqueObj<Request> m_request;
    ISensorMode *m_iSensorMode;
};

CaptureHolder::CaptureHolder()
{
}

CaptureHolder::~CaptureHolder()
{
    /* Destroy the output stream */
    m_outputStream.reset();
}

bool CaptureHolder::initialize(const SampleOptions& options, ICameraProvider *iCameraProvider, CameraDevice *device)
{
    /* Create the capture session using the first device and get the core interface */
    m_captureSession.reset(iCameraProvider->createCaptureSession(device));
    ICaptureSession *iCaptureSession = interface_cast<ICaptureSession>(m_captureSession);
    IEventProvider *iEventProvider = interface_cast<IEventProvider>(m_captureSession);
    if (!iCaptureSession || !iEventProvider)
        ORIGINATE_ERROR("Failed to create CaptureSession");

    /* Create the OutputStream */
    UniqueObj<OutputStreamSettings> streamSettings(
        iCaptureSession->createOutputStreamSettings(STREAM_TYPE_EGL));
    IEGLOutputStreamSettings *iEGLStreamSettings =
        interface_cast<IEGLOutputStreamSettings>(streamSettings);
    if (!iEGLStreamSettings)
        ORIGINATE_ERROR("Failed to create EglOutputStreamSettings");

    SensorMode* sensorMode = ArgusHelpers::getSensorMode(device, options.sensorModeIndex());
    m_iSensorMode = interface_cast<ISensorMode>(sensorMode);
    if (!m_iSensorMode)
        ORIGINATE_ERROR("Selected sensor mode not available");

    iEGLStreamSettings->setPixelFormat(PIXEL_FMT_RAW16);
    iEGLStreamSettings->setEGLDisplay(g_display.get());
    iEGLStreamSettings->setResolution(getResolution());
    iEGLStreamSettings->setMode(EGL_STREAM_MODE_FIFO);

    m_outputStream.reset(iCaptureSession->createOutputStream(streamSettings.get()));

    /* Create capture request and enable the output stream */
    m_request.reset(iCaptureSession->createRequest());
    IRequest *iRequest = interface_cast<IRequest>(m_request);
    if (!iRequest)
        ORIGINATE_ERROR("Failed to create Request");
    iRequest->enableOutputStream(m_outputStream.get());

    ISourceSettings *iSourceSettings =
            interface_cast<ISourceSettings>(iRequest->getSourceSettings());
    if (!iSourceSettings)
        ORIGINATE_ERROR("Failed to get ISourceSettings interface");
    iSourceSettings->setSensorMode(sensorMode);

    return true;
}

/**
 * Main thread function opens connection to Argus driver, creates a capture session for
 * a given camera device and sensor mode, then creates a RAW16 stream attached to a
 * CudaBayerConsumer such that the CUDA consumer will acquire the outputs of capture
 * results as raw Bayer data (which it then demosaics to RGBA for demonstration purposes).
 */
static bool execute(const SampleOptions& options)
{
    // Initialize the preview window and EGL display.
    Window &window = Window::getInstance();
    window.setWindowRect(options.windowRect());
    PROPAGATE_ERROR(g_display.initialize(window.getEGLNativeDisplay()));

    // Create the Argus CameraProvider object
    UniqueObj<CameraProvider> cameraProvider(CameraProvider::create());
    ICameraProvider *iCameraProvider = interface_cast<ICameraProvider>(cameraProvider);
    if (!iCameraProvider)
        ORIGINATE_ERROR("Failed to create CameraProvider");

    printf("Argus Version: %s\n", iCameraProvider->getVersion().c_str());

    /* Get the camera devices */
    std::vector<CameraDevice*> cameraDevices;
    iCameraProvider->getCameraDevices(&cameraDevices);
    printf("Camera devices: %lu\n", cameraDevices.size());

    if (cameraDevices.size() == 0)
        ORIGINATE_ERROR("No cameras available");

    uint32_t streamCount = cameraDevices.size() < MAX_CAMERA_NUM ?
            cameraDevices.size() : MAX_CAMERA_NUM;
    if (streamCount > options.numStreams())
        streamCount = options.numStreams();

    if (streamCount > options.hCells() * options.vCells())
        streamCount = options.hCells() * options.vCells();

    printf("Streams: %u\n", streamCount);

    UniqueObj<CaptureHolder> captureHolders[MAX_CAMERA_NUM];
    for (uint32_t i = 0; i < streamCount; i++)
    {
        captureHolders[i].reset(new CaptureHolder);
        if (!captureHolders[i].get()->initialize(options, iCameraProvider, cameraDevices[i]))
            ORIGINATE_ERROR("Failed to initialize Camera session %d", i);
    }

    std::vector<OutputStream*> streams;
    std::vector<Argus::Size2D<uint32_t>> sizes;
    for (uint32_t i = 0; i < streamCount; i++) {
        streams.push_back(captureHolders[i].get()->getStream());
        sizes.push_back(captureHolders[i].get()->getResolution());
    }

    // Create the CUDA Bayer consumer and connect it to the RAW16 output stream.
    CudaBayerDemosaicConsumer cudaConsumer(g_display.get(), streams, sizes,
                                           options.frameCount());
    PROPAGATE_ERROR(cudaConsumer.initialize());
    PROPAGATE_ERROR(cudaConsumer.waitRunning());

    // Submit the batch of capture requests.
    for (unsigned int frame = 0; frame < options.frameCount(); ++frame)
    {
        for (uint32_t j = 0; j < streamCount; j++)
        {
            ICaptureSession *iCaptureSession =
                    interface_cast<ICaptureSession>(captureHolders[j].get()->getSession());
            Request *request = captureHolders[j].get()->getRequest();
            uint32_t frameId = iCaptureSession->capture(request);
            if (frameId == 0)
                ORIGINATE_ERROR("Failed to submit capture request");
        }
    }

    // Wait until all captures have completed.
    for (uint32_t i = 0; i < streamCount; i++)
    {
        ICaptureSession *iCaptureSession =
            interface_cast<ICaptureSession>(captureHolders[i].get()->getSession());
        iCaptureSession->waitForIdle();
    }

    /* Destroy the capture resources */
    for (uint32_t i = 0; i < streamCount; i++)
    {
        captureHolders[i].reset();
    }

    // Shutdown the CUDA consumer.
    PROPAGATE_ERROR(cudaConsumer.shutdown());

    // Shut down Argus.
    cameraProvider.reset();

    // Shut down the window (destroys window's EGLSurface).
    window.shutdown();

    return true;
}

}; // namespace ArgusSamples

int main(int argc, char** argv)
{
    printf("Executing Argus Sample: %s\n", basename(argv[0]));

    ArgusSamples::SampleOptions options(basename(argv[0]));

    if (!options.parse(argc, argv))
        return EXIT_FAILURE;
    if (options.requestedExit())
        return EXIT_SUCCESS;

    if (!ArgusSamples::execute(options))
        return EXIT_FAILURE;

    printf("Argus sample '%s' completed successfully.\n", basename(argv[0]));

    return EXIT_SUCCESS;
}
