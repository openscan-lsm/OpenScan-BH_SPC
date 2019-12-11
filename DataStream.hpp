#pragma once

#include "AcquisitionCompletion.hpp"
#include "SPCFileWriter.hpp"
#include "SDTFileWriter.hpp"

#include <FLIMEvents/BHDeviceEvent.hpp>
#include <FLIMEvents/StreamBuffer.hpp>

#include <OpenScanDeviceLib.h>

#include <bitset>
#include <cstdint>
#include <functional>
#include <memory>
#include <tuple>


std::tuple<std::shared_ptr<EventStream<BHSPCEvent>>, std::future<void>>
SetUpProcessing(uint32_t width, uint32_t height, uint32_t maxFrames,
	std::bitset<16> channelMask,
	int32_t lineDelay, uint32_t lineTime, uint32_t lineMarkerBit,
	OScDev_Acquisition* acquisition, std::function<void(void)> stopFunc,
	std::shared_ptr<DeviceEventProcessor> additionalProcessor,
	std::shared_ptr<SDTWriter> histogramWriter,
	std::shared_ptr<AcquisitionCompletion> completion);