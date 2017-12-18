#pragma once

#include "OpenScanLibPrivate.h"

#include <Spcm_def.h>

#include <Windows.h>


struct AcqPrivateData
{
	OSc_Acquisition *acquisition;

	uint16_t *frameBuffer;
	size_t width;
	size_t height;
	uint64_t pixelTime;

	CRITICAL_SECTION mutex;
	CONDITION_VARIABLE acquisitionFinishCondition;
	bool stopRequested;
	bool isRunning;
	HANDLE thread;
	HANDLE readoutThread;
	short streamHandle;

	bool wroteHeader;
	char fileName[OSc_MAX_STR_LEN];
};


struct BH_PrivateData
{
	short moduleNr;

	OSc_Setting **settings;
	size_t settingCount;

	struct AcqPrivateData acquisition;
};


static inline struct BH_PrivateData *GetData(OSc_Device *device)
{
	return (struct BH_PrivateData *)(device->implData);
}


OSc_Error BH_SPC150PrepareSettings(OSc_Device *device);