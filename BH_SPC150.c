#include "BH_SPC150.h"
#include "OpenScanLibPrivate.h"

#include <Spcm_def.h>

#include <Windows.h>
#include <stdio.h>


static OSc_Device **g_devices;
static size_t g_deviceCount;


struct BH_PrivateData
{
	short moduleNr;
	struct {
		OSc_Acquisition *acquisition;
		CRITICAL_SECTION mutex;
		bool stopRequested;
		HANDLE thread;
		bool wroteHeader;
		char fileName[OSc_MAX_STR_LEN];
	} acquisition;
};


static inline struct BH_PrivateData *GetData(OSc_Device *device)
{
	return (struct BH_PrivateData *)(device->implData);
}


static OSc_Error EnumerateInstances(OSc_Device ***devices, size_t *count)
{
	// For now, support just one board

	struct BH_PrivateData *data = calloc(1, sizeof(struct BH_PrivateData));
	data->moduleNr = 0; // TODO for multiple modules
	InitializeCriticalSection(&(data->acquisition.mutex));

	OSc_Device *device;
	OSc_Error err;
	if (OSc_Check_Error(err, OSc_Device_Create(&device, &BH_TCSCP150_Device_Impl, data)))
	{
		char msg[OSc_MAX_STR_LEN + 1] = "Failed to create device for BH SPC150";
		OSc_Log_Error(NULL, msg);
		return err;
	}

	*devices = malloc(sizeof(OSc_Device *));
	*count = 1;
	(*devices)[0] = device;

	short spcErr = SPC_init("spcm.ini");
	if (spcErr < 0)
	{
		char msg[OSc_MAX_STR_LEN + 1] = "Cannot initialize BH SPC150 using: ";
		strcat(msg, "spcm.ini");
		OSc_Log_Error(NULL, msg);
		return OSc_Error_Unknown;
	}

	return OSc_Error_OK;
}


static OSc_Error BH_GetModelName(const char **name)
{
	*name = "Becker & Hickl TCSCP150";
	return OSc_Error_OK;
}


static OSc_Error BH_GetInstances(OSc_Device ***devices, size_t *count)
{
	if (!g_devices)
		OSc_Return_If_Error(EnumerateInstances(&g_devices, &g_deviceCount));
	*devices = g_devices;
	*count = g_deviceCount;
	return OSc_Error_OK;
}


static OSc_Error BH_ReleaseInstance(OSc_Device *device)
{
	return OSc_Error_OK;
}


static OSc_Error BH_GetName(OSc_Device *device, char *name)
{
	strncpy(name, "BH SPC device", OSc_MAX_STR_LEN);
	return OSc_Error_OK;
}


static OSc_Error BH_Open(OSc_Device *device)
{
	SPCMemConfig memInfo;
	short spcErr = SPC_configure_memory(GetData(device)->moduleNr,
		8 /* TODO */, 0 /* TODO */, &memInfo);
	if (spcErr < 0 || memInfo.maxpage == 0)
	{
		return OSc_Error_Unknown;
	}

	return OSc_Error_OK;
}


static OSc_Error BH_Close(OSc_Device *device)
{
	return OSc_Error_OK;
}


static OSc_Error BH_HasScanner(OSc_Device *device, bool *hasScanner)
{
	*hasScanner = false;
	return OSc_Error_OK;
}


static OSc_Error BH_HasDetector(OSc_Device *device, bool *hasDetector)
{
	*hasDetector = true;
	return OSc_Error_OK;
}


static OSc_Error BH_GetSettings(OSc_Device *device, OSc_Setting ***settings, size_t *count)
{
	*settings = NULL;
	*count = 0;
	return OSc_Error_OK;
}


static OSc_Error BH_GetImageSize(OSc_Device *device, uint32_t *width, uint32_t *height)
{
	*width = *height = 16; // TODO
	return OSc_Error_OK;
}


static OSc_Error BH_GetNumberOfChannels(OSc_Device *device, uint32_t *nChannels)
{
	*nChannels = 1;
	return OSc_Error_OK;
}


static OSc_Error BH_GetBytesPerSample(OSc_Device *device, uint32_t *bytesPerSample)
{
	*bytesPerSample = 2;
	return OSc_Error_OK;
}


static short SaveData(OSc_Device *device, unsigned short *buffer, size_t size)
{
	short moduleNr = GetData(device)->moduleNr;

	FILE *fp;
	if (!GetData(device)->acquisition.wroteHeader)
	{
		unsigned header;
		signed short ret = SPC_get_fifo_init_vars(moduleNr, NULL, NULL, NULL, &header);
		if (ret)
			return ret;

		// The following (including the size-2 fwrite) is just byte swapping, I think.
		// Let's not mess with it for now.
		unsigned short headerSwapped[] =
		{
			(uint16_t)header,
			(uint16_t)(header >> 16),
		};

		fp = fopen(GetData(device)->acquisition.fileName, "wb");
		if (!fp)
			return -1;
		fwrite(&headerSwapped[0], 2, 2, fp);

		GetData(device)->acquisition.wroteHeader = true;
	}
	else {
		fp = fopen(GetData(device)->acquisition.fileName, "ab");
		if (!fp)
			return -1;
		fseek(fp, 0, SEEK_END);
	}

	size_t bytesWritten = fwrite(buffer, 1, size, fp);
	fclose(fp);

	if (bytesWritten < size)
		return -1;

	return 0;
}


static DWORD WINAPI ReadLoop(void *param)
{
	OSc_Device *device = (OSc_Device *)param;
	short moduleNr = GetData(device)->moduleNr;
	OSc_Acquisition *acq = GetData(device)->acquisition.acquisition;

	size_t bufferSizeWords = 2 * 200000; // for SPC150
	unsigned short *buffer = calloc(sizeof(unsigned short), bufferSizeWords);

	short spcRet = SPC_start_measurement(moduleNr);
	if (spcRet != 0)
	{
		return OSc_Error_Unknown;
	}


	size_t wordsInBuffer = 0;
	const size_t wordsToReadPerCycle = 20000;

	while (!spcRet)
	{
		EnterCriticalSection(&(GetData(device)->acquisition.mutex));
		bool stopRequested = GetData(device)->acquisition.stopRequested;
		LeaveCriticalSection(&(GetData(device)->acquisition.mutex));
		if (stopRequested)
			break;

		short state;
		SPC_test_state(moduleNr, &state);
		if (state == SPC_WAIT_TRG)
			continue; // TODO sleep briefly?

		size_t wordsLeftThisCycle = wordsToReadPerCycle;
		unsigned long wordsToReadThisCycle = (unsigned long)wordsLeftThisCycle;
		size_t remainingBufferCapacityWords = bufferSizeWords - wordsInBuffer;
		if (remainingBufferCapacityWords < wordsLeftThisCycle)
			wordsToReadThisCycle = (unsigned long)remainingBufferCapacityWords;

		unsigned short *bufferStart = buffer + wordsInBuffer;

		if (state & SPC_FEMPTY)
			continue; // TODO sleep briefly?

		spcRet = SPC_read_fifo(moduleNr, &wordsToReadThisCycle, bufferStart);
		size_t wordsRead = wordsToReadThisCycle;
		wordsLeftThisCycle -= wordsRead;
		wordsInBuffer += wordsRead;

		if (state & SPC_ARMED)
		{
			if (wordsLeftThisCycle <= 0)
				break;

			if (state & SPC_FOVFL)
				break; // TODO Error?

			if (wordsInBuffer == bufferSizeWords)
			{
				spcRet = SaveData(device, buffer, 2 * wordsInBuffer);
				wordsInBuffer = 0;
			}
		}
		else if (state & SPC_TIME_OVER)
		{
			break;
		}
	}

	SPC_stop_measurement(moduleNr);  //TODO according to Sagar needs to stop twice
	if (wordsInBuffer > 0)
		spcRet = SaveData(device, buffer, 2 * wordsInBuffer);

	free(buffer);

	// TODO Convert file format SPC -> SDT

	return 0;
}


static OSc_Error BH_ArmDetector(OSc_Device *device, OSc_Acquisition *acq)
{
	short moduleNr = GetData(device)->moduleNr;
	SPC_enable_sequencer(moduleNr, 0);

	float mode;
	SPC_get_parameter(moduleNr, MODE, &mode);
	if (mode != ROUT_OUT && mode != FIFO_32M)
	{
		SPC_set_parameter(moduleNr, MODE, ROUT_OUT);
	}

	short fifoType = mode == ROUT_OUT ? FIFO_150 : FIFO_IMG;
	unsigned long fifo_size = 16 * 262144;  // 4194304 ( 4M ) 16-bit words for SPC150

	uint32_t scanPolarity;
	SPC_get_parameter(moduleNr, SCAN_POLARITY, (float *)&scanPolarity);

	uint32_t routingMode;
	SPC_get_parameter(moduleNr, ROUTING_MODE, (float *)&routingMode);
	routingMode &= 0xfff8;
	routingMode |= scanPolarity & 0x07;

	SPC_get_parameter(moduleNr, MODE, &mode);
	if (mode == ROUT_OUT)
		routingMode |= 0x0f00;
	else
		routingMode |= 0x0800;

	SPC_set_parameter(moduleNr, ROUTING_MODE, *(float *)&routingMode);
	SPC_set_parameter(moduleNr, SCAN_POLARITY, *(float *)&scanPolarity);

	SPC_set_parameter(moduleNr, STOP_ON_OVFL, 0);
	SPC_set_parameter(moduleNr, STOP_ON_TIME, 1);
	SPC_set_parameter(moduleNr, COLLECT_TIME, 10.0); // seconds; TODO

	GetData(device)->acquisition.acquisition = acq;
	GetData(device)->acquisition.wroteHeader = false;
	strcpy(GetData(device)->acquisition.fileName, "TODO.spc");

	EnterCriticalSection(&(GetData(device)->acquisition.mutex));
	GetData(device)->acquisition.stopRequested = false;
	LeaveCriticalSection(&(GetData(device)->acquisition.mutex));

	DWORD id;
	GetData(device)->acquisition.thread =
		CreateThread(NULL, 0, ReadLoop, device, 0, &id);
	return OSc_Error_OK;
}


static OSc_Error BH_StartDetector(OSc_Device *device, OSc_Acquisition *acq)
{
	return OSc_Error_Unsupported_Operation;
}


static OSc_Error BH_StopDetector(OSc_Device *device, OSc_Acquisition *acq)
{
	EnterCriticalSection(&(GetData(device)->acquisition.mutex));
	GetData(device)->acquisition.stopRequested = true;
	LeaveCriticalSection(&(GetData(device)->acquisition.mutex));
}


