#include "RateCounters.h"
#include "RateCounters.hpp"

#include <memory>
#include <mutex>


struct RateCounts {
	std::unique_ptr<RateCounterMonitor> monitor;

	mutable std::mutex mutex;
	float rates[4];
};


class MyProcessor : public RateCountsProcessor {
	RateCounts& destination;

public:
	explicit MyProcessor(RateCounts& destination) :
		destination(destination)
	{}

	void HandleRates(std::array<float, 4> rates) override {
		SetRates(&destination, rates.data());
	}

	void HandleError(std::string const& message) override {
		// TODO We could log
	}

	void HandleFinish() override {
		float zeros[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		SetRates(&destination, zeros);
	}
};


extern "C" struct RateCounts *StartRateCounterMonitor(short module, float intervalSeconds)
{
	auto ret = new RateCounts;
	for (int i = 0; i < 4; ++i)
		ret->rates[i] = 0.0f;
	auto proc = std::make_shared<MyProcessor>(*ret);
	ret->monitor = std::make_unique<RateCounterMonitor>(module, proc, intervalSeconds);
	return ret;
}


extern "C" void StopRateCounterMonitor(struct RateCounts *data)
{
	delete data;
}


extern "C" void GetRates(const struct RateCounts *rates, float *values)
{
	std::lock_guard<std::mutex> hold(rates->mutex);
	for (int i = 0; i < 4; ++i)
		values[i] = rates->rates[i];
}


extern "C" void SetRates(struct RateCounts *rates, const float *values)
{
	std::lock_guard<std::mutex> hold(rates->mutex);
	for (int i = 0; i < 4; ++i)
		rates->rates[i] = values[i];
}