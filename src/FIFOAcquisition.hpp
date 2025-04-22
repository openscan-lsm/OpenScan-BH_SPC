#pragma once

#include "AcquisitionCompletion.hpp"

#include <FLIMEvents/BHDeviceEvent.hpp>
#include <FLIMEvents/StreamBuffer.hpp>
#include <OpenScanDeviceLib.h>

#include <cstdint>
#include <future>
#include <memory>
#include <tuple>

OScDev_RichError *ConfigureDeviceForFIFOAcquisition(short module);
OScDev_RichError *SetMarkerPolarities(short module, uint16_t enabledBits,
                                      uint16_t polarityBits);
OScDev_RichError *SetUpAcquisition(short module, bool checkSync,
                                   char fileHeader[4], short *fifoType,
                                   int *macroTimeClockTenthNs);
bool IsStandardFIFO(short fifoType);
bool IsSPC600FIFO32(short fifoType);
bool IsSPC600FIFO48(short fifoType);

std::tuple<OScDev_RichError *, std::future<void>> StartAcquisitionStandardFIFO(
    short module, std::shared_ptr<EventBufferPool<BHSPCEvent>> pool,
    std::shared_ptr<EventStream<BHSPCEvent>> stream,
    std::shared_future<void> stopRequested,
    std::shared_ptr<AcquisitionCompletion> completion);
