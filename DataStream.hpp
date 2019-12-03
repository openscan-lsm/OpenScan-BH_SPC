#pragma once

#include "SPCFileWriter.hpp"

#include <FLIMEvents/BHDeviceEvent.hpp>
#include <FLIMEvents/StreamBuffer.hpp>

#include <OpenScanDeviceLib.h>

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <ostream>
#include <tuple>


std::tuple<std::shared_ptr<EventStream<BHSPCEvent>>, std::shared_future<void>>
SetUpProcessing(uint32_t width, uint32_t height, uint32_t maxFrames,
	int32_t lineDelay, uint32_t lineTime, uint32_t lineMarkerBit,
	OScDev_Acquisition* acquisition, std::function<void()> stopFunc,
	std::shared_ptr<SPCFileWriter> spcFile);