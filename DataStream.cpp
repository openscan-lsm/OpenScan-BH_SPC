#include "DataStream.hpp"

#include <FLIMEvents/Histogram.hpp>
#include <FLIMEvents/LineClockPixellator.hpp>

#include <array>


using SampleType = uint16_t;


namespace {
	class DataSink : public HistogramProcessor<SampleType> {
		std::function<void()> stopFunc;
		OScDev_Acquisition* acquisition;

	public:
		explicit DataSink(std::function<void()> stopFunc, OScDev_Acquisition* acquisition) :
			stopFunc(stopFunc),
			acquisition(acquisition)
		{}

		void HandleError(std::string const& message) override {
			stopFunc();
		}

		void HandleFrame(Histogram<SampleType> const& histogram) override {
			// TODO OScDev_Acquisition_CallFrameCallback() parameter should be const
			OScDev_Acquisition_CallFrameCallback(acquisition, 0,
				const_cast<void*>(reinterpret_cast<void const*>(histogram.Get())));
		}

		void HandleFinish() override {
			stopFunc();
		}
	};
}


template <typename E, unsigned N>
static void PumpDeviceEvents(std::shared_ptr<EventStream<E>> stream,
	std::array<std::shared_ptr<DeviceEventProcessor>, N> processors)
{
	for (;;) {
		std::shared_ptr<EventBuffer<E>> buffer;
		try {
			buffer = stream->ReceiveBlocking();
		}
		catch (std::exception const& e) {
			for (auto& p : processors) {
				p->HandleError(e.what());
			}
			break;
		}

		if (!buffer) {
			for (auto& p : processors) {
				p->HandleFinish();
			}
			break;
		}

		char const* data = reinterpret_cast<char const*>(buffer->GetData());
		for (auto& p : processors) {
			p->HandleDeviceEvents(data, buffer->GetSize());
		}
	}
}


// Returns:
//   0: stream to which events should be sent
//   1: future indicating end of processing
// acquisition: destination for intensity images
// stopFunc: callable to cancel acquisition
// spcFile: file for spc data
// Caller must prevent destruction of spcFile until the returned future fires.
// Header for spcFile should be written by caller ahead of time.
std::tuple<std::shared_ptr<EventStream<BHSPCEvent>>, std::shared_future<void>>
SetUpProcessing(uint32_t width, uint32_t height, uint32_t maxFrames,
	int32_t lineDelay, uint32_t lineTime, uint32_t lineMarkerBit,
	OScDev_Acquisition* acquisition, std::function<void()> stopFunc,
	std::shared_ptr<DeviceEventProcessor> additionalProcessor)
{
	int32_t inputBits = 12;
	int32_t histoBits = 0; // Intensity image
	Histogram<SampleType> frameHisto(histoBits, inputBits, width, height);
	Histogram<SampleType> cumulHisto(histoBits, inputBits, width, height);
	cumulHisto.Clear();

	auto sink = std::make_shared<DataSink>([stopFunc] {stopFunc(); }, acquisition);

	auto processor =
		std::make_shared<LineClockPixellator>(width, height, maxFrames, lineDelay, lineTime, lineMarkerBit,
			std::make_shared<Histogrammer<SampleType>>(std::move(frameHisto),
				std::make_shared<HistogramAccumulator<SampleType>>(std::move(cumulHisto),
				sink)));

	auto decoder = std::make_shared<BHSPCEventDecoder>(processor);

	std::array<std::shared_ptr<DeviceEventProcessor>, 2> procs{ decoder,
		additionalProcessor };

	auto stream = std::make_shared<EventStream<BHSPCEvent>>();
	auto done = std::async(std::launch::async, [stream, procs] {
		PumpDeviceEvents(stream, procs);
	});

	return std::make_tuple(stream, done.share());
}