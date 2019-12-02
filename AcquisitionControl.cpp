#include "BH_SPC150Private.h"

#include "DataStream.hpp"
#include "FIFOAcquisition.hpp"

#include <cmath>
#include <chrono>
#include <fstream>
#include <future>


// C++ state that we store in our device private data. Members are kept
// minimal; data that can be passed as lambda captures/parameters is passed
// that way.
struct AcqState {
	// Indicates finish of all activities related to an acquisition; once this
	// future completes, this struct may be deallocated at any time (but only
	// within an externally synchronized context).
	std::shared_future<void> finish;

	// Setting a value on this promise stopps the acquisition. Because there
	// are two separate places where this may be set (user stop request and
	// stop requested by data processing), code calling set_value() must guard
	// for the possible future_exception with a code of
	// promise_already_satisfied.
	std::promise<void> requestStop;
};


// All stopping of acquisition must be through this function
static void RequestAcquisitionStop(AcqState* acqState)
{
	try {
		acqState->requestStop.set_value();
	}
	catch (std::future_error const& e) {
		if (e.code() == std::future_errc::promise_already_satisfied) {
			// Ok, somebody else already requested stop
		}
		else {
			throw;
		}
	}
}


static int ResetAcquisitionState(OScDev_Device* device)
{
	if (GetData(device)->acqState) {
		using namespace std::chrono_literals;
		if (GetData(device)->acqState->finish.wait_for(0s) != std::future_status::ready) {
			return 1; // Acquisition already in progress
		}
		delete GetData(device)->acqState;
		GetData(device)->acqState = nullptr;
	}
	GetData(device)->acqState = new AcqState();
	return 0;
}


extern "C"
int InitializeDeviceForAcquisition(OScDev_Device* device)
{
	return ConfigureDeviceForFIFOAcquisition(GetData(device)->moduleNr);
}


// To be called before shutting down device
extern "C"
void ShutdownAcquisitionState(OScDev_Device* device)
{
	if (GetData(device)->acqState == nullptr) {
		return;
	}

	RequestAcquisitionStop(GetData(device)->acqState);
	GetData(device)->acqState->finish.get();

	delete GetData(device)->acqState;
	GetData(device)->acqState = nullptr;
}


static int32_t PixelsToMacroTime(double pixels, double pixelRateHz, uint32_t unitsTenthNs)
{
	return static_cast<int32_t>(std::round(1e10 * pixels / pixelRateHz / unitsTenthNs));
}


extern "C"
int StartAcquisition(OScDev_Device* device, OScDev_Acquisition* acq)
{
	int err = ResetAcquisitionState(device);
	if (err != 0)
		return err;
	auto acqState = GetData(device)->acqState;

	uint32_t nFrames = OScDev_Acquisition_GetNumberOfFrames(acq);
	double pixelRateHz = OScDev_Acquisition_GetPixelRate(acq);
	uint32_t xOffset, yOffset, width, height;
	OScDev_Acquisition_GetROI(acq, &xOffset, &yOffset, &width, &height);

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
	std::string spcFilename(GetData(device)->spcFilename);

	char fileHeader[4];
	short fifoType;
	int macroTimeUnitsTenthNs;
	err = SetUpAcquisition(GetData(device)->moduleNr, fileHeader,
		&fifoType, &macroTimeUnitsTenthNs);
	if (err != 0)
		return err;
	if (!IsStandardFIFO(fifoType)) {
		return 1; // Unsupported data format
	}

	uint32_t lineTime = PixelsToMacroTime(width, pixelRateHz, macroTimeUnitsTenthNs);
	int32_t lineDelay = PixelsToMacroTime(lineDelayPixels, pixelRateHz, macroTimeUnitsTenthNs);
	if (lineMarkersAtLineEnds) {
		lineDelay -= lineTime;
	}

	auto pool = std::make_shared<EventBufferPool<BHSPCEvent>>(48 * 1024);

	auto spcFile = std::make_shared<std::fstream>(spcFilename, std::fstream::binary | std::fstream::out);
	if (!spcFile->good()) {
		return 1; // Cannot open spc data file
	}
	spcFile->write(fileHeader, sizeof(fileHeader));
	
	std::shared_future<void> stopRequested =
		acqState->requestStop.get_future().share();

	auto stream_finish = SetUpProcessing(width, height, nFrames,
		lineDelay, lineTime, acq,
		[acqState]() mutable { RequestAcquisitionStop(acqState); },
		*spcFile);
	auto& stream = std::get<0>(stream_finish);
	auto dataFinished = std::get<1>(stream_finish);

	auto acqFinished = StartAcquisitionStandardFIFO(GetData(device)->moduleNr,
		pool, stream, stopRequested).share();

	acqState->finish = std::async(std::launch::async, [dataFinished, acqFinished, spcFile]() {
		dataFinished.get();
		spcFile->close(); // (Actually we could just let this go out of scope)
		acqFinished.get();
	}).share();

	return 0;
}


extern "C"
void StopAcquisition(OScDev_Device* device)
{
	if (GetData(device)->acqState == nullptr)
		return;

	RequestAcquisitionStop(GetData(device)->acqState);
}


extern "C"
bool IsAcquisitionRunning(OScDev_Device* device)
{
	if (GetData(device)->acqState == nullptr)
		return false;

	using namespace std::chrono_literals;
	return GetData(device)->acqState->finish.wait_for(0s) != std::future_status::ready;
}


extern "C"
void WaitForAcquisitionToFinish(OScDev_Device* device)
{
	if (GetData(device)->acqState == nullptr)
		return;

	GetData(device)->acqState->finish.get();
}