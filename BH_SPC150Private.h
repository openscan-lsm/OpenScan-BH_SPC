#pragma once

#include <OpenScanDeviceLib.h>

#include <Windows.h>


struct AcqState; // Defined in C++
struct RateCounts; // Defined in C++


// This is uniform across all BH SPC models supporting FIFO mode and external
// markers.
#define NUM_MARKER_BITS 4

// All BH SPC models supporting FIFO mode have 4 routing bits in FIFO mode.
#define MAX_NUM_CHANNELS 16


enum MarkerPolarity {
	MarkerPolarityDisabled,
	MarkerPolarityRisingEdge,
	MarkerPolarityFallingEdge,

	MarkerPolarityNumValues,
};


enum ScanMarkerType {
	ScanMarkerTypePixelMarker,
	ScanMarkerTypeLineMarker,
	ScanMarkerTypeFrameMarker,
};


enum PixelMappingMode {
	PixelMappingModeLineStartMarkers,
	PixelMappingModeLineEndMarkers,
	// In future, add pixel marker modes
	PixelMappingModeNumValues,
};


struct BH_PrivateData
{
	short moduleNr;

	uint16_t channelMask;

	bool accumulateIntensity;

	// External marker configuration
	enum MarkerPolarity  markerActiveEdges[NUM_MARKER_BITS];
	uint32_t pixelMarkerBit; // no pixel marker iff >= NUM_MARKER_BITS
	uint32_t lineMarkerBit; // no line marker iff >= NUM_MARKER_BITS
	uint32_t frameMarkerBit; // no frame marker iff >= NUM_MARKER_BITS

	// Pixel assignment configuration
	enum PixelMappingMode pixelMappingMode;
	double lineDelayPx; // Delay of photons relative to markers

	char spcFilename[OScDev_MAX_STR_SIZE];
	char sdtFilename[OScDev_MAX_STR_SIZE];
	bool compressHistograms;

	bool checkSyncBeforeAcq;

	// C++ data for rate counter monitoring. Manually initialized on device
	// open; deleted on device close.
	struct RateCounts *rates;

	// C++ data for a single acquisition. Access to this pointer is not
	// protected by a mutex (i.e. relies on synchronization by OpenScanLib and
	// application). Thus, although we create a new AcqState for each
	// acquisition, the old AcqState must only be deallocated in the context
	// of a call from OpenScanLib.
	struct AcqState *acqState;
};


static inline struct BH_PrivateData *GetData(OScDev_Device *device)
{
	return (struct BH_PrivateData *)OScDev_Device_GetImplData(device);
}

OScDev_Error BH_MakeSettings(OScDev_Device *device, OScDev_PtrArray **settings);
