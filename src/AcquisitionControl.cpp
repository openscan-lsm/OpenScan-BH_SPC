#include "BH_SPC150Private.h"

#include "AcquisitionCompletion.hpp"
#include "DataSender.hpp"
#include "DataStream.hpp"
#include "FIFOAcquisition.hpp"
#include "MetadataJson.hpp"
#include "SPCFileWriter.hpp"
#include "UniqueFileName.h"

#include <bitset>
#include <chrono>
#include <cmath>
#include <fstream>
#include <future>
#include <string>
#include <vector>

// C++ state that we store in our device private data. Members are kept
// minimal; data that can be passed as lambda captures/parameters is passed
// that way.
struct AcqState {
    // Indicates finish of all activities related to an acquisition; once this
    // future completes, this struct may be deallocated at any time (but only
    // within an externally synchronized context). Value = any error messages.
    std::shared_future<std::vector<std::string>> finish;

    // Setting a value on this promise stopps the acquisition. Because there
    // are two separate places where this may be set (user stop request and
    // stop requested by data processing), code calling set_value() must guard
    // for the possible future_exception with a code of
    // promise_already_satisfied.
    std::promise<void> requestStop;

    // Futures from std::async that we need to hold.
    std::future<void> eventPumpingFinish;
    std::future<void> acquisitionFinish;
    std::future<void> logStopFinish;
};

// All stopping of acquisition must be through this function
static void RequestAcquisitionStop(AcqState *acqState) {
    try {
        acqState->requestStop.set_value();
    } catch (std::future_error const &e) {
        if (e.code() == std::future_errc::promise_already_satisfied) {
            // Ok, somebody else already requested stop
        } else {
            throw;
        }
    }
}

static int ResetAcquisitionState(OScDev_Device *device) {
    if (GetData(device)->acqState) {
        using namespace std::chrono_literals;
        if (GetData(device)->acqState->finish.wait_for(0s) !=
            std::future_status::ready) {
            return 1; // Acquisition already in progress
        }
        delete GetData(device)->acqState;
        GetData(device)->acqState = nullptr;
    }
    GetData(device)->acqState = new AcqState();
    return 0;
}

extern "C" int InitializeDeviceForAcquisition(OScDev_Device *device) {
    return ConfigureDeviceForFIFOAcquisition(GetData(device)->moduleNr);
}

// To be called before shutting down device
extern "C" void ShutdownAcquisitionState(OScDev_Device *device) {
    if (GetData(device)->acqState == nullptr) {
        return;
    }

    RequestAcquisitionStop(GetData(device)->acqState);
    GetData(device)->acqState->finish.get();

    delete GetData(device)->acqState;
    GetData(device)->acqState = nullptr;
}

static int32_t PixelsToMacroTime(double pixels, double pixelRateHz,
                                 uint32_t unitsTenthNs) {
    return static_cast<int32_t>(
        std::round(1e10 * pixels / pixelRateHz / unitsTenthNs));
}

static int ConfigureMarkers(OScDev_Device *device) {
    auto data = GetData(device);
    uint16_t enabled = 0;
    uint16_t risingEdgeActive = 0;
    for (int i = 0; i < NUM_MARKER_BITS; ++i) {
        enabled |= (data->markerActiveEdges[i] != MarkerPolarityDisabled) << i;
        risingEdgeActive |=
            (data->markerActiveEdges[i] == MarkerPolarityRisingEdge) << i;
    }
    return SetMarkerPolarities(data->moduleNr, enabled, risingEdgeActive);
}

static int CheckMarkers(OScDev_Device *device) {
    auto data = GetData(device);

    // Pixel, line, and frame markers must all differ if enabled
    std::bitset<NUM_MARKER_BITS> usedMarkers;
    if (data->pixelMarkerBit < NUM_MARKER_BITS) {
        usedMarkers.set(data->pixelMarkerBit);
    }
    if (data->lineMarkerBit < NUM_MARKER_BITS) {
        if (usedMarkers.test(data->lineMarkerBit)) {
            return 1; // Duplicate marker assignment
        }
        usedMarkers.set(data->lineMarkerBit);
    }
    if (data->frameMarkerBit < NUM_MARKER_BITS) {
        if (usedMarkers.test(data->frameMarkerBit)) {
            return 1; // Duplicate marker assignment
        }
    }

    // Line marker must be assigned and enabled (until we support pixel marker)
    if (data->lineMarkerBit >= NUM_MARKER_BITS) {
        return 1; // Line marker required
    }
    if (data->markerActiveEdges[data->lineMarkerBit] ==
        MarkerPolarityDisabled) {
        return 1; // Line marker required
    }

    return 0;
}

static void WaitForCompletionAndLog(OScDev_Device *device, AcqState *acqState,
                                    std::string const &proc) {
    auto messages = acqState->finish.get();
    if (messages.empty()) {
        OScDev_Log_Info(device, (proc + " finished successfully").c_str());
    } else {
        OScDev_Log_Error(device, (proc + " failed with error(s):").c_str());
        for (auto const &m : messages) {
            OScDev_Log_Error(device, m.c_str());
        }
    }
}

extern "C" int StartAcquisition(OScDev_Device *device,
                                OScDev_Acquisition *acq) {
    OScDev_Log_Info(device, "Starting acquisition setup");

    int err = ResetAcquisitionState(device);
    if (err != 0)
        return err;
    auto acqState = GetData(device)->acqState;
    std::shared_future<void> stopRequested =
        acqState->requestStop.get_future().share();

    // Temporarily set 'finish' to a completed future in case we fail during
    // setup.
    std::promise<std::vector<std::string>> unstarted;
    acqState->finish = unstarted.get_future().share();
    unstarted.set_value({});

    err = ConfigureMarkers(device);
    if (err != 0)
        return err;

    err = CheckMarkers(device);
    if (err != 0)
        return err;
    uint32_t lineMarkerBit = GetData(device)->lineMarkerBit;

    uint32_t nFrames = OScDev_Acquisition_GetNumberOfFrames(acq);
    double pixelRateHz = OScDev_Acquisition_GetPixelRate(acq);
    uint32_t xOffset, yOffset, width, height;
    OScDev_Acquisition_GetROI(acq, &xOffset, &yOffset, &width, &height);

    std::bitset<MAX_NUM_CHANNELS> channelMask = GetData(device)->channelMask;
    if (channelMask.count() < 1) {
        return 1; // No channel enabled
    }

    bool accumulateIntensity = GetData(device)->accumulateIntensity;

    bool lineMarkersAtLineEnds;
    switch (GetData(device)->pixelMappingMode) {
    case PixelMappingModeLineStartMarkers:
        lineMarkersAtLineEnds = false;
        break;
    case PixelMappingModeLineEndMarkers:
        lineMarkersAtLineEnds = true;
        break;
    default:
        return 1; // Unimplemented mode
    }
    double lineDelayPixels = GetData(device)->lineDelayPx;
    std::string fileNamePrefix(GetData(device)->fileNamePrefix);
    bool compressHistograms = GetData(device)->compressHistograms;
    uint16_t senderPort = GetData(device)->senderPort;
    bool checkSync = GetData(device)->checkSyncBeforeAcq;

    char fileHeader[4];
    short fifoType;
    int macroTimeUnitsTenthNs;
    err = SetUpAcquisition(GetData(device)->moduleNr, checkSync, fileHeader,
                           &fifoType, &macroTimeUnitsTenthNs);
    if (err != 0)
        return err;
    if (!IsStandardFIFO(fifoType)) {
        return 1; // Unsupported data format
    }

    uint32_t lineTime =
        PixelsToMacroTime(width, pixelRateHz, macroTimeUnitsTenthNs);
    int32_t lineDelay =
        PixelsToMacroTime(lineDelayPixels, pixelRateHz, macroTimeUnitsTenthNs);
    if (lineMarkersAtLineEnds) {
        lineDelay -= lineTime;
    }

    auto completion = std::make_shared<AcquisitionCompletion>(
        [acqState]() mutable { RequestAcquisitionStop(acqState); },
        [device](std::string const &m) {
            OScDev_Log_Debug(device, m.c_str());
        });
    acqState->finish = completion->GetCompletion();

    // Treat the following setup as a separate process, preventing the
    // completion from firing during setup.
    completion->AddProcess("Setup");

    std::shared_ptr<SPCFileWriter> spcWriter;
    std::shared_ptr<SDTWriter> sdtWriter;
    std::shared_ptr<DataSender> dataSender;

    if (!fileNamePrefix.empty()) {
        const char *const extensions[] = {".spc", ".sdt", ".json"};
        char temp[512];
        if (UniqueFileName(fileNamePrefix.c_str(), extensions, 3, temp,
                           sizeof(temp))) {
            std::string uniquePrefix = temp;

            spcWriter = std::make_shared<SPCFileWriter>(
                uniquePrefix + ".spc", fileHeader, completion);

            sdtWriter = std::make_shared<SDTWriter>(
                uniquePrefix + ".sdt",
                static_cast<unsigned>(channelMask.count()), completion);
            sdtWriter->SetPreacquisitionData(
                GetData(device)->moduleNr, 8, width, height,
                compressHistograms, pixelRateHz, false,
                GetData(device)->pixelMarkerBit < NUM_MARKER_BITS,
                GetData(device)->lineMarkerBit < NUM_MARKER_BITS,
                GetData(device)->frameMarkerBit < NUM_MARKER_BITS);

            MetadataJsonWriter jsonWriter(uniquePrefix + ".json");
            jsonWriter.SetChannelMask(channelMask);
            jsonWriter.SetImageSize(width, height);
            jsonWriter.SetPixelRateHz(pixelRateHz);
            jsonWriter.SetMacrotimeUnitsTenthNs(macroTimeUnitsTenthNs);
            jsonWriter.SetLineDelayAndTime(lineDelay, lineTime);
            jsonWriter.SetMarkerSettings(false, NUM_MARKER_BITS,
                                         GetData(device)->pixelMarkerBit,
                                         GetData(device)->lineMarkerBit,
                                         GetData(device)->frameMarkerBit);
            jsonWriter.Save();
        }
    }

    if (senderPort) {
        dataSender = std::make_shared<DataSender>(
            static_cast<unsigned>(channelMask.count()), senderPort,
            completion);
    }

    std::shared_ptr<EventStream<BHSPCEvent>> stream;
    try {
        completion->AddProcess("ProcessingSetup");
        auto stream_and_done = SetUpProcessing(
            width, height, nFrames, channelMask, accumulateIntensity,
            lineDelay, lineTime, lineMarkerBit, acq,
            [acqState]() mutable { RequestAcquisitionStop(acqState); },
            spcWriter, sdtWriter, dataSender, completion);
        stream = std::get<0>(stream_and_done);
        acqState->eventPumpingFinish = std::move(std::get<1>(stream_and_done));
        completion->HandleFinish("ProcessingSetup");
    } catch (
        std::bad_alloc const &) { // Likely could not allocate histogram memory
        completion->HandleError("Cannot allocate memory for histogram(s)",
                                "ProcessingSetup");
    }

    completion->HandleFinish("Setup");
    using namespace std::chrono_literals;
    if (stopRequested.wait_for(0s) == std::future_status::ready) {
        // A synchronous error occurred during setup
        stream->Send({});
        OScDev_Log_Error(
            device, "Failed during acquisition setup; waiting for cleanup");
        WaitForCompletionAndLog(device, acqState, "Acquisition setup");
        return 1;
    }

    // 48k events = ~5 ms at 10M events/s
    auto pool = std::make_shared<EventBufferPool<BHSPCEvent>>(48 * 1024);

    auto err_and_finish = StartAcquisitionStandardFIFO(
        GetData(device)->moduleNr, pool, stream, stopRequested, completion);
    err = std::get<0>(err_and_finish);
    acqState->acquisitionFinish = std::move(std::get<1>(err_and_finish));
    if (err != 0) {
        // A synchronous error occurred while starting acquisition
        OScDev_Log_Error(device,
                         "Failed to start acquisition; waiting for cleanup");
        WaitForCompletionAndLog(device, acqState, "Starting acquisition");
        return err;
    }

    OScDev_Log_Info(device, "Started acquisition");

    // TODO: Arrange to actually set post-acquisition data to SDTWriter
    if (sdtWriter) {
        sdtWriter->FinishPostAcquisitionData();
    }

    // Arrange to log the end of acquisition.
    acqState->logStopFinish =
        std::async(std::launch::async, [device, acqState] {
            OScDev_Log_Info(device, "Waiting for acquisition to finish");
            WaitForCompletionAndLog(device, acqState, "Acquisition");
        });

    return 0;
}

extern "C" void StopAcquisition(OScDev_Device *device) {
    if (GetData(device)->acqState == nullptr)
        return;

    RequestAcquisitionStop(GetData(device)->acqState);
}

extern "C" bool IsAcquisitionRunning(OScDev_Device *device) {
    if (GetData(device)->acqState == nullptr)
        return false;

    using namespace std::chrono_literals;
    return GetData(device)->acqState->finish.wait_for(0s) !=
           std::future_status::ready;
}

extern "C" void WaitForAcquisitionToFinish(OScDev_Device *device) {
    if (GetData(device)->acqState == nullptr)
        return;

    GetData(device)->acqState->finish.get();
}
