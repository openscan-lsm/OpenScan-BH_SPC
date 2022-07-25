#pragma once

#include <Spcm_def.h>

#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <future>
#include <memory>
#include <string>

class RateCountsProcessor {
  public:
    virtual ~RateCountsProcessor() = default;

    virtual void HandleRates(std::array<float, 4> rates) = 0;
    virtual void HandleError(std::string const &message) = 0;
    virtual void HandleFinish() = 0;
};

// Read rate counters in a background thread
class RateCounterMonitor {
    std::promise<void> requestStop;
    std::future<void> finish;

  public:
    // module must be initialized
    // Caller is responsible for using a single RateCounterMonitor per module
    RateCounterMonitor(short module,
                       std::shared_ptr<RateCountsProcessor> downstream,
                       float intervalSeconds = 1.0f) {
        using namespace std::chrono_literals;

        auto requestedMs = std::chrono::milliseconds(
            static_cast<int64_t>(std::round(1000.0f * intervalSeconds)));

        // Snap to closest allowedMs value
        std::array<std::chrono::milliseconds, 4> allowedMs = {
            1000ms,
            250ms,
            100ms,
            50ms,
        };
        std::chrono::milliseconds intervalMs(0);
        for (unsigned i = 0; i < allowedMs.size() - 1; ++i) {
            auto thresh = (allowedMs[i] + allowedMs[i + 1]) / 2;
            if (requestedMs >= thresh) {
                intervalMs = allowedMs[i];
            }
        }
        if (intervalMs == intervalMs.zero()) {
            intervalMs = allowedMs[allowedMs.size() - 1];
        }

        // Older models have fixed intervalMs
        switch (SPC_test_id(module)) {
        case M_SPC140:
            intervalMs = 50ms;
            break;
        case M_SPC600:
        case M_SPC630:
        case M_SPC700:
        case M_SPC730:
            intervalMs = 1000ms;
            break;
        }

        float seconds = intervalMs.count() / 1000.0f;
        short err = SPC_set_parameter(module, RATE_COUNT_TIME, seconds);
        // Ignore errors as long as we can get the intervalMs
        err = SPC_get_parameter(module, RATE_COUNT_TIME, &seconds);
        if (err < 0) {
            // Something is wrong; don't start background loop
            downstream->HandleError("Cannot get rate counter interval");

            std::promise<void> completed;
            completed.set_value();
            finish = completed.get_future();
            return;
        }
        auto interval = std::chrono::milliseconds(
            static_cast<int64_t>(std::round(1000.0f * seconds)));

        finish = std::async(
            std::launch::async,
            [module, downstream, interval,
             stopRequested = requestStop.get_future()]() mutable {
                short err = SPC_clear_rates(module);
                if (err < 0) {
                    downstream->HandleError("Cannot clear rate counters");
                    return;
                }

                while (stopRequested.wait_for(interval) !=
                       std::future_status::ready) {
                    rate_values rates;
                    short err = SPC_read_rates(module, &rates);
                    if (err == -SPC_RATES_NOT_RDY)
                        continue;
                    if (err < 0) { // Not supposed to happen
                        downstream->HandleError("Cannot read rate counters");
                        return;
                    }

                    downstream->HandleRates({
                        rates.sync_rate,
                        rates.cfd_rate,
                        rates.tac_rate,
                        rates.adc_rate,
                    });
                }
                downstream->HandleFinish();
            });
    }

    ~RateCounterMonitor() {
        requestStop.set_value();
        finish.get();
    }
};
