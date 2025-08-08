/*
 * Copyright (c) 2023, NVIDIA CORPORATION. All rights reserved.
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
 * <b>Libargus Extension: Alternating Exposure API</b>
 *
 * @b Description: This file defines the Alternating Exposure extension.
 */

#ifndef _ARGUS_ALTERNATING_EXPOSURE_H
#define _ARGUS_ALTERNATING_EXPOSURE_H

namespace Argus
{

/**
 * Adds interfaces for hardware based alternating exposure fuctionality.
 *
 * It introduces two new interfaces:
 *   - Ext::IAlternatingExposureCaps: Determines whether a device provides alternating
 *     								  exposure support
 *   - Ext::IAlternatingExposureSettings: configure alternating exposure settings.
 *
 * @defgroup ArgusExtAlternatingExposure Ext::AlternatingExposure
 * @ingroup ArgusExtensions
 */
DEFINE_UUID(ExtensionName, EXT_ALTERNATING_EXPOSURE, 5d887620,d087,11ed,b16f,08,00,20,0c,9a,66);

namespace Ext
{

/**
 * @class IAlternatingExposureCaps
 *
 * Interface used to query the availability of hardware based alternating exposure support.
 *
 * @ingroup ArgusCameraDevice ArgusExtAlternatingExposure
 */
DEFINE_UUID(InterfaceID, IID_ALTERNATING_EXPOSURE_CAPS, 5d887621,d087,11ed,b16f,08,00,20,0c,9a,66);
class IAlternatingExposureCaps : public Interface
{
public:
    static const InterfaceID& id() { return IID_ALTERNATING_EXPOSURE_CAPS; }

    /**
     * Returns whether alternating exposure support is available or not.
     */
    virtual bool supportsAlternatingExposure() const = 0;

protected:
    ~IAlternatingExposureCaps() {}
};

/**
 * @class IAlternatingExposureSettings
 *
 * Interface used to configure alternating exposure support provided by sensor hardware.
 * If alternating exposure is enabled, then two sets of exposure time and analog gain
 * values are used by the sensor in an alternating manner. One of the sets of exposure
 * time and analog gain values are applied by the sensor for every even frame and the
 * other set of values are applied for every odd frame.
 *
 * @ingroup ArgusCameraDevice ArgusExtAlternatingExposure
 */
DEFINE_UUID(InterfaceID, IID_ALTERNATING_EXPOSURE_SETTINGS, 5d887622,d087,11ed,b16f,08,00,20,0c,9a,66);
class IAlternatingExposureSettings : public Interface
{
public:
    static const InterfaceID& id() { return IID_ALTERNATING_EXPOSURE_SETTINGS; }

    /**
     * Used to enable or disable alternating exposure mode.
     * @param[in] enable whether to enable alternating exposure mode.
     */
    virtual void setAlternatingExposureEnable(bool enable) = 0;

    /**
     * Returns if alternating exposure is enabled or not.
     */
	virtual bool getAlternatingExposureEnable() const = 0;

    /**
     * Sets the two exposure time values (in nanoseconds) to be used for capturing frames
     * using alternating exposure. The first two elements of the vector @a exposure_times
     * are used for fetching the exposure time values.
     * @param [in] exposure_times Vector of exposure times.
     */
    virtual Status setAltExpExposureTimes(const std::vector<uint64_t>& exposure_times) = 0;

    /**
     * Sets the two analog gain values that will be alternately applied by the sensor
     * on frames captured by it. The first two elements of the vector @a analog_gains
     * are used for fetching the sensor analog gain values.
     * @param [in] analog_gains Vector of sensor analog gain values.
     */
	virtual Status setAltExpAnalogGains(const std::vector<float>& analog_gains) = 0;

protected:
    ~IAlternatingExposureSettings() {}
};

} // namespace Ext

} // namespace Argus

#endif // _ARGUS_ALTERNATING_EXPOSURE_H