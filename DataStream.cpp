#include "DataStream.hpp"

#include <FLIMEvents/Histogram.hpp>


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
	int32_t lineDelay, uint32_t lineTime,
	OScDev_Acquisition* acquisition, std::function<void()> stopFunc,
	std::ostream& spcFile)
{
	int32_t inputBits = 12;
	int32_t histoBits = 0; // Intensity image
	Histogram<SampleType> frameHisto(histoBits, inputBits, width, height);
	Histogram<SampleType> cumulHisto(histoBits, inputBits, width, height);
	cumulHisto.Clear();

	auto sink = std::make_shared<DataSink>([stopFunc] {stopFunc(); }, acquisition);

	auto processor =
		std::make_shared<LineClockPixellator>(width, height, maxFrames, lineDelay, lineTime,
			std::make_shared<Histogrammer<SampleType>>(std::move(frameHisto),
				std::make_shared<HistogramAccumulator<SampleType>>(std::move(cumulHisto),
				sink)));

	auto decoder = std::make_shared<BHSPCEventDecoder>();
	decoder->SetDownstream(processor);

	auto stream = std::make_shared<EventStream<BHSPCEvent>>();
	auto done = std::async(std::launch::async, [stream, decoder, processor, &spcFile] {
		for (;;) {
			std::shared_ptr<EventBuffer<BHSPCEvent>> buffer;
			try {
				buffer = stream->ReceiveBlocking();
			}
			catch (std::exception const& e) {
				// Decoder doesn't have error propagation, so bypass
				processor->HandleError(e.what());
				break;
			}

			if (!buffer) {
				decoder->HandleFinish();
				break;
			}

			decoder->HandleDeviceEvents(buffer->GetData(), buffer->GetSize());

			// Note: the processor may have stopped the acquisition based on
			// seeing this data. We do not make any attempt to stop writing the
			// event data exactly where the processor decides to stop, so
			// typically there will be some extra data written beyond where the
			// decision to stop was made. This should not be an issue as long
			// as the raw event file is processed by well-written code, but it
			// may become desirable to stop the file at an exact photon.
			spcFile.write(reinterpret_cast<char const*>(buffer->GetData()),
				buffer->GetSize() * decoder->GetEventSize());
		}
	});

	return std::make_tuple(stream, done.share());
}