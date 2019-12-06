#pragma once

#include "AcquisitionCompletion.hpp"

#include <FLIMEvents/BHDeviceEvent.hpp>
#include <FLIMEvents/StreamBuffer.hpp>

#include <cstdint>
#include <future>
#include <memory>


int ConfigureDeviceForFIFOAcquisition(short module);
int SetMarkerPolarities(short module, uint16_t enabledBits, uint16_t polarityBits);
int SetUpAcquisition(short module, char fileHeader[4], short* fifoType, int* macroTimeClockTenthNs);
bool IsStandardFIFO(short fifoType);
bool IsSPC600FIFO32(short fifoType);
bool IsSPC600FIFO48(short fifoType);

std::future<void> StartAcquisitionStandardFIFO(short module,
	std::shared_ptr<EventBufferPool<BHSPCEvent>> pool,
	std::shared_ptr<EventStream<BHSPCEvent>> stream,
	std::shared_future<void> stopRequested,
	std::shared_ptr<AcquisitionCompletion> completion);