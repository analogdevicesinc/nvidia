/*
 * Copyright (c) 2022, NVIDIA CORPORATION.  All rights reserved.
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

/**
 * @file
 * <b>Libargus Extension: Reprocessing Information API</b>
 *
 * @b Description: This file defines the ReprocessInfo.
 */

#ifndef _ARGUS_REPROCESS_INFO_H
#define _ARGUS_REPROCESS_INFO_H

namespace Argus
{

/**
 * @class IReprocessInfo
 *
 * Interface used to access reprocess information.
 * ReprocessInfo are used to set and access the reprocessing information to run
 * camera using a user provided raw buffer instead of physical camera sensor.
 *
 * @ingroup ArgusCameraDevice ArgusExtReprocessInfo
 */
DEFINE_UUID(InterfaceID, IID_REPROCESS_INFO, dbf2b0b0,7f71,11ec,bf44,08,00,20,0c,9a,66);
class IReprocessInfo : public Interface
{
public:
    static const InterfaceID& id() { return IID_REPROCESS_INFO; }

    /**
     * Enable the libargus to run in resprocessing mode.
     * @param[in] enable The enable flag to set the reprocesisng mode.
     *
     * @returns success/status of the call.
     */
    virtual Status setReprocessingEnable(const bool enable) = 0;
    virtual bool getReprocessingEnable() const = 0;

    /**
     * Sets the resolution of reprocessing sensor mode.
     * @param[in] resolution The resolution of reprocessing sensor mode.
     *
     * @returns success/status of the call.
     */
    virtual Status setReprocessingModeResolution(const Size2D<uint32_t>& resolution) = 0;
    virtual Size2D<uint32_t> getReprocessingModeResolution() const = 0;

    /**
     * Sets the scaling for reprocessing sensor mode.
     * @param[in] scaling The scaling in x and y direction.
     *
     * @returns success/status of the call.
     */
    virtual Status setReprocessingModeScaling(const Point2D<float>& scaling) = 0;
    virtual Point2D<float> getReprocessingModeScaling() const = 0;

    /**
     * Sets the crop rectange for the reprocessing sensor mode.
     * @param[in] crop The crop rectange for the sensor mode, describing top, left, bottom, down
     *                 coordinates, origin is top left corner of the image.
     *
     * @returns success/status of the call.
     */
    virtual Status setReprocessingModeCrop(const Rectangle<float>& crop) = 0;
    virtual Rectangle<float> getReprocessingModeCrop() const = 0;

    /**
     * Sets the frame rate for the reprocessing sensor mode.
     * @param[in] framerate The framerate value that will be utilized to decide the output stream
     *                      framerate.
     *
     * @returns success/status of the call.
     */
    virtual Status setReprocessingModeFrameRate(const float framerate) = 0;
    virtual float getReprocessingModeFrameRate() const = 0;

    /**
     * Sets the bayer phase for the reprocessing sensor mode.
     * @param[in] phase The phase of the raw input data (see Argus::BayerPhase).
     *
     * @returns success/status of the call.
     */
    virtual Status setReprocessingModeColorFormat(const BayerPhase& phase) = 0;
    virtual BayerPhase getReprocessingModeColorFormat() const = 0;

    /**
     * Sets the pixel bit depth for the reprocessing sensor mode. This is the bit depth of raw byaer
     * data. For PWL HDR raw data it is the pixel bit depth of PWL companded raw data. For DOL raw
     * data, it the pixel bit depth of individual exposure plane.
     * @param[in] pixelBitDepth The pixelBitDepth is the number of bits used to represent a pixel.
     *
     * @returns success/status of the call.
     */
    virtual Status setReprocessingModePixelBitDepth(const uint32_t pixelBitDepth) = 0;
    virtual uint32_t getReprocessingModePixelBitDepth() const = 0;

    /**
     * Sets the total pixel total bit depth for the reprocessing sensor mode after
     * decompanding or merging of individual exposure planes incase of PWL HDR or DOL HDR raw data
     * respectively. In case of standard dynamic range raw data it will be equal to pixelBitDepth.
     *
     * @param[in] dynamicPixelBitDepth The dynamicPixelBitDepth of raw sensor data.
     *
     * @returns success/status of the call.
     */
    virtual Status setReprocessingModeDynamicPixelBitDepth(const uint32_t dynamicPixelBitDepth) = 0;
    virtual uint32_t getReprocessingModeDynamicPixelBitDepth() const = 0;

protected:
  ~IReprocessInfo() {}

};

} // namespace Argus

#endif // _ARGUS_REPROCESS_INFO_H

