#pragma once

#include "AcquisitionCompletion.hpp"

#include <FLIMEvents/DeviceEvent.hpp>

#include <fstream>
#include <memory>


// Write .spc file with standard 4-byte format
class SPCFileWriter final : public DeviceEventProcessor {
	std::fstream file;
	std::shared_ptr<AcquisitionCompletion> downstream;

public:
	SPCFileWriter(std::string const& filename, char fileHeader[4],
		std::shared_ptr<AcquisitionCompletion> downstream) :
		file(filename, std::fstream::binary | std::fstream::out),
		downstream(downstream)
	{
		if (downstream) {
			downstream->AddProcess("SPCFileWriter");
		}

		if (!file.is_open()) {
			if (downstream) {
				downstream->HandleError("Cannot open SPC file", "SPCFileWriter");
				downstream.reset();
			}
			return;
		}

		file.write(fileHeader, 4);
		if (!file.good() && downstream) {
			downstream->HandleError("Write error in SPC file", "SPCFileWriter");
			downstream.reset();
		}
	}

	std::size_t GetEventSize() const noexcept override {
		return 4;
	}

	void HandleDeviceEvent(char const* event) override {
		HandleDeviceEvents(event, 1);
	}

	void HandleError(std::string const& message) override {
		file.close();
		if (downstream) {
			downstream->HandleError("Closed SPC file due to error: " + message, "SPCFileWriter");
			downstream.reset();
		}
	}

	void HandleFinish() override {
		file.close();
		if (downstream) {
			downstream->HandleFinish("SPCFileWriter");
			downstream.reset();
		}
	}

	void HandleDeviceEvents(char const* events, std::size_t count) override {
		file.write(events, GetEventSize() * count);
		if (!file.good()) {
			if (downstream) {
				downstream->HandleError("Write error in SPC file", "SPCFileWriter");
				downstream.reset();
			}
		}
	}
};