#pragma once

#include "AcquisitionCompletion.hpp"
#include "MemMapFile.hpp"
#include "TempDir.hpp"
#include "UDPSender.hpp"

#include <FLIMEvents/Histogram.hpp>

#include <chrono>
#include <future>
#include <memory>
#include <thread>


// Send frame histograms using a simple UDP + file protocol.
class DataSender final : public std::enable_shared_from_this<DataSender> {
	unsigned const nChannels;

	std::mutex mutex;
	unsigned nextSeqNo = 0;
	bool started = false;
	bool canceled = false;

	TempDir tempDir;
	std::unique_ptr<MemMapFile> mapped;

	std::unique_ptr<UDPSender> sender;

	std::shared_ptr<AcquisitionCompletion> downstream;

	std::future<void> linger;

	void SendError(std::string const& message) {
		bool series_started;

		{
			std::lock_guard<std::mutex> hold(mutex);
			canceled = true;
			series_started = started;
		}

		if (series_started)
			sender->SendMsg("end_series");

		if (downstream) {
			downstream->HandleError(message, "DataSender");
			downstream.reset();
		}
	}

	void Start(std::size_t nCh, std::size_t h, std::size_t w, std::size_t nTimeBins) {
		sender->SendMsg("new_series\tu16\t4\t" + std::to_string(nCh) + '\t' +
			std::to_string(h) + '\t' + std::to_string(w) + '\t' +
			std::to_string(nTimeBins) + '\t' + tempDir.GetPath());

		{
			std::lock_guard<std::mutex> hold(mutex);
			started = true;
		}
	}

public:
	DataSender(unsigned nChannels, uint16_t port,
		std::shared_ptr<AcquisitionCompletion> downstream) :
		nChannels(nChannels),
		sender(std::make_unique<UDPSender>(port)),
		downstream(downstream)
	{
		if (downstream) {
			downstream->AddProcess("DataSender");
		}
	}

	// Assumes channels come in in cyclic order
	void SetHistogram(unsigned channel, Histogram<uint16_t> const& histogram) {
		{
			std::lock_guard<std::mutex> hold(mutex);
			if (canceled)
				return;
		}

		std::size_t nElems = histogram.GetNumberOfElements();

		if (channel == 0) {
			std::size_t size = sizeof(uint16_t) * nElems * nChannels;
			std::string name = tempDir.GetPath() + "/" + std::to_string(nextSeqNo);
			mapped = std::make_unique<MemMapFile>(size, name);
		}

		std::size_t offset = sizeof(uint16_t) * nElems * channel;
		memcpy(mapped->Get() + offset, histogram.Get(), sizeof(uint16_t) * nElems);

		if (channel + 1 == nChannels) {
			if (nextSeqNo == 0) {
				Start(nChannels, histogram.GetHeight(), histogram.GetWidth(),
					histogram.GetNumberOfTimeBins());
			}
			mapped.reset();
			sender->SendMsg("element\t" + std::to_string(nextSeqNo));
			++nextSeqNo;
		}
	}

	void Finish() {
		bool series_started;

		{
			std::lock_guard<std::mutex> hold(mutex);
			series_started = started;
			started = false;
		}

		if (series_started)
			sender->SendMsg("end_series");

		if (downstream) {
			downstream->HandleFinish("DataSender");
			downstream.reset();

			linger = std::async(std::launch::async, [self = shared_from_this()]{
				// Give the receiver some time to read the last frames before TempDir is
				// deleted.
				std::this_thread::sleep_for(std::chrono::milliseconds(2000));
			});
		}
	}

	void HandleError(std::string const& message) {
		SendError("Stopping data sender due to error: " + message);
	}
};
