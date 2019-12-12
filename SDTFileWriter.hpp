#pragma once

#include "AcquisitionCompletion.hpp"
#include "SDTFile.h"

#include <FLIMEvents/Histogram.hpp>

#include <cstring>
#include <ctime>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>


// This is a high-level wrapper around SDTFile.{h,c} adding state and
// concurrency management.
class SDTWriter final : public std::enable_shared_from_this<SDTWriter> {
	std::string filename;
	SDTFileData data;
	std::vector<SDTFileChannelData> channelData;
	SPCdata params;

	std::mutex mutex; // Protects the next 3 members that are set by upstream
	std::vector<Histogram<uint16_t>> histograms;
	bool finishedRecordingPostAcquisitionData;
	bool canceled;
	bool writeStarted;

	std::future<void> asyncWriteCompletion;
	std::shared_ptr<AcquisitionCompletion> downstream;

	void SendError(std::string const& message) {
		{
			std::lock_guard<std::mutex> hold(mutex);
			canceled = true;
		}

		if (downstream) {
			downstream->HandleError(message, "SDTWriter");
			downstream.reset();
		}
	}

	// Threading design:
	// - Pre-acquisition data sould be set synchronously.
	// - Post-acquisition data and histograms are set from separate threads;
	//   they only interact through the flags protected by a mutex.
	//   (This assumes that all post-acquisition data comes from the same
	//   thread; if this is not the case, we may need to protect more under
	//   our mutex.)
	// - Once all data is set, we assume no more calls from upstream; now
	//   the writing can happen asynchronously.

public:
	SDTWriter(std::string const& filename, unsigned nChannels,
		std::shared_ptr<AcquisitionCompletion> downstream) :
		filename(filename),
		finishedRecordingPostAcquisitionData(false),
		canceled(false),
		writeStarted(false),
		downstream(downstream)
	{
		memset(&data, 0, sizeof(data));
		data.numChannels = nChannels;

		channelData.resize(nChannels);
		for (unsigned i = 0; i < nChannels; ++i) {
			auto& chData = channelData[i];
			memset(&chData, 0, sizeof(SDTFileChannelData));
			chData.channel = i;
		}

		memset(&params, 0, sizeof(params));

		histograms.resize(nChannels);

		if (downstream) {
			downstream->AddProcess("SDTWriter");
		}
	}

	// Should be called after setting all SPC parameters but before starting
	// measurement.
	void SetPreacquisitionData(short module, uint32_t histogramBits,
		uint32_t width, uint32_t height, bool useCompression,
		double pixelRateHz, bool usePixelMarkers, bool recordPixelMarkers,
		bool recordLineMarkers, bool recordFrameMarkers) {
		data.histogramBits = histogramBits;
		data.width = width;
		data.height = height;
		data.useCompression = useCompression;
		data.pixelRateHz = pixelRateHz;
		data.usePixelMarkers = usePixelMarkers;
		data.pixelMarkersRecorded = recordPixelMarkers;
		data.lineMarkersRecorded = recordLineMarkers;
		data.frameMarkersRecorded = recordFrameMarkers;

		short err = SPC_get_parameters(module, &params);
		if (err < 0) {
			SendError("Cannot get SPC parameters for SDT file");
			return;
		}

		int macroTimeUnitsTenthNs;
		err = SPC_get_fifo_init_vars(module, nullptr, nullptr, &macroTimeUnitsTenthNs, nullptr);
		if (err < 0) {
			SendError("Cannot get FIFO init vars for SDT file");
			return;
		}

		SPC_EEP_Data eepData;
		err = SPC_get_eeprom_data(module, &eepData);
		if (err < 0) {
			SendError("Cannot get EEPROM data for SDT file");
			return;
		}

		unsigned short version;
		err = SPC_get_version(module, &version);
		if (err < 0) {
			SendError("Cannot get FPGA version for SDT file");
			return;
		}

		data.macroTimeUnitsTenthNs = macroTimeUnitsTenthNs;

		data.moduleNumber = module;
		std::strncpy(data.modelName, eepData.module_type, sizeof(data.modelName));
		std::strncpy(data.serialNumber, eepData.serial_no, sizeof(data.serialNumber));
		data.modelCode = SPC_test_id(module);
		data.fpgaVersion = version;

		std::time_t now = std::time(nullptr);
		std::tm stm;
#ifdef _MSC_VER
		localtime_s(&stm, &now);
#else // POSIX
		localtime_r(&now, &stm);
#endif
		std::strftime(data.date, sizeof(data.date), "%F", &stm);
		std::strftime(data.time, sizeof(data.time), "%T", &stm);
	}

	// TODO Functions to set post-acquisition data

	// Indicate that no more post-acquisition data will be set (thus
	// writing can commence). Does not block. Not thread safe.
	void FinishPostAcquisitionData() {
		{
			std::lock_guard<std::mutex> hold(mutex);
			finishedRecordingPostAcquisitionData = true;
		}

		StartWritingFileIfReady();
	}

	// Return value indicates write finish; caller must keep histogram valid
	// until the future completes. Not thread safe.
	void SetHistogram(unsigned channel, Histogram<uint16_t>&& histogram) {
		{
			std::lock_guard<std::mutex> hold(mutex);
			histograms[channel] = std::move(histogram);
		}

		StartWritingFileIfReady();
	}

	void HandleError(std::string const& message) {
		SendError("Canceling SDT file due to error: " + message);
	}

private:
	void StartWritingFileIfReady() {
		{
			std::lock_guard<std::mutex> hold(mutex);

			if (canceled || writeStarted) {
				return;
			}

			if (!finishedRecordingPostAcquisitionData) {
				return;
			}
			for (auto const& histo : histograms) {
				if (!histo.IsValid())
					return;
			}

			writeStarted = true;
		}

		asyncWriteCompletion = std::async([self = shared_from_this()] {
			std::vector<uint16_t const*> histoDataPtrs;
			for (auto const& h : self->histograms) {
				histoDataPtrs.emplace_back(h.Get());
			}

			std::vector<SDTFileChannelData const*> chanDataPtrs;
			for (size_t i = 0; i < self->channelData.size(); ++i) {
				chanDataPtrs.emplace_back(&self->channelData[i]);
			}
			
			int err = WriteSDTFile(self->filename.c_str(), &self->data,
				chanDataPtrs.data(), histoDataPtrs.data(), &self->params);
			if (err) {
				self->SendError("Write error in SDT file");
			}
			else if (self->downstream) {
				self->downstream->HandleFinish("SDTWriter");
				self->downstream.reset();
			}
		});
	}
};