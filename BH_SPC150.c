#include "BH_SPC150.h"
#include "BH_SPC150Private.h"

#include <stdio.h>
#include <stdint.h>

#pragma pack(1)

static bool g_BH_initialized = false;
static OScDev_Device **g_devices;
static size_t g_deviceCount;
static size_t g_openDeviceCount = 0;


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


// monitoring critical FLIM parameters
// such as CFD, Sync, etc.
// supposed to be running all the time
static DWORD WINAPI BH_Monitor_Loop(void *param)
{
	OScDev_Device *device = (OScDev_Device *)param;
	struct AcqPrivateData *acq = &(GetData(device)->acquisition);
	rate_values m_rates;
	short act_mod = 0;

	OScDev_Log_Debug(device, "FLIM monitoring thread starting...");
	bool stopRequested = false;
	while (true)
	{
		EnterCriticalSection(&(acq->mutex));
		// only exit the monitor thread when user set monitoringFLIM to OFF
		// and Stop Live (or Exit program) is clicked.
		stopRequested = !(GetData(device)->monitoringFLIM) && acq->stopRequested;
		LeaveCriticalSection(&(acq->mutex));
		if (stopRequested)
		{
			OScDev_Log_Debug(device, "User interruption...Exiting FLIM monitor loop");
			GetData(device)->flimStarted = false;  // reset for next run
			break;
		}

		if (SPC_read_rates(act_mod, &m_rates) == 0) {
			acq->cfd_value = m_rates.cfd_rate;
			acq->sync_value = m_rates.sync_rate;
			acq->adc_value = m_rates.adc_rate;
			acq->tac_value = m_rates.tac_rate;
		}
		Sleep(100);
	}

	BH_FinishAcquisition(device);
	return 0;
}


static void PopulateDefaultParameters(struct BH_PrivateData *data)
{
	data->settingsChanged = true;
	data->acqTime = 20;
	data->flimStarted = false;
	data->flimDone = false;
	data->monitoringFLIM = true;
	data->acquisition.cfd_value = 1.1;
	data->acquisition.sync_value = 2.1;
	data->acquisition.adc_value = 3.1;
	data->acquisition.tac_value = 4.1;
	strcpy(data->flimFileName, "default-BH-FLIM-data");

	InitializeCriticalSection(&(data->acquisition.mutex));
	InitializeConditionVariable(&(data->acquisition.acquisitionFinishCondition));
	data->acquisition.thread = NULL;
	data->acquisition.monitorThread = NULL;
	data->acquisition.streamHandle = 0;
	data->acquisition.isRunning = false;
	data->acquisition.started = false;
	data->acquisition.stopRequested = false;
	data->acquisition.acquisition = NULL;
}


static OScDev_Error EnsureFLIMBoardInitialized(void)
{
	if (g_BH_initialized)
		return OScDev_Error_Device_Already_Open;

	short spcErr;
	short spcRet;
	int active_board[1];
	SPCModInfo m_ModInfo;
	//short status=SPC_get_init_status();
	int  a = 0;
	//spcErr = SPC_close();  // close SPC150 if it remains open from previous session
	spcErr = SPC_init("sspcm.ini");
	if (spcErr < 0)
	{
		char msg[OScDev_MAX_STR_LEN + 1] = "Cannot initialize BH SPC150 using: ";
		strcat(msg, "Sspcm.ini");
		OScDev_Log_Error(NULL, msg);
		return OScDev_Error_Unknown; // TODO: add error msg: CANNOT_OPEN_FILE
	}

	spcRet = SPC_get_module_info(MODULE, (SPCModInfo *)&m_ModInfo);
	if (spcRet != 0) {
		SPC_get_error_string(spcRet, spcErr, 100);

	}

	int isBoardActive = 0;
	if (m_ModInfo.init == OK)
	{
		isBoardActive = 1;
	}
	else
	{	// the board is forced to be available to this program in harware mode
		//reset the board and reload
		short force_use = 1;
		active_board[0] = 1;
		spcRet = SPC_set_mode(SPC_HARD, force_use, active_board);

		//reset?
		SPC_get_module_info(MODULE, (SPCModInfo *)&m_ModInfo);

		if (m_ModInfo.init == OK)
		{
			isBoardActive = 1;
		}
		else
		{
			//resetting the board failed
			isBoardActive = 0;
		}
	}
	g_BH_initialized = true;
	return OScDev_OK;
}


static OScDev_Error DeinitializeFLIMBoard(void)
{
	if (!g_BH_initialized)
		return OScDev_OK;
	
	// TODO - close the FLIM board

	g_BH_initialized = false;
	return OScDev_OK;
}


static OScDev_Error EnumerateInstances(OScDev_Device ***devices, size_t *count)
{
	// TODO
	// check if the FLIM board is available in the system
	// then deinitialize it so that other FLIM software like SPCm can use it
	// when it is not used in OpenScan
	//OSc_Return_If_Error(EnsureFLIMBoardInitialized());
	//OSc_Return_If_Error(DeinitializeFLIMBoard());

	// For now, support just one board

	struct BH_PrivateData *data = calloc(1, sizeof(struct BH_PrivateData));
	data->moduleNr = 0; // TODO for multiple modules

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

	SPCModInfo m_ModInfo;
	// inUse = -1 means SPC150 board was still being used by previous session i.e. the code didn't exit correctly
	// TODO: need to find the way to exit the board when the software crashes
	short spcErr = SPC_get_module_info(GetData(device)->moduleNr, (SPCModInfo *)&m_ModInfo);
	//if (m_ModInfo.in_use == -1)
	//	SPC_set_mode(SPC_HARD, 1, 1);  // force to take control of the active board
	
	SPCMemConfig memInfo;
	spcErr = SPC_configure_memory(GetData(device)->moduleNr,
		-1 /* TODO */, 0 /* TODO */, &memInfo);
	if (spcErr < 0 || memInfo.maxpage == 0)
	{
		return OScDev_Error_Unknown; //TODO: OScDev_Error_SPC150_MODULE_NOT_ACTIVE
	}

	// read SPC150 parameters such as CFD, Sync, etc in a separate thread
	// TODO: the issue starting monitor loop here is that if Stop Live is clicked
	// i.e. stopRequested = true, the thread will exit and there is no way to restart it
	DWORD id;
	GetData(device)->acquisition.monitorThread = CreateThread(NULL, 0, BH_Monitor_Loop, device, 0, &id);

	++g_openDeviceCount;

	OScDev_Log_Debug(device, "BH SPC150 board initialized");
	return OScDev_OK;
}


static OScDev_Error BH_Close(OScDev_Device *device)
{
	struct AcqPrivateData *acq = &GetData(device)->acquisition;
	EnterCriticalSection(&acq->mutex);
	acq->stopRequested = true;
	GetData(device)->flimStarted = false; // reset for next run
	while (acq->isRunning)
		SleepConditionVariableCS(&acq->acquisitionFinishCondition, &acq->mutex, INFINITE);
	LeaveCriticalSection(&acq->mutex);

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

	//*width = data.scan_size_x;
	//*height = data.scan_size_y;

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


// Current main loop for FLIM acquisition
static DWORD WINAPI BH_FIFO_Loop(void *param)
{
	OScDev_Error err;
	if (OScDev_CHECK(err, set_measurement_params()))
		return err;

	OScDev_Device *device = (OScDev_Device *)param;
	struct AcqPrivateData *acq = &(GetData(device)->acquisition);
	SPCdata parameterCheck;
	SPC_get_parameters(0, &parameterCheck);
	if (OScDev_CHECK(err, set_measurement_params()))
		return err;

	acq->firstWrite = 1;
	OScDev_Log_Debug(device, "Waiting for user to start FLIM acquisition...");

	//adapted from init_fifo_measurement

	float curr_mode;
	unsigned short offset_value, *ptr;
	unsigned long photons_to_read, words_to_read, words_left;
	//char phot_fname[80];
	short state;
	short spcRet=0;
	// in most of the modules types with FIFO mode it is possible to stop the fifo measurement 
	//   after specified Collection time
	short fifo_stopt_possible = 1;
	short first_write = 1;
	short module_type = M_SPC150;
	short fifo_type; 
	short act_mod = 0;
	unsigned long fifo_size;
	unsigned long max_ph_to_read, max_words_in_buf, words_in_buf = 0 , current_cnt;
	unsigned short fpga_version;

	SPC_get_version(act_mod, &fpga_version);
	// before the measurement sequencer must be disabled
	SPC_enable_sequencer(act_mod, 0);
	// set correct measurement mode

	SPC_get_parameter(act_mod, MODE, &curr_mode);

	SPC_get_parameters(0, &parameterCheck);
	switch (module_type) {
	case M_SPC130:
		break;
		//these blocks hsould be upgraded to support other boards// TODO
	case M_SPC600:
	case M_SPC630:
		break;

	case M_SPC830:
		break;

	case M_SPC140:
		break;

	case M_SPC150:
		// ROUT_OUT in 150 == fifo
		if (curr_mode != ROUT_OUT &&  curr_mode != FIFO_32M) {
			SPC_set_parameter(act_mod, MODE, ROUT_OUT);
			curr_mode = ROUT_OUT;
		}
		fifo_size = 16 * 262144;  // 4194304 ( 4M ) 16-bit words
		if (curr_mode == ROUT_OUT)
			fifo_type = FIFO_150;
		else  // FIFO_IMG ,  marker 3 can be enabled via ROUTING_MODE
			fifo_type = FIFO_IMG;

		acq->fifo_type = fifo_type;
		break;

	}
	unsigned short rout_mode, scan_polarity;
	float fval;

	// ROUTING_MODE sets active markers and their polarity in Fifo mode ( not for FIFO32_M)
	// bits 8-11 - enable Markers0-3,  bits 12-15 - active edge of Markers0-3

	// SCAN_POLARITY sets markers polarity in FIFO32_M mode
	SPC_get_parameter(act_mod, SCAN_POLARITY, &fval);
	scan_polarity = fval;
	SPC_get_parameter(act_mod, ROUTING_MODE, &fval);
	rout_mode = fval;

	SPC_get_parameters(0, &parameterCheck);
	// use the same polarity of markers in Fifo_Img and Fifo mode
	rout_mode &= 0xfff8;
	rout_mode |= scan_polarity & 0x7;

	SPC_get_parameter(act_mod, MODE, &curr_mode);

	SPC_get_parameters(0, &parameterCheck);
	if (curr_mode == ROUT_OUT) {
		rout_mode |= 0xf00;     // markers 0-3 enabled
		SPC_set_parameter(act_mod, ROUTING_MODE, rout_mode);
	}
	if (curr_mode == FIFO_32M) {
		rout_mode |= 0x800;     // additionally enable marker 3
		SPC_set_parameter(act_mod, ROUTING_MODE, rout_mode);
		SPC_set_parameter(act_mod, SCAN_POLARITY, scan_polarity);
	}
	SPC_get_parameters(0, &parameterCheck);
	// switch off stop_on_overfl
	float collectionTime = (float)GetData(device)->acqTime;
	SPC_set_parameter(-1, COLLECT_TIME, collectionTime);//setting the collection time from the device property browser

	SPC_set_parameter(act_mod, STOP_ON_OVFL, 1);
	
	SPC_set_parameter(act_mod, STOP_ON_TIME, 1);
	
	if (fifo_stopt_possible) {

		SPC_set_parameter(act_mod, STOP_ON_TIME, 1);
	}

	SPC_get_parameters(0, &parameterCheck);//check the time 
	if (module_type == M_SPC830)
		max_ph_to_read = 2000000; // big fifo, fast DMA readout
	else
		//max_ph_to_read = 16384;
		max_ph_to_read = 200000;
	if (fifo_type == FIFO_48)
		max_words_in_buf = 3 * max_ph_to_read;
	else
		max_words_in_buf = 2 * max_ph_to_read;


	acq->buffer = (unsigned short *)malloc(max_words_in_buf * sizeof(unsigned short)); //memory allocation for FIFO data collection
	if (acq->buffer ==NULL)
		return 0;
	SPC_get_parameters(0, &parameterCheck);
	photons_to_read = 100000000;

	words_to_read = 2 * photons_to_read; //max photon in one acquisition cycle

	words_left = words_to_read;
	
	strcpy(acq->phot_fname, "BH_photons.spc");//name will later be collected from user //FLIMTODO

	int totalWord = 0;
	int loopcount = 0;
	int totalPhot = 0;
	int markerLine = 0;
	int markerFrame = 0;
	int flimPixelType = 0;
	int flimPhotonType = 0;

	int max_buff_reached = 0;

	unsigned long lineFrameMacroTime = 0;
	int countprev = 0;
	int linCount = 0;
	int pixCount = 0;
	int pixelTime = 150;//This is in terms of macro time
	int frameCount = 0;
	int maxMacroTime = 0;
	unsigned short PrevMacroTIme = 0;
	unsigned long totMacroTime = 0;

	SPC_get_parameters(0, &parameterCheck);
	//snprintf(msg, OScDev_MAX_STR_LEN, "Updated magnification is: %6.2f", *magnification);

	bool stopRequested;  // allow user to stop acquisition
	// do not start FLIM acquisition until user click 'StartFLIM'
	while (true)
	{
		EnterCriticalSection(&(acq->mutex));
		{	
			stopRequested = acq->stopRequested;
			acq->started = GetData(device)->flimStarted;
		}
		LeaveCriticalSection(&(acq->mutex));
		if (acq->started)
		{
			OScDev_Log_Debug(device, "user started FLIM acquisition");
			break;
		}
		if (stopRequested)
		{
			GetData(device)->flimStarted = false;  // reset to false for next run
			OScDev_Log_Debug(device, "User interruption...");
			break;
		}
		Sleep(100);
	}
	
	// TODO: not sure if the loop should directly exit here if stop is reuqested?
	spcRet = SPC_start_measurement(GetData(device)->moduleNr);
	char msg[OScDev_MAX_STR_LEN + 1];
	snprintf(msg, OScDev_MAX_STR_LEN, "return value after start measurement %d", spcRet);
	OScDev_Log_Debug(device, msg);

	// keep acquiring photon data until the set acquisition time is over
	GetData(device)->flimDone = false; 
	while (!spcRet) 
	{
		EnterCriticalSection(&(acq->mutex));
		stopRequested = acq->stopRequested;
		LeaveCriticalSection(&(acq->mutex));
		if (stopRequested)
		{
			OScDev_Log_Debug(device, "User interruption...Exiting FLIM acquisition loop...");
			GetData(device)->flimStarted = false;  // reset for next run
			break;
		}

		loopcount++;  // debug use only
		//char msg[OScDev_MAX_STR_LEN + 1];
		//snprintf(msg, OScDev_MAX_STR_LEN, "Current FLIM loop: %d", loopcount);
		//OScDev_Log_Debug(device, msg);

		// now test SPC state and read photons
		SPC_test_state(act_mod, &state);
		// user must provide safety way out from this loop 
		//    in case when trigger will not occur or required number of photons 
		//          cannot be reached
		if (state & SPC_WAIT_TRG) {   // wait for trigger                
			continue;
		}
		if (state != 192)//check for debugging, not needed (DELETE)
		{
			snprintf(msg, OScDev_MAX_STR_LEN, "inside while, state %d", state);
			OScDev_Log_Debug(device, msg);
		}

		if (words_left > max_words_in_buf - words_in_buf)
			// limit current_cnt to the free space in buffer
			current_cnt = max_words_in_buf - words_in_buf;
		else
			current_cnt = max_words_in_buf;//1*words_left; orginal code

		ptr = (unsigned short *)&(acq->buffer[words_in_buf]);

		SPCdata parameterCheck1;
		SPC_get_parameters(0, &parameterCheck1);


		if (state & SPC_ARMED) {  //  system armed   //continues to get data
			
			if (state & SPC_FEMPTY)
				continue;  // Fifo is empty - nothing to read

						   // before the call current_cnt contains required number of words to read from fifo
			spcRet = SPC_read_fifo(act_mod, &current_cnt, ptr);
			//printf("%d ",current_cnt);

			totalPhot += current_cnt;
			words_left -= current_cnt;
			if (words_left <= 0)
				break;   // required no of photons read already

			if (state & SPC_FOVFL) {

				OScDev_Log_Debug(device, "SPC Overload");
				break;
				//should I read the rest of the data? 
			}

			if ((state & SPC_COLTIM_OVER) | (state & SPC_TIME_OVER)) {//if overtime occured, that should be over
					  
																	  //there should be exit code here if time over by 10 seconds

				OScDev_Log_Debug(device, "FLIM Collection time over 1");
				break;

			}
			words_in_buf += current_cnt;
			if (words_in_buf == max_words_in_buf) {
				// your buffer is full, but photons are still needed 
				// save buffer contents in the file and continue reading photons
				max_buff_reached++;

				acq->words_in_buf = words_in_buf;
				spcRet= save_photons_in_file(acq);
				totalWord += words_in_buf;
				acq->words_in_buf=words_in_buf = 0;
				OScDev_Log_Debug(device, "Maximum buffer reached");
			}
		}
		else { //enters when SPC is not armed //NOT armed when measurement is NOT in progress
			if (fifo_stopt_possible && (state & SPC_TIME_OVER) != 0) {
				// measurement stopped after collection time
				// read rest photons from the fifo
				// before the call current_cnt contains required number of words to read from fifo
				spcRet = SPC_read_fifo(act_mod, &current_cnt, ptr);
				// after the call current_cnt contains number of words read from fifo  

				words_left -= current_cnt;
				words_in_buf += current_cnt; //should be reading until less than zero
				acq->words_in_buf += current_cnt;
				break;
			}
		}
		if ((state & SPC_COLTIM_OVER) | (state & SPC_TIME_OVER)) {//if overtime occured, that should be over
																  //there should be exit code here if time over by 10 seconds
				OScDev_Log_Debug(device, "FLIM Collection time over 2");
				break; 
		}
	}

	// SPC_stop_measurement should be called even if the measurement was stopped after collection time
	//           to set DLL internal variables
	OScDev_Log_Debug(device, "Finished FLIM");

	snprintf(msg, OScDev_MAX_STR_LEN, "total words acquired %d", totalWord);
	OScDev_Log_Debug(device, msg);

	SPC_stop_measurement(act_mod);
	SPC_stop_measurement(act_mod);
	totalWord += words_in_buf;
	if (words_in_buf > 0) {

		acq->words_in_buf = words_in_buf;
		spcRet = save_photons_in_file(acq);

	}
	//savign the photon(converted to exponential file)
	//this funtion should execute after the acquisition is over
	
	//BH_extractPhoton(device);

	if (acq->buffer) {
		free(acq->buffer);
	}

	//write the SDT file
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
	GetData(device)->flimStarted = false; // reset for next run
	LeaveCriticalSection(&(acq->mutex));
	CONDITION_VARIABLE *cv = &(acq->acquisitionFinishCondition);
	WakeAllConditionVariable(cv);
}

OScDev_Error set_measurement_params() {
	/*
	-the SPC parameters must be set(SPC_init or SPC_set_parameter(s)),
		-the SPC memory must be configured(SPC_configure_memory in normal modes),
		-the measured blocks in SPC memory must be filled(cleared) (SPC_fill_memory),
		-the measurement page must be set(SPC_set_page)

		*/
	SPCMemConfig m_spc_mem_config;

	int m_meas_page = 0;
	//reset the memory of LifeTime board
	SPC_configure_memory(MODULE, -1, 0, &m_spc_mem_config);
	short ret = SPC_fill_memory(MODULE, -1, -1, 0);
	if (ret != 0) {
		//TRACE("Failed at SPC_fill_memory()\n");
		//SPC_get_error_string(ret, err, 100);
		//TRACE("\n ERROR_MEMORY_FILL \n %s\n", err);
	}

	//ret = SPC_clear_rates(MODULE);

	//ret = SPC_fill_memory(MODULE, -1, m_meas_page, 0);

	ret= SPC_set_page(MODULE, m_meas_page);



	if (ret != 0) {
		//TRACE("ERROR: when clearing rates\n");
		return ret;
	}

	return OScDev_OK;
}


OScDev_Error save_photons_in_file(struct AcqPrivateData *acq) {
	
	long ret;
	int i;
	unsigned short first_frame[3], no_of_fifo_routing_bits;
	unsigned long lval;
	float fval;
	FILE *stream;
	unsigned header;
	//char phot_fname[80];
	
	strcpy(acq->phot_fname, "BH_photons.spc");//name will later be collected from user //FLIMTODO

	if (acq->firstWrite) {


		no_of_fifo_routing_bits = 3; // it means 8 routing channels - default value
									 //  set to 0 if router is not used

									 ///
		first_frame[2] = 0;

//		ret = SPC_get_fifo_init_vars(0, NULL, NULL, NULL, &spc_header);
		
		signed short ret = SPC_get_fifo_init_vars(0, NULL, NULL, NULL, &header);
		if (!ret) {
			first_frame[0] = (unsigned short)header;
			first_frame[1] = (unsigned short)(header >> 16);
		}
		else
			return -1;

		acq->firstWrite = 0;
		// write 1st frame to the file
		stream = fopen(acq->phot_fname, "wb");
		if (!stream)
			return -1;

		if (acq->fifo_type == FIFO_48)
			fwrite((void *)&first_frame[0], 2, 3, stream); // write 3 words ( 48 bits )
		else
			fwrite((void *)&first_frame[0], 2, 2, stream); // write 2 words ( 32 bits )
	}
	else {
		stream = fopen(acq->phot_fname, "ab");
		if (!stream)
			return -1;
		fseek(stream, 0, SEEK_END);     // set file pointer to the end
	}


	ret = fwrite((void *)acq->buffer, 1, 2 * acq->words_in_buf, stream); // write photons buffer
	fclose(stream);
	if (ret != 2 * acq->words_in_buf)
		return -1;     // error type in errno
		
	return OScDev_OK;

}


OScDev_Error SaveHistogramAndIntensityImage(void *param) 
{
	OScDev_Device *device = (OScDev_Device *)param;
	struct AcqPrivateData *acq = &(GetData(device)->acquisition);

	// Note: We cannot use SPC_save_data_to_sdtfile() cannot be used in FIFO Image mode

	int stream_type = BH_STREAM;
	int what_to_read = 1;   // valid photons
	if (acq->fifo_type == FIFO_IMG) {
		stream_type |= MARK_STREAM;
		what_to_read |= (0x4 | 0x8 | 0x10);   // also pixel, line, frame markers possible
	}

	acq->streamHandle = SPC_init_phot_stream(acq->fifo_type, acq->phot_fname, 1, stream_type, what_to_read);

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
	unsigned setupLength = strlen(setupString);

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
		SPC_get_phot_stream_info(acq->streamHandle, &unusedStreamInfo);

		unsigned frameIndex = 0;
		unsigned lineIndex = 0;
		bool firstLineMarkOfFrameSeen = false;

		uint64_t lineMarkMacroTime; // Units depend on SPC setting and model

		while (!ret) { // untill error (for example end of file)
			PhotInfo64 phot_info;
			ret = SPC_get_photon(acq->streamHandle, (PhotInfo *)&phot_info);

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

		// TODO Not needed?
		// Read stream info to skip
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

	ret = fwrite(&fileHeader, sizeof(bhfile_header), 1, sdtFile);  //Write Header Block
	ret = fwrite(fileInfoString, fileInfoLength, 1, sdtFile);  // Write File Info Block
	ret = fwrite(setupString, setupLength, 1, sdtFile);  // Write Setup Block
	ret = fwrite(&meas_desc, sizeof(MeasureInfo), 1, sdtFile);  //Write Measurement Description Block
	ret = fwrite(&block_header, sizeof(BHFileBlockHeader), 1, sdtFile);  //Write Data Block Header
	ret = fwrite(histogramImage, sizeof(short), histogramImageSizeBytes / sizeof(short), sdtFile);

	fclose(sdtFile);

	free(intensityImage);
	free(histogramImage);

	OScDev_Log_Debug(device, "Finished writing SDT file");

	GetData(device)->flimDone = true;

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
			if (privAcq->started)
				return OScDev_Error_Acquisition_Running;
			else
				return OScDev_OK;
		}
		privAcq->stopRequested = false;
		privAcq->isRunning = true;
		privAcq->acquisition = acq;
		privAcq->started = GetData(device)->flimStarted;  // only true if user set flimStarted to True
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