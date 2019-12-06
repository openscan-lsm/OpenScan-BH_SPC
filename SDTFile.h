#pragma once

#include <Spcm_def.h>

#include <stdbool.h>
#include <stdint.h>


#ifdef __cplusplus
extern "C" {
#endif


// Channel-independent data needed to write an SDT file
struct SDTFileData {
	unsigned histogramBits; // 8 to 12
	unsigned width;
	unsigned height;
	unsigned numChannels;

	double pixelRateHz;
	unsigned macroTimeUnitsTenthNs;

	bool usePixelMarkers;
	bool pixelMarkersRecorded;
	bool lineMarkersRecorded;
	bool frameMarkersRecorded;

	// "reversed in software" - but BH sets this false in normal histogram
	// files, in which time is in natural direction.
	bool histogramTimeReversed;

	short moduleNumber; // 0 to 3
	char modelName[16]; // null-terminated
	char serialNumber[16]; // null-terminated
	short modelCode; // SPC_test_id()
	unsigned short fpgaVersion; // SPC_get_version()

	char date[11]; // yyyy-mm-dd, null-terminated
	char time[9]; // hh:mm:ss, null-terminated

	// All fields below contain data only available after the acquisition has
	// completed.

	// Elapsed time between SPC_start_measurement() and SPC_stop_measurement()?
	// Or compute from last observed macro-time in event stream?
	float acquisitionDurationSeconds;

	float timeOfFirstFrameMarkerSeconds;
	float timeBetweenFrameMarkersSeconds;
	float timeBetweenLineMarkersSeconds; // 0 if no line marker
	float timeBetweenPixelMarkersSeconds; // 0 if no pixel marker

	bool recordRateCounterRanges;
	float minSync, maxSync;
	float minCFD, maxCFD;
	float minTAC, maxTAC;
	float minADC, maxADC;
};


struct SDTFileChannelData {
	// These 3 are the only parameters that change between channels.
	// Currently only time-gated channels are supported (no line-by-line or
	// frame-by-frame interleaving).
	unsigned channel; // 0-based
	unsigned numPhotonsInChannel;
	float timeOfLastPhotonInChannelSeconds;
};


int WriteSDTFile(const char *filename,
	const struct SDTFileData *data,
	const struct SDTFileChannelData *const channelDataArray[],
	const uint16_t *const channelHistograms[],
	const SPCdata *fifoModeParams);


#ifdef __cplusplus
} // extern "C"
#endif