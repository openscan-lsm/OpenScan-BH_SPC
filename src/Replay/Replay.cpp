#include "BHSPCFile.hpp"
#include "DataSender.hpp"
#include "FLIMEvents/BHDeviceEvent.hpp"
#include "FLIMEvents/Histogram.hpp"
#include "FLIMEvents/LineClockPixellator.hpp"
#include "FLIMEvents/PixelPhotonRouter.hpp"
#include "FLIMEvents/StreamBuffer.hpp"
#include "MetadataJson.hpp"

#include <bitset>
#include <cmath>
#include <fstream>
#include <future>
#include <iostream>
#include <memory>
#include <sstream>

void Usage() {
    std::cerr << "Replay .spc file and send histograms.\n"
              << "Usage: ReplaySPC <port> <input>\n"
              << "where input.spc and input.json must both exist.\n";
}

using SampleType = uint16_t;

template <typename T> class HistogramSink : public HistogramProcessor<T> {
    unsigned channel;
    std::shared_ptr<DataSender> dataSender;

  public:
    HistogramSink(unsigned channel, std::shared_ptr<DataSender> dataSender)
        : channel(channel), dataSender(dataSender) {}

    void HandleError(std::string const &message) override {
        if (dataSender) {
            dataSender->HandleError(message);
            dataSender.reset();
        }
    }

    void HandleFrame(Histogram<SampleType> const &histogram) override {
        if (dataSender) {
            dataSender->SetHistogram(channel, histogram);
        }
    }

    void HandleFinish(Histogram<SampleType> &&histogram,
                      bool isCompleteFrame) override {
        // isCompleteFrame is always true because our upstream guarantees it
        if (dataSender) {
            dataSender->Finish();
            dataSender.reset();
        }
    }
};

template <typename T>
static std::shared_ptr<PixelPhotonProcessor> MakeNoncumulativeHistogrammer(
    uint32_t histoBits, uint32_t inputBits, uint32_t width, uint32_t height,
    std::shared_ptr<HistogramProcessor<T>> downstream) {
    Histogram<T> frameHisto(histoBits, inputBits, true, width, height);
    return std::make_shared<Histogrammer<T>>(std::move(frameHisto),
                                             downstream);
}

template <typename T>
static std::shared_ptr<PixelPhotonProcessor>
MakeCumulativeHistogrammer(uint32_t histoBits, uint32_t inputBits,
                           uint32_t width, uint32_t height,
                           std::shared_ptr<HistogramProcessor<T>> downstream) {
    Histogram<T> cumulHisto(histoBits, inputBits, true, width, height);
    cumulHisto.Clear();
    return MakeNoncumulativeHistogrammer<T>(
        histoBits, inputBits, width, height,
        std::make_shared<HistogramAccumulator<T>>(std::move(cumulHisto),
                                                  downstream));
}

void replay(std::string const &inFilename,
            MetadataJsonReader const &jsonReader, uint16_t port) {

    std::fstream input(inFilename + ".spc",
                       std::fstream::binary | std::fstream::in);
    if (!input.is_open()) {
        throw std::runtime_error("Cannot open " + inFilename + ".spc");
    }
    char spcHeaderBuf[sizeof(BHSPCFileHeader)];
    input.read(spcHeaderBuf, sizeof(BHSPCFileHeader));
    BHSPCFileHeader spcHeader;
    std::memcpy(&spcHeader, spcHeaderBuf, sizeof(BHSPCFileHeader));
    uint32_t macrotimeUnitsTenthNs = spcHeader.GetMacroTimeUnitsTenthNs();

    std::bitset<16> channelMask = jsonReader.GetChannelMask();

    int32_t inputBits = 12;
    int32_t histoBits = 8;

    uint32_t width = jsonReader.GetRasterWidth();
    uint32_t height = jsonReader.GetRasterHeight();

    if (jsonReader.GetUsePixelClock())
        throw std::runtime_error(
            "JSON specifies use of pixel clock, which is not currently supported");

    uint32_t lineMarkerBit = jsonReader.GetLineMarkerBit();
    if (lineMarkerBit > 4)
        throw std::runtime_error("Line marker bit out of range");

    int32_t lineDelay = jsonReader.GetLineDelay();
    uint32_t lineTime = jsonReader.GetLineTime();

    double pixelRateHz = jsonReader.GetPixelRateHz();
    if (std::abs(lineTime -
                 1e10 * width / pixelRateHz / macrotimeUnitsTenthNs) >= 0.5)
        throw std::runtime_error(
            "JSON parameters don't match macrotime units (from .spc) of " +
            std::to_string(macrotimeUnitsTenthNs) + " x 0.1 ns");

    auto nChannels = static_cast<unsigned>(channelMask.count());
    auto sender = std::make_shared<DataSender>(nChannels, port, nullptr);

    std::vector<std::shared_ptr<PixelPhotonProcessor>> histogrammers;
    histogrammers.resize(nChannels);
    int n = 0;
    for (unsigned i = 0; i < channelMask.size(); ++i) {
        if (!channelMask[i])
            continue;
        auto histoSink =
            std::make_shared<HistogramSink<SampleType>>(n, sender);
        auto histoProc = MakeCumulativeHistogrammer<SampleType>(
            histoBits, inputBits, width, height, histoSink);
        histogrammers[n] = histoProc;
        ++n;
    }

    auto histProc = std::make_shared<PixelPhotonRouter>(histogrammers);

    auto pixellator = std::make_shared<LineClockPixellator>(
        width, height, UINT32_MAX, lineDelay, lineTime, lineMarkerBit,
        histProc);

    auto decoder = std::make_shared<BHSPCEventDecoder>(pixellator);

    EventStream<BHSPCEvent> stream;
    auto processorDone = std::async(std::launch::async, [&stream, decoder] {
        for (;;) {
            auto eventBuffer = stream.ReceiveBlocking();
            if (!eventBuffer) {
                break;
            }
            decoder->HandleDeviceEvents(
                reinterpret_cast<char const *>(eventBuffer->GetData()),
                eventBuffer->GetSize());
        }

        decoder->HandleFinish();
    });

    EventBufferPool<BHSPCEvent> pool(48 * 1024);
    while (input.good()) {
        auto buf = pool.CheckOut();
        auto const maxSize = buf->GetCapacity() * sizeof(BHSPCEvent);
        input.read(reinterpret_cast<char *>(buf->GetData()), maxSize);
        auto const readSize = input.gcount() / sizeof(BHSPCEvent);
        buf->SetSize(static_cast<std::size_t>(readSize));
        stream.Send(buf);
    }
    stream.Send({});

    processorDone.get();

    std::this_thread::sleep_for(std::chrono::seconds(2));
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        Usage();
        return 1;
    }

    uint16_t port;
    std::istringstream(argv[1]) >> port;

    std::string inFilename(argv[2]);

    MetadataJsonReader jsonReader(inFilename + ".json");
    if (!jsonReader.IsValid()) {
        std::cerr << "Cannot read " << inFilename << ".json\n";
        return 1;
    }

    try {
        replay(inFilename, jsonReader, port);
    } catch (std::exception const &e) {
        std::cerr << e.what();
        return 1;
    }

    return 0;
}
