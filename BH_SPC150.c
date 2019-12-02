#include "BH_SPC150Private.h"

#include "AcquisitionControl.h"

#include <math.h>
#include <stdio.h>
#include <stdint.h>

static bool g_BH_initialized = false;
static size_t g_openDeviceCount = 0;


// Forward declaration
static OScDev_DeviceImpl BH_TCSPC_Device_Impl;


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
	InitializeCriticalSection(&data->rateCountersMutex);
	InitializeConditionVariable(&data->rateCountersStopCondition);
	data->rateCountersStopRequested = false;
	data->rateCountersRunning = false;
	data->syncRate = 0.0;
	data->cfdRate = 0.0;
	data->tacRate = 0.0;
	data->adcRate = 0.0;

	for (int i = 0; i < NUM_MARKER_BITS; ++i) {
		data->markerActiveEdges[i] = MarkerPolarityRisingEdge;
	}
	data->pixelMarkerBit = -1;
	data->lineMarkerBit = 1;
	data->frameMarkerBit = 2;

	data->pixelMappingMode = PixelMappingModeLineEndMarkers;
	data->lineDelayPx = 0.0;
	strcpy(data->spcFilename, "BH_photons.spc");
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
		char msg[OScDev_MAX_STR_LEN + 1] = "Cannot initialize BH SPC using ";
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


static OScDev_Error BH_EnumerateInstances(OScDev_PtrArray **devices)
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
	if (OScDev_CHECK(err, OScDev_Device_Create(&device, &BH_TCSPC_Device_Impl, data)))
	{
		char msg[OScDev_MAX_STR_LEN + 1] = "Failed to create device for BH SPC";
		OScDev_Log_Error(device, msg);
		return err;
	}

	PopulateDefaultParameters(GetData(device));

	*devices = OScDev_PtrArray_Create();
	OScDev_PtrArray_Append(*devices, device);
	return OScDev_OK;
}


static OScDev_Error BH_GetModelName(const char **name)
{
	// TODO Get actual model (store in device private data)
	*name = "Becker & Hickl TCSPC";
	return OScDev_OK;
}


static OScDev_Error BH_ReleaseInstance(OScDev_Device *device)
{
	free(GetData(device));
	return OScDev_OK;
}


static OScDev_Error BH_GetName(OScDev_Device *device, char *name)
{
	// TODO Name should probably include model name and module number
	strncpy(name, "BH-TCSPC", OScDev_MAX_STR_LEN);
	return OScDev_OK;
}


static OScDev_Error BH_Open(OScDev_Device *device)
{
	OScDev_Error err;
	if (OScDev_CHECK(err, EnsureFLIMBoardInitialized()))
		return err;

	PopulateDefaultParameters(GetData(device));

	if (OScDev_CHECK(err, InitializeDeviceForAcquisition(device)))
		return err;

	DWORD threadId;
	CreateThread(NULL, 0, RateCounterMonitoringLoop, device, 0, &threadId);

	++g_openDeviceCount;

	OScDev_Log_Debug(device, "BH SPC board initialized");
	return OScDev_OK;
}


static OScDev_Error BH_Close(OScDev_Device *device)
{
	ShutdownAcquisitionState(device);

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


static OScDev_Error BH_GetPixelRates(OScDev_Device *device, OScDev_NumRange **pixelRatesHz)
{
	*pixelRatesHz = OScDev_NumRange_CreateContinuous(1e3, 1e7);
	return OScDev_OK;
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


static unsigned short compute_checksum(void* hdr)
{
	unsigned short* ptr;
	unsigned short chksum = 0;
	ptr = (unsigned short*)hdr;
	for (int i = 0; i < BH_HDR_LENGTH / 2 - 1; i++) {
		chksum += ptr[i];
	}

	return (-chksum + BH_HEADER_CHKSUM);
}


// TODO: This should be refactored into a set of more generic and stateless
// functions to write an SDT file.
static OScDev_Error SaveHistogramAndIntensityImage(
	short module, short fifoType, const char *spcFilename, const char *sdtFilename)
{
	// Note: We cannot use SPC_save_data_to_sdtfile(); that function is only
	// for conventional (non-FIFO) acquisition modes.

	int stream_type = BH_STREAM;
	int what_to_read = 1;   // valid photons
	if (fifoType == FIFO_IMG) {
		stream_type |= MARK_STREAM;
		what_to_read |= (0x4 | 0x8 | 0x10);   // also pixel, line, frame markers possible
	}

	// TODO stream handle is only used in this function; pointless to put in AcqPrivateData.
	char nonConstSpcFilename[512];
	strncpy(nonConstSpcFilename, spcFilename, 511);
	short streamHandle = SPC_init_phot_stream(fifoType, nonConstSpcFilename, 1, stream_type, what_to_read);

	SPCdata parameters;
	SPC_get_parameters(module, &parameters);

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

	short moduleType = SPC_test_id(module);

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
	block_header.lblock_no = ((module & 3) << 24);
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
	if (streamHandle >= 0) {
		// Read stream info to skip
		// TODO This is almost certainly not needed unless we need the stream
		// info: the function does not operate like a sequential read, unlike
		// SPC_get_photon()
		SPC_get_phot_stream_info(streamHandle, &unusedStreamInfo);

		unsigned frameIndex = 0;
		unsigned lineIndex = 0;
		bool firstLineMarkOfFrameSeen = false;

		uint64_t lineMarkMacroTime; // Units depend on SPC setting and model

		while (!ret) { // untill error (for example end of file)
			PhotInfo64 phot_info;
			ret = SPC_get_photon(streamHandle, (PhotInfo *)&phot_info);
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
		SPC_get_phot_stream_info(streamHandle, &unusedStreamInfo);

		SPC_close_phot_stream(streamHandle);
	}

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

	return OScDev_OK;
}


static OScDev_Error Arm(OScDev_Device *device, OScDev_Acquisition *acq)
{
	bool useClock, useScanner, useDetector;
	OScDev_Acquisition_IsClockRequested(acq, &useClock);
	OScDev_Acquisition_IsScannerRequested(acq, &useScanner);
	OScDev_Acquisition_IsDetectorRequested(acq, &useDetector);
	if (useClock || useScanner || !useDetector)
		return OScDev_Error_Unsupported_Operation;

	OScDev_ClockSource clockSource;
	OScDev_Acquisition_GetClockSource(acq, &clockSource);
	if (clockSource != OScDev_ClockSource_External)
		return OScDev_Error_Unsupported_Operation;

	int err = StartAcquisition(device, acq);
	if (err != 0) {
		return err;
	}

	return OScDev_OK;
}


static OScDev_Error Start(OScDev_Device* device) {
	// We have no software start trigger
	return OScDev_OK;
}


static OScDev_Error Stop(OScDev_Device* device) {
	StopAcquisition(device);
	WaitForAcquisitionToFinish(device);
	return OScDev_OK;
}


static OScDev_Error IsRunning(OScDev_Device* device, bool* isRunning) {
	*isRunning = IsAcquisitionRunning(device);
	return OScDev_OK;
}


static OScDev_Error Wait(OScDev_Device* device) {
	WaitForAcquisitionToFinish(device);
	return OScDev_OK;
}


static OScDev_DeviceImpl BH_TCSPC_Device_Impl = {
	.GetModelName = BH_GetModelName,
	.EnumerateInstances = BH_EnumerateInstances,
	.ReleaseInstance = BH_ReleaseInstance,
	.GetName = BH_GetName,
	.Open = BH_Open,
	.Close = BH_Close,
	.HasScanner = BH_HasScanner,
	.HasDetector = BH_HasDetector,
	.HasClock = BH_HasClock,
	.MakeSettings = BH_MakeSettings,
	.GetPixelRates = BH_GetPixelRates,
	.GetNumberOfChannels = BH_GetNumberOfChannels,
	.GetBytesPerSample = BH_GetBytesPerSample,
	.Arm = Arm,
	.Start = Start,
	.Stop = Stop,
	.IsRunning = IsRunning,
	.Wait = Wait,
};


static OScDev_Error GetDeviceImpls(OScDev_PtrArray **impls)
{
	*impls = OScDev_PtrArray_CreateFromNullTerminated(
		(OScDev_DeviceImpl *[]) { &BH_TCSPC_Device_Impl, NULL });
	return OScDev_OK;
}


OScDev_MODULE_IMPL = {
	.displayName = "Becker & Hickl TCSPC Module",
	.GetDeviceImpls = GetDeviceImpls,
};