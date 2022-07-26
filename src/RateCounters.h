#pragma once

#include "BH_SPC150Private.h"

#ifdef __cplusplus
extern "C" {
#endif

struct RateCounts *StartRateCounterMonitor(short module,
                                           float intervalSeconds);
void StopRateCounterMonitor(struct RateCounts *data);

// 'values' is float[4]
void GetRates(const struct RateCounts *rates, float *values);
void SetRates(struct RateCounts *rates, const float *values);

#ifdef __cplusplus
} // extern "C"
#endif
