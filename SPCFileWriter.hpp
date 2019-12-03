#pragma once

#include <FLIMEvents/DeviceEvent.hpp>

#include <fstream>


// Write .spc file with standard 4-byte format
class SPCFileWriter final : DeviceEventProcessor {
	std::fstream file;

public:
	SPCFileWriter(std::string const& filename, char fileHeader[4]) :
		file(filename, std::fstream::binary | std::fstream::out)
	{
		if (file.is_open() && file.good()) {
			file.write(fileHeader, 4);
		}
	}

	bool IsValid() const noexcept {
		return file.is_open() && file.good();
	}

	std::size_t GetEventSize() const noexcept override {
		return 4;
	}

	void HandleDeviceEvent(char const* event) override {
		HandleDeviceEvents(event, 1);
	}

	void HandleError(std::string const& message) override {
		file.close();
	}

	void HandleFinish() override {
		file.close();
	}

	void HandleDeviceEvents(char const* events, std::size_t count) override {
		if (!file.is_open() || !file.good()) {
			return;
		}

		// TODO Report write errors
		file.write(events, GetEventSize() * count);
	}
};