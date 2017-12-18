#include "BH_SPC150.h"
#include "BH_SPC150Private.h"

#include <stdio.h>


static OSc_Device **g_devices;
static size_t g_deviceCount;


struct ReadoutState
{
	PhotInfo64 *lineBuffer;
	size_t lineBufferSize;
	size_t linePhotonCount;
	size_t lineNr;
	size_t frameNr;

	// TODO Other fields for pixel-clock-based acquisition
	// TODO Extra state information if scanning bidirectionally
};


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
	OSc_Return_If_Error(BH_SPC150PrepareSettings(device));
	*settings = GetData(device)->settings;
	*count = GetData(device)->settingCount;
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


static DWORD WINAPI ReadoutLoop(void *param)
{
	OSc_Device *device = (OSc_Device *)param;
	struct AcqPrivateData *acq = &(GetData(device)->acquisition);
	short streamHandle = acq->streamHandle;

	PhotInfo64 *photonBuffer = calloc(1024, sizeof(PhotInfo64));

	const size_t lineBufferAllocSize = 1024 * 1024;
	struct ReadoutState readoutState;
	readoutState.lineBufferSize = lineBufferAllocSize;
	readoutState.lineBuffer = malloc(readoutState.lineBufferSize * sizeof(PhotInfo64));
	readoutState.linePhotonCount = 0;
	readoutState.lineNr = 0;
	readoutState.frameNr = 0;

	for (;;)
	{
		uint64_t photonCount = 0;
		short spcRet = SPC_get_photons_from_stream(streamHandle, photonBuffer, (int *)*(&photonCount));
		switch (spcRet)
		{
		case 0: // No error
			break;
		case 1: // Stop condition
			break;
		case 2: // End of stream
			break;
		default: // Error code
			goto cleanup;
		}

		// Create intensity image for now

		uint64_t pixelTime = acq->pixelTime;

		for (PhotInfo64 *photon = photonBuffer; photon < photonBuffer + photonCount; ++photon)
		{
			// TODO Check FIFO overflow flag

			if (photon->flags & NOT_PHOTON)
			{
				if (photon->flags & P_MARK)
				{
					// Not implemented yet; current impl uses clock
				}
				if (photon->flags & L_MARK)
				{
					PhotInfo64 *bufferedPhoton = readoutState.lineBuffer;
					PhotInfo64 *endOfLineBuffer = readoutState.lineBuffer +
						readoutState.linePhotonCount;
					uint64_t endOfLineTime = photon->mtime;
					uint64_t startOfLineTime = endOfLineTime - acq->pixelTime * acq->width;
					uint16_t pixelPhotonCount = 0;

					// Discard photons occurring before the first pixel of the line
					while (bufferedPhoton->mtime < startOfLineTime)
						++bufferedPhoton;

					for (size_t pixel = 0; pixel < acq->width; ++pixel)
					{
						uint64_t pixelStartTime = startOfLineTime + pixel * acq->pixelTime;
						uint64_t nextPixelStartTime = pixelStartTime + acq->pixelTime;
						while (bufferedPhoton < endOfLineBuffer &&
							bufferedPhoton->mtime < nextPixelStartTime)
						{
							++pixelPhotonCount;
							++bufferedPhoton;
						}

						acq->frameBuffer[readoutState.lineNr * acq->width + pixel] = pixelPhotonCount;
						pixelPhotonCount = 0;
					}

					readoutState.linePhotonCount = 0;
					readoutState.lineNr++;
				}
				if (photon->flags & F_MARK)
				{
					acq->acquisition->frameCallback(acq->acquisition, 0,
						acq->frameBuffer, acq->acquisition->data);
					readoutState.frameNr++;
					readoutState.lineNr = 0;
					if (readoutState.frameNr == acq->acquisition->numberOfFrames)
					{
						EnterCriticalSection(&(GetData(device)->acquisition.mutex));
						acq->stopRequested = true;
						LeaveCriticalSection(&(GetData(device)->acquisition.mutex));
					}
					goto cleanup; // Exit loop
				}
			}
			else // A bona fide photon
			{
				if (readoutState.linePhotonCount >= readoutState.lineBufferSize)
				{
					readoutState.lineBufferSize += lineBufferAllocSize;
					readoutState.lineBuffer = realloc(readoutState.lineBuffer,
						readoutState.lineBufferSize);
				}

				memcpy(&(readoutState.lineBuffer[readoutState.linePhotonCount++]),
					photon, sizeof(PhotInfo64));
			}
		}
	}

cleanup:
	free(readoutState.lineBuffer);
	free(photonBuffer);

	SPC_close_phot_stream(acq->streamHandle);

	return 0;
}


static DWORD WINAPI AcquisitionLoop(void *param)
{
	OSc_Device *device = (OSc_Device *)param;
	short moduleNr = GetData(device)->moduleNr;
	struct AcqPrivateData *acq = &(GetData(device)->acquisition);
	short streamHandle = acq->streamHandle;

	short spcRet = SPC_start_measurement(moduleNr);
	if (spcRet != 0)
	{
		return OSc_Error_Unknown;
	}

	// The flow of data is
	// SPC hardware -> "fifo" -> "stream" -> our memory buffer -> file/OpenScan
	// The fifo is part of the SPC device; the stream is in the PC RAM but
	// managed by the BH library.
	// In this thread we handle the transfer from fifo to stream.
	// The readout thread will handle downstream from the stream.

	while (!spcRet)
	{
		EnterCriticalSection(&(acq->mutex));
		bool stopRequested = acq->stopRequested;
		LeaveCriticalSection(&(acq->mutex));
		if (stopRequested)
			break;

		short state;
		SPC_test_state(moduleNr, &state);
		if (state == SPC_WAIT_TRG)
			continue; // TODO sleep briefly?
		if (state & SPC_FEMPTY)
			continue; // TODO sleep briefly?

		// For now, use a 1 MWord read at a time. Will need to measure performance, perhaps.
		unsigned long words = 1 * 1024 * 1024;
		spcRet = SPC_read_fifo_to_stream(streamHandle, moduleNr, &words);

		if (state & SPC_ARMED && state & SPC_FOVFL)
			break; // TODO Error
		if (state & SPC_TIME_OVER)
			break;
	}

	SPC_stop_measurement(moduleNr);
	// It has been observed that sometimes the measurement needs to be stopped twice.
	SPC_stop_measurement(moduleNr);

	// TODO Somebody has to close the stream, but that needs to happen after we have
	// read all the photons from it. Also in the case of error/overflow.

	return 0;
}


static OSc_Error BH_ArmDetector(OSc_Device *device, OSc_Acquisition *acq)
{
	struct AcqPrivateData *privAcq = &(GetData(device)->acquisition);

	short moduleNr = GetData(device)->moduleNr;
	SPCdata spcData;
	short spcRet = SPC_get_parameters(moduleNr, &spcData);
	if (spcRet)
		return OSc_Error_Unknown;

	privAcq->width = spcData.scan_size_x;
	privAcq->height = spcData.scan_size_y;
	size_t nPixels = privAcq->width * privAcq->height;
	privAcq->frameBuffer = malloc(nPixels * sizeof(uint16_t));
	privAcq->pixelTime = 50000; // Units of 0.1 ns (same as macro clock); TODO get this from scanner

	SPC_enable_sequencer(moduleNr, 0);

	if (spcData.mode != ROUT_OUT && spcData.mode != FIFO_32M)
		spcData.mode = ROUT_OUT;

	spcData.routing_mode &= 0xfff8;
	spcData.routing_mode |= spcData.scan_polarity & 0x07;

	if (spcData.mode == ROUT_OUT)
		spcData.routing_mode |= 0x0f00;
	else
		spcData.routing_mode |= 0x0800;

	spcData.stop_on_ovfl = 0;
	spcData.stop_on_time = 0; // We explicitly stop after the desired number of frames

	SPC_set_parameters(moduleNr, &spcData);

	short fifoType;
	short streamType;
	int initMacroClock;
	SPC_get_fifo_init_vars(moduleNr, &fifoType, &streamType, &initMacroClock, NULL);

	short whatToRead = 0x0001 | // valid photons
		0x0002 | // invalid photons
		0x0004 | // pixel markers
		0x0008 | // line markers
		0x0010 | // frame markers
		0x0020; // (marker 3)
	privAcq->streamHandle =
		SPC_init_buf_stream(fifoType, streamType, whatToRead, initMacroClock, 0);

	privAcq->acquisition = acq;
	privAcq->wroteHeader = false;
	strcpy(privAcq->fileName, "TODO.spc");

	EnterCriticalSection(&(privAcq->mutex));
	privAcq->stopRequested = false;
	LeaveCriticalSection(&(privAcq->mutex));

	DWORD id;
	privAcq->thread = CreateThread(NULL, 0, AcquisitionLoop, device, 0, &id);
	privAcq->readoutThread = CreateThread(NULL, 0, ReadoutLoop, device, 0, &id);
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