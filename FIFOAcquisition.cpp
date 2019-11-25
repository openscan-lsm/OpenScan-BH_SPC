#include "FIFOAcquisition.hpp"

#include <Spcm_def.h>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <stdexcept>
#include <string>
#include <thread>


int ConfigureDeviceForFIFOAcquisition(short module)
{
	short err;

	err = SPC_test_id(module);
	if (err < 0) {
		return err;
	}
	short model = err;

	// Note: In addition to the model number, we can also get the (FPGA)
	// firmware version. However, we use FIFO mode (not FIFO Imaging mode),
	// which is not affected by firmware version.

	// FIFO (not FIFO Imaging) mode
	short mode;
	switch (model) {
	case M_SPC130:
		mode = 2;
		break;
	case M_SPC131:
	case M_SPC140:
	case M_SPC150:
	case M_SPC151:
	case M_SPC152:
	case M_SPC160:
	case M_SPC161:
	case M_SPC830:
	case M_SPC930:
		mode = 1;
		break;
	default:
		return 1; // Unsupported model
	// Note: SPC-600/630 could be supported, but data format will be different.
	}

	SPCdata parameters;
	std::memset(&parameters, 0, sizeof(parameters)); // Defensive
	err = SPC_get_parameters(module, &parameters);
	if (err < 0) {
		return err;
	}

	parameters.mode = mode;
	parameters.adc_resolution = 12; // Do not discard bits!
	parameters.count_incr = 1;
	parameters.stop_on_time = 0; // We stop based on markers

	// Bits  8-11: enable marker
	// Bits 12-15: marker polarity (1 = rising)
	// TODO User should select rising/falling/disable for each of 4 markers
	// TODO User should also select which marker to use as line marker
	// For now, enable all 4 markers at rising edge.
	parameters.routing_mode = 0xff00;

	// The following parameters are not relevant, but clear them to avoid
	// confusion.
	parameters.stop_on_ovfl = 0; // Doesn't apply to FIFO mode
	parameters.collect_time = 0; // Not used as stop_on_time == 0
	parameters.repeat_time = 0; // Not used in FIFO mode
	parameters.scan_polarity = 0; // Not used in FIFO mode
	parameters.pixel_clock = 0; // Not used in FIFO mode
	parameters.adc_zoom = 0; // Not relevant in FIFO mode

	err = SPC_set_parameters(module, &parameters);
	if (err < 0) {
		return err;
	}

	return 0;
}


// Prepare for acquisition and determine necessary parameters for data handling
// fileHeader: set to first 4 bytes of the (4- or 6-byte) .spc file header
// fifoType: set to FIFO_48, FIFO_32, FIFO_130, etc.
int SetUpAcquisition(short module, char fileHeader[4], short* fifoType, int* macroTimeClockTenthNs)
{
	short err;

	// Somewhat surprisingly, there are no parameters that we need to set for
	// each acquisition. This is because different scan rates and image sizes
	// are handled when interpreting the photon event stream.

	// Check that Sync is good. This check is not atomic (obviously), but it is
	// good to fail early.
	short syncState;
	err = SPC_get_sync_state(module, &syncState);
	if (err < 0) {
		return err;
	}
	if (syncState != 1) {
		return 1; // TODO Error: No sync (or overload)
	}

	// Get the event data format and .spc file header
	short streamType;
	unsigned int fileHeaderInt;
	err = SPC_get_fifo_init_vars(module, fifoType, &streamType,
		macroTimeClockTenthNs, &fileHeaderInt);
	if (err < 0) {
		return err;
	}
	memcpy(fileHeader, &fileHeaderInt, 4);

	return 0;
}


bool IsStandardFIFO(short fifoType) {
	switch (fifoType) {
	case FIFO_130:
	case FIFO_830:
	case FIFO_140:
	case FIFO_150:
		return true;
	}
	return false;
}


bool IsSPC600FIFO32(short fifoType) {
	return fifoType == FIFO_32;
}


bool IsSPC600FIFO48(short fifoType) {
	return fifoType == FIFO_48;
}


template <typename E>
static void PushError(short code, EventStream<E>* stream)
{
	char buffer[512];
	short err = SPC_get_error_string(code, buffer, sizeof(buffer) - 1);
	if (err < 0) {
		std::runtime_error e("Unknown SPC error: " + std::to_string(code));
		stream->SendException(std::make_exception_ptr(e));
	}
	else {
		std::runtime_error e("SPC error: " + std::string(buffer));
		stream->SendException(std::make_exception_ptr(e));
	}
}


// Wrap SPC_read_fifo() to use sane units (instead of 2-byte words)
template <typename E, typename T>
static short ReadFifo(short module, std::size_t* eventCount, T* buffer)
{
	unsigned long wordCount = static_cast<unsigned long>(*eventCount * sizeof(E) / 2);
	char* rawBuffer = reinterpret_cast<char*>(buffer);
	short ret = SPC_read_fifo(module, &wordCount, reinterpret_cast<unsigned short*>(rawBuffer));
	*eventCount = wordCount * 2 / sizeof(E);
	return ret;
}


// Start measurement, read data, stop measurement
// pool: buffer pool for data
// stream: destination for data
// stopRequested: setting this future's shared state stops the acquisition
template <typename E>
static void RunAcquisition(short module, EventBufferPool<E>* pool,
	EventStream<E>* stream, std::shared_future<void> stopRequested)
{
	short err;

	// Note that, in all code paths, we ensure that measurement is stopped
	// _before_ the stream is finished (successfully or with error).

	err = SPC_start_measurement(module);
	if (err < 0) {
		PushError(err, stream);
		return;
	}

	// Since we are not using stop_on_time, measurement continues until we
	// decide to stop from software. That decision is made by downstream data
	// analysis, or user input. Thus, we have no need for SPC_test_state().

	// Our read loop ensures that we send any read data within 20 ms, but
	// otherwise avoids sending data in batches smaller than a fraction of the
	// capacity of a buffer.

	for (;;) {
		using namespace std::chrono_literals;
		if (stopRequested.wait_for(0s) == std::future_status::ready) {
			break;
		}

		auto buffer = pool->CheckOut();

		std::size_t eventCount = buffer->GetCapacity();
		err = ReadFifo<E>(module, &eventCount, buffer->GetData());
		if (err < 0) {
			goto error;
		}
		std::size_t eventsRead = eventCount;

		// No events (unlikely due to macro-time overflow records); wait a
		// little and try again.
		if (eventsRead == 0) {
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(20ms);
			continue;
		}

		// If buffer is <50% utilized, wait a little and try to fill further
		if (eventsRead * 2 < buffer->GetCapacity()) {
			using namespace std::chrono_literals;
			std::this_thread::sleep_for(20ms);

			eventCount = buffer->GetCapacity() - eventsRead;
			err = ReadFifo<E>(module, &eventCount, buffer->GetData() + eventsRead);
			if (err < 0) {
				goto error;
			}
			eventsRead += eventCount;
		}

		buffer->SetSize(eventsRead);

		stream->Send(buffer);
	}

	// A single call to SPC_stop_measurement() is sufficient, as we are NOT
	// using FIFO Imaging (FIFO_32M) mode.
	err = SPC_stop_measurement(module);
	if (err < 0) {
		PushError(err, stream);
		return;
	}

	// Indicate end of stream
	stream->Send({});

	return;

error:
	SPC_stop_measurement(module);
	PushError(err, stream);
}


template <typename E>
static std::future<void> StartAcquisition(short module,
	std::shared_ptr<EventBufferPool<E>> pool,
	std::shared_ptr<EventStream<E>> stream,
	std::shared_future<void> stopRequested)
{
	return std::async(std::launch::async, [module, pool, stream, stopRequested]() {
		RunAcquisition<E>(module, pool.get(), stream.get(), stopRequested);
	});
}


std::future<void> StartAcquisitionStandardFIFO(short module,
	std::shared_ptr<EventBufferPool<BHSPCEvent>> pool,
	std::shared_ptr<EventStream<BHSPCEvent>> stream,
	std::shared_future<void> stopRequested)
{
	return StartAcquisition<BHSPCEvent>(module, pool, stream, stopRequested);
}