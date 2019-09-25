#include "BH_SPC150.h"
#include "BH_SPC150Private.h"

#include <stdio.h>
#include <stdint.h>

static bool g_BH_initialized = false;
static OScDev_Device **g_devices;
static size_t g_deviceCount;
static size_t g_openDeviceCount = 0;


static DWORD WINAPI RateCounterMonitoringLoop(void *param)
{
	OScDev_Device *device = (OScDev_Device *)param;
	struct BH_PrivateData *privData = GetData(device);

	short moduleNr = 0; // TODO Avoid hard-coded module no

	EnterCriticalSection(&privData->rateCountersMutex);
	privData->rateCountersRunning = true;
	LeaveCriticalSection(&privData->rateCountersMutex);

	OScDev_Log_Debug(device, "Rate counter monitoring thread started");
	while (true)
	{
		Sleep(100);

		rate_values rates;
		short err = SPC_read_rates(moduleNr, &rates);
		if (err != 0) {
			continue;
		}

		EnterCriticalSection(&privData->rateCountersMutex);
		privData->syncRate = rates.sync_rate;
		privData->cfdRate = rates.cfd_rate;
		privData->tacRate = rates.tac_rate;
		privData->adcRate = rates.adc_rate;
		if (privData->rateCountersStopRequested) {
			privData->rateCountersRunning = false;
			LeaveCriticalSection(&privData->rateCountersMutex);
			break;
		}
		LeaveCriticalSection(&privData->rateCountersMutex);
	}

	OScDev_Log_Debug(device, "Exiting rate counter monitoring thread");

	WakeAllConditionVariable(&privData->rateCountersStopCondition);

	return 0;
}


static void PopulateDefaultParameters(struct BH_PrivateData *data)
{
	data->acqTime = 20;

	InitializeCriticalSection(&data->rateCountersMutex);
	InitializeConditionVariable(&data->rateCountersStopCondition);
	data->rateCountersStopRequested = false;
	data->rateCountersRunning = false;
	data->syncRate = 0.0;
	data->cfdRate = 0.0;
	data->tacRate = 0.0;
	data->adcRate = 0.0;
	
	strcpy(data->flimFileName, "default-BH-FLIM-data");

	InitializeCriticalSection(&(data->acquisition.mutex));
	InitializeConditionVariable(&(data->acquisition.acquisitionFinishCondition));
	data->acquisition.thread = NULL;
	data->acquisition.streamHandle = 0;
	data->acquisition.isRunning = false;
	data->acquisition.stopRequested = false;
	data->acquisition.acquisition = NULL;
}


static OScDev_Error EnsureFLIMBoardInitialized(void)
{
	// There is a mismatch between the OpenScan model of initializing a device
	// with how BH SPC works: multiple BH modules (boards) are initialized at
	// once using a single .ini file. We will need to figure out a workaround
	// for this when we support multiple modules, but for now we assume a
	// single module.

	if (g_BH_initialized) {
		// Reject second instance, as we don't yet support multiple modules
		return OScDev_Error_Device_Already_Open;
	}

	char iniFileName[] = "sspcm.ini";
	short spcErr = SPC_init(iniFileName);
	if (spcErr < 0)
	{
		char msg[OScDev_MAX_STR_LEN + 1] = "Cannot initialize BH SPC150 using ";
		strcat(msg, iniFileName);
		strcat(msg, ": ");
		char bhMsg[OScDev_MAX_STR_LEN + 1];
		SPC_get_error_string(spcErr, bhMsg, sizeof(bhMsg));
		strncat(msg, bhMsg, sizeof(msg) - strlen(msg) - 1);
		bhMsg[sizeof(bhMsg) - 1] = '\0';
		OScDev_Log_Error(NULL, msg);
		return OScDev_Error_Unknown; // TODO: error reporting
	}

	// Note: The force-initialize code that used to be here was removed after
	// testing to ensure it is not necessary (even after a crash).

	g_BH_initialized = true;
	return OScDev_OK;
}


static OScDev_Error DeinitializeFLIMBoard(void)
{
	if (!g_BH_initialized)
		return OScDev_OK;

	// See comment where we call SPC_init(). We assume only one module is in
	// use.
	SPC_close();

	g_BH_initialized = false;
	return OScDev_OK;
}


static OScDev_Error EnumerateInstances(OScDev_Device ***devices, size_t *count)
{
	// TODO SPC_test_id(0...7) can be used to get all modules present.
	// We would then have the user supply a .ini file for each module,
	// and check that the .ini file specifies the correct module number
	// before calling SPC_init().

	struct BH_PrivateData *data = calloc(1, sizeof(struct BH_PrivateData));
	// For now, support just one board
	data->moduleNr = 0;

	OScDev_Device *device;
	OScDev_Error err;
	if (OScDev_CHECK(err, OScDev_Device_Create(&device, &BH_TCSCP150_Device_Impl, data)))
	{
		char msg[OScDev_MAX_STR_LEN + 1] = "Failed to create device for BH SPC150";
		OScDev_Log_Error(device, msg);
		return err;
	}

	PopulateDefaultParameters(GetData(device));

	*devices = malloc(sizeof(OScDev_Device *));
	*count = 1;
	(*devices)[0] = device;

	return OScDev_OK;
}


static OScDev_Error BH_GetModelName(const char **name)
{
	*name = "Becker & Hickl TCSCP150";
	return OScDev_OK;
}


static OScDev_Error BH_GetInstances(OScDev_Device ***devices, size_t *count)
{
	OScDev_Error err;
	if (!g_devices && OScDev_CHECK(err, EnumerateInstances(&g_devices, &g_deviceCount)))
		return err;
	*devices = g_devices;
	*count = g_deviceCount;
	return OScDev_OK;
}


static OScDev_Error BH_ReleaseInstance(OScDev_Device *device)
{
	return OScDev_OK;
}


static OScDev_Error BH_GetName(OScDev_Device *device, char *name)
{
	strncpy(name, "BH SPC device", OScDev_MAX_STR_LEN);
	return OScDev_OK;
}


static OScDev_Error BH_Open(OScDev_Device *device)
{
	OScDev_Error err;
	if (OScDev_CHECK(err, EnsureFLIMBoardInitialized()))
		return err;

	PopulateDefaultParameters(GetData(device));

	// TODO There was previously a comment here suggesting that acquisitions
	// that did not cleanly finish or a previous program crash could cause
	// the module to remain "in use". Is this true? In any case, the following
	// result from SPC_get_module_info() is not being used and is also
	// redundant (see EnsureFLIMBoardInitialized() called above).
	SPCModInfo m_ModInfo;
	short spcErr = SPC_get_module_info(GetData(device)->moduleNr, (SPCModInfo *)&m_ModInfo);

	// TODO Documentation says a call to SPC_configure_memory() is NOT
	// required when operating in FIFO modes. Should remove this.
	SPCMemConfig memInfo;
	spcErr = SPC_configure_memory(GetData(device)->moduleNr,
		-1 /* TODO */, 0 /* TODO */, &memInfo);
	if (spcErr < 0 || memInfo.maxpage == 0)
	{
		return OScDev_Error_Unknown; //TODO: OScDev_Error_SPC150_MODULE_NOT_ACTIVE
	}

	DWORD threadId;
	CreateThread(NULL, 0, RateCounterMonitoringLoop, device, 0, &threadId);

	++g_openDeviceCount;

	OScDev_Log_Debug(device, "BH SPC150 board initialized");
	return OScDev_OK;
}


static OScDev_Error BH_Close(OScDev_Device *device)
{
	struct AcqPrivateData *acq = &GetData(device)->acquisition;
	EnterCriticalSection(&acq->mutex);
	acq->stopRequested = true;
	while (acq->isRunning)
		SleepConditionVariableCS(&acq->acquisitionFinishCondition, &acq->mutex, INFINITE);
	LeaveCriticalSection(&acq->mutex);

	struct BH_PrivateData *privData = GetData(device);
	EnterCriticalSection(&privData->rateCountersMutex);
	privData->rateCountersStopRequested = true;
	while (privData->rateCountersRunning)
		SleepConditionVariableCS(&privData->rateCountersStopCondition, &privData->rateCountersMutex, INFINITE);
	LeaveCriticalSection(&privData->rateCountersMutex);

	--g_openDeviceCount;

	OScDev_Error err;
	if (g_openDeviceCount == 0 && OScDev_CHECK(err, DeinitializeFLIMBoard()))
		return err;

	return OScDev_OK;
}


static OScDev_Error BH_HasScanner(OScDev_Device *device, bool *hasScanner)
{
	*hasScanner = false;
	return OScDev_OK;
}


static OScDev_Error BH_HasDetector(OScDev_Device *device, bool *hasDetector)
{
	*hasDetector = true;
	return OScDev_OK;
}


static OScDev_Error BH_HasClock (OScDev_Device *device, bool *hasDetector)
{
	*hasDetector = false;
	return OScDev_OK;
}


static OScDev_Error BH_GetSettings(OScDev_Device *device, OScDev_Setting ***settings, size_t *count)
{
	OScDev_Error err;
	if (OScDev_CHECK(err, BH_SPC150PrepareSettings(device)))
		return err;
	*settings = GetData(device)->settings;
	*count = GetData(device)->settingCount;
	return OScDev_OK;
}


static OScDev_Error BH_GetAllowedResolutions(OScDev_Device *device, size_t **widths, size_t **heights, size_t *count)
{
	// TODO If we are hard-coded to 256x256, we shouldn't allow anything else!
	static size_t resolutions[] = { 256, 512, 1024, 2048 };
	*widths = *heights = resolutions;
	*count = sizeof(resolutions) / sizeof(size_t);
	return OScDev_OK;
}


static OScDev_Error BH_GetResolution(OScDev_Device *device, size_t *width, size_t *height)
{
	SPCdata data;
	short spcRet = SPC_get_parameters(GetData(device)->moduleNr, &data);
	if (spcRet)
		return OScDev_Error_Unknown;

	// TODO Shouldn't be hard-coded!
	*height = 256;
	*width = 256;
	return OScDev_OK;
}


static OScDev_Error BH_SetResolution(OScDev_Device *device, size_t width, size_t height)
{
	short spcRet = SPC_set_parameter(GetData(device)->moduleNr,
		SCAN_SIZE_X, (float)width);
	if (spcRet)
		return OScDev_Error_Unknown;
	spcRet = SPC_set_parameter(GetData(device)->moduleNr,
		SCAN_SIZE_Y, (float)height);
	if (spcRet)
		return OScDev_Error_Unknown;
	return OScDev_OK;
}


static OScDev_Error BH_GetImageSize(OScDev_Device *device, uint32_t *width, uint32_t *height)
{
	// Currently all image sizes match the current resolution
	size_t w, h;
	OScDev_Error err = BH_GetResolution(device, &w, &h);
	if (err != OScDev_OK)
		return err;
	*width = (uint32_t)w;
	*height = (uint32_t)h;
	return err;
}


static OScDev_Error BH_GetNumberOfChannels(OScDev_Device *device, uint32_t *nChannels)
{
	*nChannels = 1;
	return OScDev_OK;
}


static OScDev_Error BH_GetBytesPerSample(OScDev_Device *device, uint32_t *bytesPerSample)
{
	*bytesPerSample = 2;
	return OScDev_OK;
}


// Main acquisition loop
static DWORD WINAPI BH_FIFO_Loop(void *param)
{
	OScDev_Error err;

	// TODO What is the principle by which setup code is distributed in
	// set_measurement_params() vs this function?
	// TODO We need to pass at least module no to set_measurement_params()
	if (OScDev_CHECK(err, set_measurement_params()))
		return err;

	OScDev_Device *device = (OScDev_Device *)param;
	struct AcqPrivateData *acq = &(GetData(device)->acquisition);

	// TODO Why call this a second time?
	if (OScDev_CHECK(err, set_measurement_params()))
		return err;

	acq->firstWrite = true;
	OScDev_Log_Debug(device, "Waiting for user to start FLIM acquisition...");

	short moduleNr = GetData(device)->moduleNr;

	unsigned short fpga_version;

	SPC_get_version(moduleNr, &fpga_version);
	// before the measurement sequencer must be disabled
	SPC_enable_sequencer(moduleNr, 0);
	// set correct measurement mode

	// TODO We seem to allow mode = ROUT_OUT and mode = FIFO_32M below.
	// Have we tested both? Shouldn't we always use FIFO_32M?
	float mode;
	SPC_get_parameter(moduleNr, MODE, &mode);

	if (mode != ROUT_OUT &&  mode != FIFO_32M) {
		SPC_set_parameter(moduleNr, MODE, ROUT_OUT);
		mode = ROUT_OUT;
	}
	if (mode == ROUT_OUT)
		acq->fifoType = FIFO_150;
	else  // FIFO_IMG ,  marker 3 can be enabled via ROUTING_MODE
		acq->fifoType = FIFO_IMG;

	// ROUTING_MODE sets active markers and their polarity in Fifo mode ( not for FIFO32_M)
	// bits 8-11 - enable Markers0-3,  bits 12-15 - active edge of Markers0-3

	// SCAN_POLARITY sets markers polarity in FIFO32_M mode

	float fval;
	SPC_get_parameter(moduleNr, SCAN_POLARITY, &fval);
	unsigned short scanPolarity = (unsigned short)fval;
	SPC_get_parameter(moduleNr, ROUTING_MODE, &fval);
	unsigned short routingMode = (unsigned short)fval;

	// TODO Although the settings for routing_mode and scan_polarity
	// follow the example code, they don't make sense. The docs do not
	// mention bits 0-5 of routing_mode. Why are we copying bits 0-2 of
	// scan_polarity into the same bits of routing_mode?

	// use the same polarity of markers in Fifo_Img and Fifo mode
	routingMode &= 0xfff8;
	routingMode |= scanPolarity & 0x7;

	// TODO Redundant (or move up to just after previous set)
	SPC_get_parameter(moduleNr, MODE, &mode);

	if (mode == ROUT_OUT) {
		routingMode |= 0xf00;     // markers 0-3 enabled
		SPC_set_parameter(moduleNr, ROUTING_MODE, routingMode);
	}
	if (mode == FIFO_32M) {
		routingMode |= 0x800;     // additionally enable marker 3
		SPC_set_parameter(moduleNr, ROUTING_MODE, routingMode);
		// TODO Redundant (we did not change scanPolarity)
		SPC_set_parameter(moduleNr, SCAN_POLARITY, scanPolarity);
	}

	float collectionTime = (float)GetData(device)->acqTime;
	SPC_set_parameter(-1, COLLECT_TIME, collectionTime);

	SPC_set_parameter(moduleNr, STOP_ON_OVFL, 1);
	SPC_set_parameter(moduleNr, STOP_ON_TIME, 1);

	unsigned long bufferCapacityEvents = 200000;
	unsigned long bufferCapacityWords = 2 * bufferCapacityEvents;

	acq->buffer = malloc(bufferCapacityWords * sizeof(unsigned short));
	if (acq->buffer ==NULL)
		return 0;

	// TODO Why 1e8 photons? Should this be configurable?
	unsigned long maxEventsToRead, maxWordsToRead, wordsRemaining;
	maxEventsToRead = 100000000;
	maxWordsToRead = 2 * maxEventsToRead;
	wordsRemaining = maxWordsToRead;
	
	strcpy(acq->photonFilename, "BH_photons.spc");//name will later be collected from user //FLIMTODO
	
	// TODO: not sure if the loop should directly exit here if stop is reuqested?
	short spcRet = SPC_start_measurement(GetData(device)->moduleNr);
	char msg[OScDev_MAX_STR_LEN + 1];
	snprintf(msg, OScDev_MAX_STR_LEN, "return value after start measurement %d", spcRet);
	OScDev_Log_Debug(device, msg);
	// TODO Stop with error if spcRet != 0

	unsigned long bufferDataSizeWords = 0;

	// TODO Probably better to sleep briefly before the 'continue' statements
	// in the loop below. 1 ms?

	while (!spcRet) 
	{
		EnterCriticalSection(&(acq->mutex));
		bool stopRequested = acq->stopRequested;
		LeaveCriticalSection(&(acq->mutex));
		if (stopRequested)
		{
			OScDev_Log_Debug(device, "User interruption...Exiting FLIM acquisition loop...");
			break;
		}

		// now test SPC state and read photons
		short state;
		SPC_test_state(moduleNr, &state);
		if (state & SPC_WAIT_TRG) {
			continue;
		}

		unsigned long readSizeWords;
		if (wordsRemaining > bufferCapacityWords - bufferDataSizeWords) {
			readSizeWords = bufferCapacityWords - bufferDataSizeWords;
		}
		else {
			// FIXME Shouldn't this be wordsRemaining?
			readSizeWords = bufferCapacityWords;
		}

		unsigned short *bufferStart = &(acq->buffer[bufferDataSizeWords]);

		if (state & SPC_ARMED) {
			if (state & SPC_FEMPTY) // FIFO is empty; nothing to read
				continue;

			spcRet = SPC_read_fifo(moduleNr, &readSizeWords, bufferStart);

			// FIXME wordsRemaining is unsigned, so we could wrap around
			// here until we fix the bug above computing readSizeWords.
			wordsRemaining -= readSizeWords;
			if (wordsRemaining <= 0)
				break;

			if (state & SPC_FOVFL) {
				// TODO This should report an error
				OScDev_Log_Debug(device, "SPC FIFO overflow (data lost)");
				// We could potentially continue (with a warning), as the
				// photon data stream can record a GAP.
				break;
			}

			if ((state & SPC_COLTIM_OVER) || (state & SPC_TIME_OVER)) {
				OScDev_Log_Debug(device, "SPC collection time reached");
				break;
			}

			bufferDataSizeWords += readSizeWords;
			if (bufferDataSizeWords == bufferCapacityWords) {
				// TODO Pass buffer pointer and data size to save func instead of
				// using AcqPrivateData as a transfer mechanism
				acq->bufferDataSizeWords = bufferDataSizeWords;
				spcRet= save_photons_in_file(acq);
				acq->bufferDataSizeWords = bufferDataSizeWords = 0;
			}
		}
		else { // Not armed
			// TODO Why not SPC_COLTIM_OVER here (compare above check)
			if ((state & SPC_TIME_OVER) != 0) {
				// Read the remaining events
				spcRet = SPC_read_fifo(moduleNr, &readSizeWords, bufferStart);

				// FIXME This is buggy: compare with code above
				wordsRemaining -= readSizeWords;
				bufferDataSizeWords += readSizeWords;
				acq->bufferDataSizeWords += readSizeWords;
				break;
			}
		}

		// TODO We check for this case above. Checking here is redundant,
		// and breaking is a bug because it may discard the last bit of data.
		if ((state & SPC_COLTIM_OVER) | (state & SPC_TIME_OVER)) {
				OScDev_Log_Debug(device, "SPC collection time reached");
				break; 
		}
	}

	OScDev_Log_Debug(device, "Finished FLIM");

	// See documentation: 2 calls are required. (Events generated up to the
	// first call can be read before the second call, but there is probably
	// no point in doing so because it is all software-timed.)
	SPC_stop_measurement(moduleNr);
	SPC_stop_measurement(moduleNr);

	if (bufferDataSizeWords > 0) {
		acq->bufferDataSizeWords = bufferDataSizeWords;
		spcRet = save_photons_in_file(acq);
	}

	free(acq->buffer);

	if (OScDev_CHECK(err, SaveHistogramAndIntensityImage(device)))
	{
		OScDev_Log_Error(device, "Error writing SDT file");
		BH_FinishAcquisition(device);
		return 0;
	}

	BH_FinishAcquisition(device);
	return 0;
}

static void BH_FinishAcquisition(OScDev_Device *device)
{
	struct AcqPrivateData *acq = &(GetData(device)->acquisition);
	EnterCriticalSection(&(acq->mutex));
	acq->isRunning = false;
	LeaveCriticalSection(&(acq->mutex));
	CONDITION_VARIABLE *cv = &(acq->acquisitionFinishCondition);
	WakeAllConditionVariable(cv);
}

OScDev_Error set_measurement_params() {
	// TODO Documentation says a call to SPC_configure_memory() is NOT
	// required when operating in FIFO modes. Should remove this.

	SPCMemConfig m_spc_mem_config;
	SPC_configure_memory(MODULE, -1, 0, &m_spc_mem_config);
	short ret = SPC_fill_memory(MODULE, -1, -1, 0);

	ret= SPC_set_page(MODULE, 0);
	if (ret != 0) {
		return ret;
	}

	return OScDev_OK;
}


OScDev_Error save_photons_in_file(struct AcqPrivateData *acq)
{
	FILE *fp;
	
	// TODO User-provided filename
	strcpy(acq->photonFilename, "BH_photons.spc");

	if (acq->firstWrite) {
		unsigned header;
		short ret = SPC_get_fifo_init_vars(0, NULL, NULL, NULL, &header);
		if (ret != 0) {
			return -1;
		}

		unsigned short first_frame[3];
		first_frame[0] = (unsigned short)header;
		first_frame[1] = (unsigned short)(header >> 16);
		first_frame[2] = 0;

		fp = fopen(acq->photonFilename, "wb");
		if (!fp)
			return -1;

		// TODO Here and elsewhere we have bits of code for 6-byte (3-word)
		// event records, but other parts of the code assume 4-byte records.
		// Delete code for FIFO_48 until we can fully support it.
		if (acq->fifoType == FIFO_48)
			fwrite(first_frame, sizeof(unsigned short), 3, fp);
		else
			fwrite(first_frame, sizeof(unsigned short), 2, fp);

		acq->firstWrite = false;
	}
	else {
		fp = fopen(acq->photonFilename, "ab");
		if (!fp)
			return -1;

		// TODO This is redundant because we passed "a" to fopen() above
		fseek(fp, 0, SEEK_END);
	}

	size_t wordsWritten = fwrite((void *)acq->buffer, sizeof(unsigned short),
		acq->bufferDataSizeWords, fp);
	fclose(fp);
	if (wordsWritten != acq->bufferDataSizeWords)
		return -1;

	return OScDev_OK;
}


OScDev_Error SaveHistogramAndIntensityImage(void *param) 
{
	OScDev_Device *device = (OScDev_Device *)param;
	struct AcqPrivateData *acq = &(GetData(device)->acquisition);

	// Note: We cannot use SPC_save_data_to_sdtfile(); that function is only
	// for conventional (non-FIFO) acquisition modes.

	int stream_type = BH_STREAM;
	int what_to_read = 1;   // valid photons
	if (acq->fifoType == FIFO_IMG) {
		stream_type |= MARK_STREAM;
		what_to_read |= (0x4 | 0x8 | 0x10);   // also pixel, line, frame markers possible
	}

	// TODO stream handle is only used in this function; pointless to put in AcqPrivateData.
	acq->streamHandle = SPC_init_phot_stream(acq->fifoType, acq->photonFilename, 1, stream_type, what_to_read);

	SPCdata parameters;
	SPC_get_parameters(MODULE, &parameters);

	const int histoResolutionBits = 8;

	// TODO Size should be variable
	unsigned pixelsPerLine = 256;
	unsigned linesPerFrame = 256;
	
	SYSTEMTIME st;
	GetSystemTime(&st);
	char date[11];
	sprintf_s(date, 11, "%02d:%02d:%04d", st.wMonth, st.wDay, st.wYear);
	char time[9];
	sprintf_s(time, 9, "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);

	char fileInfoString[512];
	int fileInfoLength = sprintf_s(fileInfoString, 512,
		"*IDENTIFICATION\r\n"
		"ID : SPC Setup & Data File\r\n"
		"Title : OpenScan\r\n"
		"Version : 1  781 M\r\n"
		"Revision : %d-bit histogram\r\n"
		"Date : %s\r\n"
		"Time : %s\r\n"
		"*END\r\n"
		"\r\n",
		histoResolutionBits, date, time);

	//TODO might have to add more here to comply with new file format
	char setupString[] =
		"*SETUP\r\n"
		"*END\r\n"
		"\r\n";
	short setupLength = (short)strlen(setupString);

	short moduleType = SPC_test_id(MODULE);

	bhfile_header fileHeader;
	fileHeader.revision = 0x28 << 4;
	fileHeader.info_offs = sizeof(bhfile_header);
	fileHeader.info_length = fileInfoLength;
	fileHeader.setup_offs = fileHeader.info_offs + fileHeader.info_length;
	fileHeader.setup_length = setupLength;
	fileHeader.meas_desc_block_offs = fileHeader.setup_offs + fileHeader.setup_length;
	fileHeader.meas_desc_block_length = sizeof(MeasureInfo);
	fileHeader.no_of_meas_desc_blocks = 1;
	fileHeader.data_block_offs = fileHeader.meas_desc_block_offs + fileHeader.meas_desc_block_length * fileHeader.no_of_meas_desc_blocks;
	fileHeader.data_block_length = pixelsPerLine * linesPerFrame * (1 << histoResolutionBits) * sizeof(short);
	fileHeader.no_of_data_blocks = 1;
	fileHeader.header_valid = BH_HEADER_VALID;
	fileHeader.reserved1 = fileHeader.no_of_data_blocks;
	fileHeader.reserved2 = 0;
	fileHeader.chksum = compute_checksum(&fileHeader);

	// Create Measurement Description Block
	MeasureInfo meas_desc;
	strcpy_s(meas_desc.time, 9, time);
	strcpy_s(meas_desc.date, 11, date);
	SPC_EEP_Data eepromContents;
	SPC_get_eeprom_data(0, &eepromContents);//MODULE
	strcpy_s(meas_desc.mod_ser_no, 16, eepromContents.serial_no);
	meas_desc.meas_mode = 9;  //WiscScan has a 9 here and says it is scan sync in mode, FIFO mode appears to be 11 from examining an SDT file I created
							  //leaving it as the 9 since I am mimicking the scan sync in file format
	meas_desc.cfd_ll = parameters.cfd_limit_low;
	meas_desc.cfd_lh = parameters.cfd_limit_high;
	meas_desc.cfd_zc = parameters.cfd_zc_level;
	meas_desc.cfd_hf = parameters.cfd_holdoff;
	meas_desc.syn_zc = parameters.sync_zc_level;
	meas_desc.syn_fd = parameters.sync_freq_div;
	meas_desc.syn_hf = parameters.sync_holdoff;
	meas_desc.tac_r = (float)(parameters.tac_range * 1e-9);
	meas_desc.tac_g = parameters.tac_gain;
	meas_desc.tac_of = parameters.tac_offset;
	meas_desc.tac_ll = parameters.tac_limit_low;
	meas_desc.tac_lh = parameters.tac_limit_high;
	meas_desc.adc_re = 1 << histoResolutionBits;
	meas_desc.eal_de = parameters.ext_latch_delay;
	meas_desc.ncx = 1;  //not sure what these three do, they were hardcoded to 1's in WiscScan
	meas_desc.ncy = 1;
	meas_desc.page = 1;
	meas_desc.col_t = parameters.collect_time;
	meas_desc.rep_t = parameters.repeat_time;
	meas_desc.stopt = parameters.stop_on_time;
	meas_desc.overfl = 'N';  //may want to set this eventually, but that would require storing the entire fifo acquisition somewhere while it is being acquired to check if an overflow occurs before writing this, the xml file wil contain the usual error flag so it shouldn't matter
	meas_desc.use_motor = 0;
	meas_desc.steps = 1;
	meas_desc.offset = 0.0;
	meas_desc.dither = parameters.dither_range;
	meas_desc.incr = parameters.count_incr;
	meas_desc.mem_bank = parameters.mem_bank;
	strcpy_s(meas_desc.mod_type, 16, eepromContents.module_type);
	meas_desc.syn_th = parameters.sync_threshold;
	meas_desc.dead_time_comp = parameters.dead_time_comp;
	meas_desc.polarity_l = parameters.scan_polarity & 1;
	meas_desc.polarity_f = (parameters.scan_polarity & 2) >> 1;
	meas_desc.polarity_p = (parameters.scan_polarity & 4) >> 2;
	meas_desc.linediv = 0;//2  //not sure what this is WiscScan comments indicate they were also unsure, could be line compression, I stole this value of 2 assuming they had a reason
	meas_desc.accumulate = 0;  //ditto, no clue what this is for
	meas_desc.flbck_x = parameters.scan_flyback & 0x0000FFFF;
	meas_desc.flbck_y = (parameters.scan_flyback >> 16) & 0x0000FFFF;
	meas_desc.bord_u = parameters.scan_borders & 0x0000FFFF;
	meas_desc.bord_l = (parameters.scan_borders >> 16) & 0x0000FFFF;
	meas_desc.pix_time = parameters.pixel_time;
	meas_desc.pix_clk = parameters.pixel_clock;
	meas_desc.trigger = parameters.trigger;

	meas_desc.scan_rx = 1; 
	meas_desc.scan_ry = 1;
	meas_desc.fifo_typ = 0;  //copied value from WiscScan, which in turn got it from looking at an SDT file
	meas_desc.epx_div = parameters.ext_pixclk_div;
	meas_desc.mod_type_code = moduleType;
	//meas_desc.mod_fpga_ver = 300;  //not sure how to get this value, chose the only value I found mentioned in the documentation, WiscScan isn't setting this
	meas_desc.overflow_corr_factor = 0.0;  //value from WiscScan
	meas_desc.adc_zoom = parameters.adc_zoom;
	meas_desc.cycles = 1;

	meas_desc.scan_x = pixelsPerLine;
	meas_desc.scan_y = linesPerFrame;

	meas_desc.scan_x = 256; //1
	meas_desc.scan_y = 256; //1
	meas_desc.scan_rx = 1;
	meas_desc.scan_ry = 1;

	meas_desc.image_x = 256;
	meas_desc.image_y = 256;

	meas_desc.image_rx = 1;//1 
	meas_desc.image_ry = 1;
	meas_desc.xy_gain = parameters.xy_gain;
	meas_desc.dig_flags = parameters.master_clock;

	// Create Data Block Header
	// FIXME 'lblock_no' is written twice below, probably meant to set
	// 'block_no' in the old-style block header.
	BHFileBlockHeader block_header;
	block_header.lblock_no = 1;
	block_header.data_offs = fileHeader.data_block_offs + sizeof(BHFileBlockHeader);
	block_header.next_block_offs = block_header.data_offs + fileHeader.data_block_length;

	block_header.block_type = MEAS_DATA_FROM_FILE | PAGE_BLOCK;//this one works for our case
																   //block_header.block_type = 1;
	block_header.meas_desc_block_no = 0;
	block_header.lblock_no = ((MODULE & 3) << 24);
	block_header.block_length = fileHeader.data_block_length;

	unsigned histogramImageSizeBytes = pixelsPerLine * linesPerFrame * (1 << histoResolutionBits) * sizeof(uint16_t);
	uint16_t *histogramImage = calloc(1, histogramImageSizeBytes);
	if (histogramImage == NULL) {
		return OScDev_Error_Unknown;
	}

	// FIXME Size shouldn't be hard-coded
	unsigned intensityImageSizeBytes = 256 * 256 * sizeof(uint16_t);
	uint16_t *intensityImage = calloc(1, intensityImageSizeBytes);

	PhotStreamInfo unusedStreamInfo;

	int ret = 0;
	if (acq->streamHandle >= 0) {
		// Read stream info to skip
		// TODO This is almost certainly not needed unless we need the stream
		// info: the function does not operate like a sequential read, unlike
		// SPC_get_photon()
		SPC_get_phot_stream_info(acq->streamHandle, &unusedStreamInfo);

		unsigned frameIndex = 0;
		unsigned lineIndex = 0;
		bool firstLineMarkOfFrameSeen = false;

		uint64_t lineMarkMacroTime; // Units depend on SPC setting and model

		while (!ret) { // untill error (for example end of file)
			PhotInfo64 phot_info;
			ret = SPC_get_photon(acq->streamHandle, (PhotInfo *)&phot_info);
			// FIXME If we have an error, shouldn't we quit here instead of at
			// the next start of the loop?

			if (phot_info.flags & F_MARK) {
				frameIndex++;
				lineIndex = 0;
				firstLineMarkOfFrameSeen = false;
			}

			if (phot_info.flags & L_MARK) {
				lineMarkMacroTime = phot_info.mtime;

				// We do not increment lineIndex on the first line mark of
				// the frame, because that is the start of line 0.
				if (firstLineMarkOfFrameSeen) {
					lineIndex++;
				}
				else {
					firstLineMarkOfFrameSeen = true;
				}
			}

			if (phot_info.flags & NOT_PHOTON) {
				continue;
			}

			// The first frame may be incomplete, so discard
			// The second frame may contain ghosting (TODO does it?)
			if (frameIndex < 2)
				continue;

			if (lineIndex >= linesPerFrame) {
				// Detected more lines than expected; probably should report an error.
				continue;
			}

			uint64_t macroTimeSinceLineMark = phot_info.mtime - lineMarkMacroTime;

			// Map macro time to pixel index within line
			// TODO These should not be hard coded!
			uint64_t lineDelayMacroTime = 400; // Adjustable setting
			uint64_t lineDurationMacroTime = 5 * 256 * 40; // pixelTimeUs * pixelsPerLine * macroTimeTicksPerUs

			if (macroTimeSinceLineMark < lineDelayMacroTime) {
				continue; // Before line starts
			}

			uint64_t pixelIndex = (pixelsPerLine - 1) * (macroTimeSinceLineMark - lineDelayMacroTime) / lineDurationMacroTime;

			if (pixelIndex >= pixelsPerLine) {
				continue; // During retrace
			}

			unsigned histoBinsPerPixel = 1 << histoResolutionBits;

			uint16_t microTime = phot_info.micro_time; // 0-4095 for SPC150
			uint16_t histoBinIndex = microTime >> (12 - histoResolutionBits);

			intensityImage[lineIndex * pixelsPerLine + pixelIndex]++;

			histogramImage[
				lineIndex * pixelsPerLine * histoBinsPerPixel +
				pixelIndex * histoBinsPerPixel +
				histoBinIndex]++;
		}

		// Read stream info to skip
		// TODO This is almost certainly not needed unless we need the stream
		// info: the function does not operate like a sequential read, unlike
		// SPC_get_photon()
		SPC_get_phot_stream_info(acq->streamHandle, &unusedStreamInfo);

		SPC_close_phot_stream(acq->streamHandle);
	}

	// Temporary: send the total intensity image. A correct
	// implementation would send an intensity image for every frame scanned.
	OScDev_Log_Debug(device, "Sending intensity image...");
	OScDev_Acquisition_CallFrameCallback(acq->acquisition, 0, intensityImage);
	OScDev_Log_Debug(device, "Sent intensity image");

	char sdtFilename[500];
	sprintf_s(sdtFilename, sizeof(sdtFilename), GetData(device)->flimFileName);
	strcat_s(sdtFilename, sizeof(sdtFilename), ".sdt");

	FILE* sdtFile;
	fopen_s(&sdtFile, sdtFilename, "wb");
	if (sdtFile == NULL)
		return 0;

	// TODO Check for i/o errors
	fwrite(&fileHeader, sizeof(bhfile_header), 1, sdtFile);  //Write Header Block
	fwrite(fileInfoString, fileInfoLength, 1, sdtFile);  // Write File Info Block
	fwrite(setupString, setupLength, 1, sdtFile);  // Write Setup Block
	fwrite(&meas_desc, sizeof(MeasureInfo), 1, sdtFile);  //Write Measurement Description Block
	fwrite(&block_header, sizeof(BHFileBlockHeader), 1, sdtFile);  //Write Data Block Header
	fwrite(histogramImage, sizeof(short), histogramImageSizeBytes / sizeof(short), sdtFile);

	fclose(sdtFile);

	free(intensityImage);
	free(histogramImage);

	OScDev_Log_Debug(device, "Finished writing SDT file");

	return OScDev_OK;
}


static OScDev_Error BH_Arm(OScDev_Device *device, OScDev_Acquisition *acq)
{
	bool useClock, useScanner, useDetector;
	OScDev_Acquisition_IsClockRequested(acq, &useClock);
	OScDev_Acquisition_IsScannerRequested(acq, &useScanner);
	OScDev_Acquisition_IsDetectorRequested(acq, &useDetector);
	if (useClock || useScanner || !useDetector)
		return OScDev_Error_Unsupported_Operation;

	enum OScDev_ClockSource clockSource;
	OScDev_Acquisition_GetClockSource(acq, &clockSource);
	if (clockSource != OScDev_ClockSource_External)
		return OScDev_Error_Unsupported_Operation;

	struct AcqPrivateData *privAcq = &(GetData(device)->acquisition);
	EnterCriticalSection(&(privAcq->mutex));
	{
		if (privAcq->isRunning)
		{
			LeaveCriticalSection(&(privAcq->mutex));
			return OScDev_Error_Acquisition_Running;
		}
		privAcq->stopRequested = false;
		privAcq->isRunning = true;
		privAcq->acquisition = acq;
	}
	LeaveCriticalSection(&(privAcq->mutex));

	short moduleNr = GetData(device)->moduleNr;
	short state;
	SPC_test_state(moduleNr, &state);

	DWORD id;
	// FLIm acquisition thread
	privAcq->thread = CreateThread(NULL, 0, BH_FIFO_Loop, device, 0, &id);

	OScDev_Log_Debug(device, "FLIM armed");

	return OScDev_OK;
}


unsigned short compute_checksum(void* hdr) {

	unsigned short* ptr;
	unsigned short chksum = 0;
	ptr = (unsigned short*)hdr;
	for (int i = 0; i < BH_HDR_LENGTH / 2 - 1; i++) {

		chksum += ptr[i];
	}
	
	return (-chksum + BH_HEADER_CHKSUM);
}


static OScDev_Error BH_Start(OScDev_Device *device)
{
	// FLIM detector doesn't support this operation
	return OScDev_OK;
}


static OScDev_Error BH_Stop(OScDev_Device *device)
{
	EnterCriticalSection(&(GetData(device)->acquisition.mutex));
	{
		if (!GetData(device)->acquisition.isRunning)
		{
			LeaveCriticalSection(&(GetData(device)->acquisition.mutex));
			return OScDev_OK;
		}
		GetData(device)->acquisition.stopRequested = true;
	}
	LeaveCriticalSection(&(GetData(device)->acquisition.mutex));
	return BH_WaitForAcquisitionToFinish(device);
}


static OScDev_Error BH_WaitForAcquisitionToFinish(OScDev_Device *device)
{
	OScDev_Error err = OScDev_OK;
	CONDITION_VARIABLE *cv = &(GetData(device)->acquisition.acquisitionFinishCondition);

	EnterCriticalSection(&(GetData(device)->acquisition.mutex));
	while (GetData(device)->acquisition.isRunning)
	{
		SleepConditionVariableCS(cv, &(GetData(device)->acquisition.mutex), INFINITE);
	}
	LeaveCriticalSection(&(GetData(device)->acquisition.mutex));
	return err;
}


static OScDev_Error BH_IsRunning(OScDev_Device *device, bool *isRunning)
{
	EnterCriticalSection(&(GetData(device)->acquisition.mutex));
	*isRunning = GetData(device)->acquisition.isRunning;
	LeaveCriticalSection(&(GetData(device)->acquisition.mutex));
	return OScDev_OK;
}


static OScDev_Error BH_Wait(OScDev_Device *device)
{
	return BH_WaitForAcquisitionToFinish(device);
}


struct OScDev_DeviceImpl BH_TCSCP150_Device_Impl = {
	.GetModelName = BH_GetModelName,
	.GetInstances = BH_GetInstances,
	.ReleaseInstance = BH_ReleaseInstance,
	.GetName = BH_GetName,
	.Open = BH_Open,
	.Close = BH_Close,
	.HasScanner = BH_HasScanner,
	.HasDetector = BH_HasDetector,
	.HasClock = BH_HasClock,
	.GetSettings = BH_GetSettings,
	.GetAllowedResolutions = BH_GetAllowedResolutions,
	.GetResolution = BH_GetResolution,
	.SetResolution = BH_SetResolution,
	.GetImageSize = BH_GetImageSize,
	.GetNumberOfChannels = BH_GetNumberOfChannels,
	.GetBytesPerSample = BH_GetBytesPerSample,
	.Arm = BH_Arm,
	.Start = BH_Start,
	.Stop = BH_Stop,
	.IsRunning = BH_IsRunning,
	.Wait = BH_Wait,
};

static OScDev_Error GetDeviceImpls(struct OScDev_DeviceImpl **impls, size_t *implCount)
{
	if (*implCount < 1)
		return OScDev_OK;

	impls[0] = &BH_TCSCP150_Device_Impl;
	*implCount = 1;
	return OScDev_OK;
}


OScDev_MODULE_IMPL = {
	.displayName = "OpenScan BH-SPC150",
	.GetDeviceImpls = GetDeviceImpls,
};