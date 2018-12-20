#include "BH_SPC150.h"
#include "BH_SPC150Private.h"

#include <stdio.h>
#pragma once
#pragma pack(1)

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


static void PopulateDefaultParameters(struct BH_PrivateData *data)
{
	data->settingsChanged = true;
	data->acqTime = 20;
	data->flimStarted = false;
	data->flimDone = false;
	data->acquisition.cfd_value = 1.0;

	strcpy(data->flimFileName, "default-BH-FLIM-data");

	//// TODO
	//InitializeCriticalSection(&(data->acquisition.mutex));
	//data->acquisition.thread = NULL;
	//InitializeConditionVariable(&(data->acquisition.acquisitionFinishCondition));
	//data->acquisition.running = false;
	//data->acquisition.armed = false;
	//data->acquisition.started = false;
	//data->acquisition.stopRequested = false;
	//data->acquisition.acquisition = NULL;
}


static OSc_Error EnumerateInstances(OSc_Device ***devices, size_t *count)
{
	short spcErr;
	short spcRet;
	int active_board[1];
	SPCModInfo m_ModInfo;
	//short status=SPC_get_init_status();
	int  a = 0;
	//spcErr = SPC_close();  // close SPC150 if it remains open from previous session
	spcErr = SPC_init("cspcm.ini");
	if (spcErr < 0)
	{
		char msg[OSc_MAX_STR_LEN + 1] = "Cannot initialize BH SPC150 using: ";
		strcat(msg, "spcm.ini");
		OSc_Log_Error(NULL, msg);
		return OSc_Error_SPC150_CANNOT_OPEN_FILE;
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
	// For now, support just one board

	struct BH_PrivateData *data = calloc(1, sizeof(struct BH_PrivateData));
	data->moduleNr = 0; // TODO for multiple modules
	InitializeCriticalSection(&(data->acquisition.mutex));
	InitializeConditionVariable(&(data->acquisition.acquisitionFinishCondition));

	OSc_Device *device;
	OSc_Error err;
	if (OSc_Check_Error(err, OSc_Device_Create(&device, &BH_TCSCP150_Device_Impl, data)))
	{
		char msg[OSc_MAX_STR_LEN + 1] = "Failed to create device for BH SPC150";
		OSc_Log_Error(NULL, msg);
		return err;
	}

	PopulateDefaultParameters(GetData(device));

	*devices = malloc(sizeof(OSc_Device *));
	*count = 1;
	(*devices)[0] = device;

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
		return OSc_Error_SPC150_MODULE_NOT_ACTIVE;
	}

	return OSc_Error_OK;
}


static OSc_Error BH_Close(OSc_Device *device)
{
	struct AcqPrivateData *acq = &GetData(device)->acquisition;
	EnterCriticalSection(&acq->mutex);
	acq->stopRequested = true;
	while (acq->isRunning)
		SleepConditionVariableCS(&acq->acquisitionFinishCondition, &acq->mutex, INFINITE);
	LeaveCriticalSection(&acq->mutex);

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


static OSc_Error BH_GetAllowedResolutions(OSc_Device *device, size_t **widths, size_t **heights, size_t *count)
{
	static size_t resolutions[] = { 256, 512, 1024, 2048 };
	*widths = *heights = resolutions;
	*count = sizeof(resolutions) / sizeof(size_t);
	return OSc_Error_OK;
}


static OSc_Error BH_GetResolution(OSc_Device *device, size_t *width, size_t *height)
{
	SPCdata data;
	short spcRet = SPC_get_parameters(GetData(device)->moduleNr, &data);
	if (spcRet)
		return OSc_Error_Unknown;

	//*width = data.scan_size_x;
	//*height = data.scan_size_y;

	*height = 256;
	*width = 256;
	return OSc_Error_OK;
}


static OSc_Error BH_SetResolution(OSc_Device *device, size_t width, size_t height)
{
	short spcRet = SPC_set_parameter(GetData(device)->moduleNr,
		SCAN_SIZE_X, (float)width);
	if (spcRet)
		return OSc_Error_Unknown;
	spcRet = SPC_set_parameter(GetData(device)->moduleNr,
		SCAN_SIZE_Y, (float)height);
	if (spcRet)
		return OSc_Error_Unknown;
	return OSc_Error_OK;
}


static OSc_Error BH_GetImageSize(OSc_Device *device, uint32_t *width, uint32_t *height)
{
	// Currently all image sizes match the current resolution
	size_t w, h;
	OSc_Error err = BH_GetResolution(device, &w, &h);
	if (err != OSc_Error_OK)
		return err;
	*width = (uint32_t)w;
	*height = (uint32_t)h;
	return err;
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


// monitoring critical FLIM parameters
// such as CFD, Sync, etc.
// supposed to be running all the time
static DWORD WINAPI BH_Monitor_Loop(void *param)
{
	OSc_Device *device = (OSc_Device *)param;
	struct AcqPrivateData *acq = &(GetData(device)->acquisition);
	
	while (!acq->stopRequested)
	{
		acq->cfd_value = 12345;  // TODO: read from SPC150 board
		Sleep(100);
	}

	return 0;
}


// Current main loop for FLIM acquisition
static DWORD WINAPI BH_FIFO_Loop(void *param)
{
	OSc_Return_If_Error(set_measurement_params());

	OSc_Device *device = (OSc_Device *)param;
	struct AcqPrivateData *acq = &(GetData(device)->acquisition);
	SPCdata parameterCheck;
	SPC_get_parameters(0, &parameterCheck);
	OSc_Return_If_Error(set_measurement_params());

	acq->firstWrite = 1;
	OSc_Log_Debug(device, "Started FLIM");

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

	//for (int i = 0; i < 512; i++) {
	//	for (int j = 0; j < 512; j++) {
	//		LTmatrix[i][j] = 0;
	//	}
	//}
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
	//snprintf(msg, OSc_MAX_STR_LEN, "Updated magnification is: %6.2f", *magnification);

	// do not start FLIM acquisition until user click 'StartFLIM'
	while (!GetData(device)->flimStarted)
		Sleep(100);

	spcRet = SPC_start_measurement(GetData(device)->moduleNr);
	char msg[OSc_MAX_STR_LEN + 1];
	snprintf(msg, OSc_MAX_STR_LEN, "return value after start measurement %d", spcRet);

	OSc_Log_Debug(device, msg);

	// keep acquiring photon data until the set acquisition time is over
	GetData(device)->flimDone = false; 
	while (!spcRet) {
		loopcount++;
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
			snprintf(msg, OSc_MAX_STR_LEN, "inside while, state %d", state);
			OSc_Log_Debug(device, msg);
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

				OSc_Log_Debug(device, "SPC Overload");
				break;
				//should I read the rest of the data? 
			}

			if ((state & SPC_COLTIM_OVER) | (state & SPC_TIME_OVER)) {//if overtime occured, that should be over
					  
																	  //there should be exit code here if time over by 10 seconds

				OSc_Log_Debug(device, "FLIM Collection time over 1");
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
				OSc_Log_Debug(device, "Maximum buffer reached");
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
				OSc_Log_Debug(device, "FLIM Collection time over 2");
				break; 
		}
	}

	// SPC_stop_measurement should be called even if the measurement was stopped after collection time
	//           to set DLL internal variables
	OSc_Log_Debug(device, "Finished FLIM");

	snprintf(msg, OSc_MAX_STR_LEN, "total words acquired %d", totalWord);
	OSc_Log_Debug(device, msg);

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

	OSc_Error err;
	//write the SDT file
	if (OSc_Check_Error(err, BH_LTDataSave(device)))
	{
		OSc_Log_Error(device, "Error writing SDT file");
		return 0;
	}

	EnterCriticalSection(&(acq->mutex));
	acq->isRunning = false;
	acq->stopRequested = true;
	LeaveCriticalSection(&(acq->mutex));
	WakeAllConditionVariable(&(acq->acquisitionFinishCondition));

	return 0;
}


OSc_Error set_measurement_params() {
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

	return OSc_Error_OK;
}


OSc_Error save_photons_in_file(struct AcqPrivateData *acq) {
	
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
		
	return OSc_Error_OK;

}


// write SDT file
OSc_Error BH_LTDataSave(void *param) 
{
	OSc_Device *device = (OSc_Device *)param;
	struct AcqPrivateData *acq = &(GetData(device)->acquisition);
	///uint32_t collectionTime = GetData(device)->acqTime; //get collection time
	//SPC_save_data_to_sdtfile(-1, acq->buffer, 50000, "testSDT.sdt"); // cannot run on mode=5

	char file_info[512];
	short setup_length;
	char setup[32];
	bhfile_header header;
	MeasureInfo meas_desc;
	BHFileBlockHeader block_header;
	unsigned int iPhotonCountBufferSize;
	short *iPhotonCountBuffer;//should be void*, but works in wiscScan
	PhotStreamInfo stream_info;
	PhotInfo   phot_info;

	//for live display or intensity image
	//unsigned short  LTmatrix[512][512];//why is this line creating problem //issue


	int flagFreeBuff = 1;//1- empty
	int stream_type = BH_STREAM;

	int what_to_read = 1;   // valid photons
	if (acq->fifo_type == FIFO_IMG) {
		stream_type |= MARK_STREAM;
		what_to_read |= (0x4 | 0x8 | 0x10);   // also pixel, line, frame markers possible
	}


	
	int ret = 0;
	unsigned int loc = 0;

	for (short i = 0; i < 512; i++) {
		for (short j = 0; j < 512; j++) {
			//LTmatrix[i][j] = 0; //issue
		}
	}
	//"testExternalPxl_5us.spc"
	acq->streamHandle = SPC_init_phot_stream(acq->fifo_type, acq->phot_fname, 1, stream_type, what_to_read);
	//acq->streamHandle = SPC_init_phot_stream(acq->fifo_type, "testExternalPxl_5us.spc", 1, stream_type, what_to_read);

	SPCdata parameters;
	SPC_get_parameters(MODULE, &parameters);//



	int FLIM_ADCResolution = 8;

	///size change start///////
	int factorforSize =2;


	int sizeinPixel = 512 / factorforSize;
	int pixelsPerLine = sizeinPixel;
	int linesPerFrame = sizeinPixel;
	int pixlimit = 327;//327 originally
	int borderLimit = pixlimit / factorforSize;//327 originally
	int startExcludePixel = 2;
	
	//working code ends

	///end test block
	SYSTEMTIME st;
	GetSystemTime(&st);

	char date[11];
	sprintf_s(date, 11, "%02d:%02d:%04d", st.wMonth, st.wDay, st.wYear);
	char time[9];
	sprintf_s(time, 9, "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
	int file_info_length = sprintf_s(file_info, 512, "*IDENTIFICATION\r\nID : SPC Setup & Data File\r\nTitle : sagartest\r\nVersion : 1  781 M\r\nRevision : %d bits ADC\r\nDate : %s\r\nTime : %s\r\n*END\r\n\r\n", FLIM_ADCResolution, date, time);

	// Create Setup Block
	//char setup[32];
	setup_length = sprintf_s(setup, 32, "*SETUP\r\n*END\r\n\r\n");
	//TODO might have to add more here to comply with new file format

	// Create Header Block

	short moduleType = SPC_test_id(MODULE);
	header.revision = (0x28 << 4)  ;

	
	header.info_offs = sizeof(sdt_file_header);
	header.info_length = file_info_length;
	header.setup_offs = header.info_offs + header.info_length;
	header.setup_length = setup_length;
	header.meas_desc_block_offs = header.setup_offs + header.setup_length;
	header.meas_desc_block_length = sizeof(MeasureInfo);
	header.no_of_meas_desc_blocks = 1;
	header.data_block_offs = header.meas_desc_block_offs + header.meas_desc_block_length * header.no_of_meas_desc_blocks;

	//header.data_block_length = SP->pixelsPerLine * SP->linesPerFrame * (1 << SP->FLIM_ADCResolution) * sizeof(short);  //*MJF 7/25/13 'numChannels' -> 'SP->nChannels'
	header.data_block_length = pixelsPerLine * linesPerFrame * (1 << FLIM_ADCResolution) * sizeof(short);  
	header.no_of_data_blocks = 1;
	header.header_valid = BH_HEADER_VALID;
	header.reserved1 = header.no_of_data_blocks;
	header.reserved2 = 0;
	header.chksum = compute_checksum(&header);


	// Create Measurement Description Block
	//MeasureInfo meas_desc;
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
	meas_desc.adc_re = 1 << (parameters.adc_resolution - 4); //goal is to make it 8 //1 << parameters.adc_resolution; //should be default
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
	//BHFileBlockHeader block_header;
	block_header.lblock_no = 1;
	block_header.data_offs = header.data_block_offs + sizeof(BHFileBlockHeader);
	block_header.next_block_offs = block_header.data_offs + header.data_block_length;

	block_header.block_type = MEAS_DATA_FROM_FILE | PAGE_BLOCK;//this one works for our case
																   //block_header.block_type = 1;
	block_header.meas_desc_block_no = 0;
	block_header.lblock_no = ((MODULE & 3) << 24);
	block_header.block_length = header.data_block_length;

	iPhotonCountBufferSize = pixelsPerLine * linesPerFrame * (1 << FLIM_ADCResolution) * sizeof(short);//this size should be dynamically allocated in the future
																									   //short *iPhotonCountBuffer;//should be void*
	iPhotonCountBuffer = (short*)malloc(iPhotonCountBufferSize);
	if (iPhotonCountBuffer == NULL) {
		return OSc_Error_SPC150_Buffer_Empty;
	}

	flagFreeBuff = 0;//0- indicates buffer is full//1 indicates empty
	memset(iPhotonCountBuffer, 0, iPhotonCountBufferSize);

	 
	if (acq->streamHandle >= 0) {
		// 2. in every moment of extracting current stream state can be checked
		SPC_get_phot_stream_info(acq->streamHandle, &stream_info);

		ret = 0;

		int lineCount = 0;
		int frameCount = 0;
		unsigned int max_microtime = 0;
		unsigned int max_macroimeLow = 0;
		unsigned int max_macrotimehi = 0;
		int histogram[256];//zero init TODO
		unsigned int prevvalue = 0;
		unsigned long prevvalueFrameMacro = 0;
		unsigned long prevvalueLineMacro = 0;
		int diffCOuntLine = 0;
		int difffcountFrame = 0;

		int countprev = 0;
		float linCount = 0;
		int pixCount = 0;
		int pixelTime = 340;//This is in terms of macro time
		int prevLinChk = 0;
		unsigned long lineFrameMacroTime;
		int flagWrongFrame = 0;

		int missedphoton = 0;
		int countedphoton = 0;
		int NonZeroCount = 0;
		int ZeroCount = 0;


		while (!ret) {  // untill error ( for example end of file )
						// user must provide safety way out from this loop 
						// fill phot_info structure with subsequent photons information
			ret = SPC_get_photon(acq->streamHandle, &phot_info);
			// save it somewhere

			int tempLin = 0;//tempLin is temp line position



			if (phot_info.flags & F_MARK) {
				frameCount++;
				linCount = 0;

			}

			if (phot_info.flags & L_MARK) {

				lineFrameMacroTime = phot_info.mtime_lo;
				linCount++;

			}

			if (frameCount<2) continue;
			float linNoTemp = linCount;// / ((float)factorforSize);
			tempLin = (int)(linNoTemp>(pixelsPerLine - 1) ? (pixelsPerLine - 1) : linNoTemp);  //check limit

																							   //the following line should be upgraded to 64bit code in case this does not work
			unsigned long relativeMacroTime = phot_info.mtime_lo - lineFrameMacroTime;//relative macro time compared to start of line

			float tempPix = (float)relativeMacroTime / ((float)pixelTime);//location in one line, given the pixel time


			//tempPix = tempPix / ((float)factorforSize);//resolution change

			if (tempPix > borderLimit | tempPix < startExcludePixel) {
				missedphoton++;
				continue;
			}

			//if (tempPix>borderLimit | tempPix<startExcludePixel) continue;
			//int locPix=(int)(tempPix>borderLimit?borderLimit:tempPix);//location in a row
			float ratio = (float)pixelsPerLine / ((float)(borderLimit - startExcludePixel));
			int locPix = (int)((tempPix - (float)startExcludePixel)*ratio);

			//LTmatrix[tempLin][locPix]++;//intensity photon counting image //issue

										//microtime calculcation/// no change needed for resolution change
			float tempLoc = (float)phot_info.micro_time * 256 / 4000;

			if (tempLoc == 0) {//better way to handle this, these are photons with no time lag from sync signal
				//ZeroCount++;
				continue;
			}

			int loc = (int)tempLoc;


			histogram[loc]++;
			///adding histogram to the buffer

			iPhotonCountBuffer[(tempLin*pixelsPerLine*(1 << 8)) + ((locPix)*(1 << 8)) + loc]++;//crashes for locpix=-1[fixed];//this number 8 reperents 2^8 time bins, it should be 10 for 1024 level time bins 
																							   //building  histogram
		}
		SPC_get_phot_stream_info(acq->streamHandle, &stream_info);
		// - at the end close the opened stream
		SPC_close_phot_stream(acq->streamHandle);

		int max = -10;
		for (short i = 0; i < 512; i++) {
			for (short j = 0; j < 512; j++) {
				//if (LTmatrix[i][j]>max) {
					//LTmatrix[i][j] = LTmatrix[i][j] / 4; //4 divided for normalization, will not be needed for OpenFLIM//issue
				//}
			}
		}
	}

	sdt_file_header dest_header;
	//CFile dest_file;
	data_block_header dest_dbh;
	SYSTEMTIME now;

	//default file
	char dest_filename[500];
//	sprintf_s(dest_filename, 500, "BH_data.sdt");//it was *.bin

//	sprintf_s(dest_filename, 500, "trialCat");//it was *.bin

	sprintf_s(dest_filename, 500, GetData(device)->flimFileName);//it was *.bin
	strcat(dest_filename, ".sdt");

	//GetData(device)->flimFileName;


	FILE* headerFile;

	fopen_s(&headerFile, dest_filename, "wb");
	if (headerFile == NULL) return 0;

	//int ret;
	ret = fwrite(&header, sizeof(bhfile_header), 1, headerFile);  //Write Header Block
	ret = fwrite(file_info, file_info_length, 1, headerFile);  // Write File Info Block
	ret = fwrite(setup, setup_length, 1, headerFile);  // Write Setup Block
	ret = fwrite(&meas_desc, sizeof(MeasureInfo), 1, headerFile);  //Write Measurement Description Block
	ret = fwrite(&block_header, sizeof(data_block_header), 1, headerFile);  //Write Data Block Header
	ret = fwrite(iPhotonCountBuffer, sizeof(short), iPhotonCountBufferSize / sizeof(short), headerFile);

	fclose(headerFile);

	//OSc_Log_Debug(device, "User interruption for FooLoop...");

	/* addition ends */
	free(iPhotonCountBuffer);
	flagFreeBuff = 1;//emptied//needed when save button is implemented

    // indicate the status of FLIM acquisition
	GetData(device)->flimDone = true;

	return OSc_Error_OK;
}


// plan to use to extract intensity image from spc data
// not used in current version
OSc_Error BH_extractPhoton(void *param) {
	//int a = 1;
	OSc_Device *device = (OSc_Device *)param;
	struct AcqPrivateData *acq = &(GetData(device)->acquisition);


	PhotInfo   phot_info;
	PhotStreamInfo stream_info;
	

	unsigned short LTmatrix[512][512];
	////////THE FOLLOWING PORTION IS PHOTON EXTRACTION AFTER SAVING SPC FILE. YOU CAN DO IT RUNTIME WITH BUFFERED PHOTON STREAM////
	int stream_type = BH_STREAM;
	int what_to_read = 1;   // valid photons
	if (acq->fifo_type == FIFO_IMG) {
		stream_type |= MARK_STREAM;
		what_to_read |= (0x4 | 0x8 | 0x10);   // also pixel, line, frame markers possible
	}

	// there is new alternative method:
	//  call SPC_get_fifo_init_vars function to get:
	//      - values needed to init photons stream  
	//      -  .spc file header

	//ret = SPC_get_fifo_init_vars ( act_mod,  &fifo_type, &stream_type, NULL, NULL);

	int ret = 0;
	unsigned int loc = 0;

	for (int i = 0; i < 512; i++) {
		for (int j = 0; j < 512; j++) {
			LTmatrix[i][j] = 0;
		}
	}

	acq->streamHandle = SPC_init_phot_stream(acq->fifo_type, acq->phot_fname, 1, stream_type, what_to_read);
	if (acq->streamHandle >= 0) {
		// 2. in every moment of extracting current stream state can be checked
		SPC_get_phot_stream_info(acq->streamHandle, &stream_info);

		ret = 0;
		int count = 0;
		int lineCount = 0;
		int frameCount = 0;
		unsigned int max_microtime = 0;
		unsigned int max_macroimeLow = 0;
		unsigned int max_macrotimehi = 0;
		int histogram[256];//zero init TODO
		unsigned int prevvalue = 0;
		unsigned long prevvalueFrameMacro = 0;
		unsigned long prevvalueLineMacro = 0;
		int diffCOuntLine = 0;
		int difffcountFrame = 0;

		int countprev = 0;
		int linCount = 0;
		int pixCount = 0;
		int pixelTime = 150;//This is in terms of macro time

		unsigned long lineFrameMacroTime;
		while (!ret) {  // untill error ( for example end of file )
						// user must provide safety way out from this loop 
						// fill phot_info structure with subsequent photons information
			ret = SPC_get_photon(acq->streamHandle, &phot_info);
			// save it somewhere

			if (phot_info.flags == L_MARK) {
				lineCount++;
				unsigned int diff = phot_info.mtime_lo - prevvalueLineMacro;
				prevvalueLineMacro = phot_info.mtime_lo;
				if (diff<75000 | diff>78000) {
					diffCOuntLine++;
				}
				lineFrameMacroTime = phot_info.mtime_lo;
				linCount++;
				linCount = (int)(linCount>511 ? 511 : linCount);
			}
			if (phot_info.flags == F_MARK) {
				frameCount++;
				unsigned int diff = phot_info.mtime_lo - prevvalueFrameMacro;
				prevvalueFrameMacro = phot_info.mtime_lo;
				if (!(diff>39300000 | diff<39400000)) {
					difffcountFrame++;
				}
				//reset line, pixel clock
				linCount = 0;


			}

			unsigned long relativeMacroTime = phot_info.mtime_lo - lineFrameMacroTime;//relative macro time compared to beginiing of line

			float tempPix = (float)relativeMacroTime / pixelTime;//location in one line, given the pixel time
			int loc = (int)(tempPix>511 ? 511 : tempPix);
			LTmatrix[linCount][loc]++; 

			float tempLoc = (float)phot_info.micro_time * 256 / 4000;
			loc = (int)tempLoc;
			histogram[loc]++;

			//building  histogram
			count++;
		}
		SPC_get_phot_stream_info(acq->streamHandle, &stream_info);
		// - at the end close the opened stream
		SPC_close_phot_stream(acq->streamHandle);

	}

	OSc_Error err;
	if (OSc_Check_Error(err, BH_LTDataSave(acq)))
		return err;

	return OSc_Error_OK;

}



static OSc_Error BH_ArmDetector(OSc_Device *device, OSc_Acquisition *acq)
{
	struct AcqPrivateData *privAcq = &(GetData(device)->acquisition);
	EnterCriticalSection(&(privAcq->mutex));
	{
		if (privAcq->isRunning)
		{
			LeaveCriticalSection(&(privAcq->mutex));
			return OSc_Error_Acquisition_Running;
		}
		privAcq->stopRequested = false;
		privAcq->isRunning = true;
	}
	LeaveCriticalSection(&(privAcq->mutex));

	return OSc_Error_OK;
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


static OSc_Error BH_StartDetector(OSc_Device *device, OSc_Acquisition *acq)
{
	struct AcqPrivateData *privAcq = &(GetData(device)->acquisition);
	short moduleNr = GetData(device)->moduleNr;
	short state;
	SPC_test_state(moduleNr, &state);

	DWORD id;

	// read SPC150 parameters such as CFD, Sync, etc in a separate thread
	privAcq->monitorThread = CreateThread(NULL, 0, BH_Monitor_Loop, device, 0, &id);
	
	// FLIm acquisition thread
	privAcq->thread = CreateThread(NULL, 0, BH_FIFO_Loop, device, 0, &id);

	//test insert
	EnterCriticalSection(&(privAcq->mutex));
	privAcq->isRunning = true;
	privAcq->stopRequested = false;
	LeaveCriticalSection(&(privAcq->mutex));
	WakeAllConditionVariable(&(privAcq->acquisitionFinishCondition));

	return OSc_Error_Unsupported_Operation;
}


static OSc_Error BH_StopDetector(OSc_Device *device, OSc_Acquisition *acq)
{
	EnterCriticalSection(&(GetData(device)->acquisition.mutex));
	GetData(device)->acquisition.stopRequested = true;
	LeaveCriticalSection(&(GetData(device)->acquisition.mutex));
	return OSc_Error_OK;
}


static OSc_Error BH_IsRunning(OSc_Device *device, bool *isRunning)
{
	EnterCriticalSection(&(GetData(device)->acquisition.mutex));
	*isRunning = GetData(device)->acquisition.isRunning;
	LeaveCriticalSection(&(GetData(device)->acquisition.mutex));
	return OSc_Error_OK;
}


static OSc_Error BH_Wait(OSc_Device *device)
{
	struct AcqPrivateData *acq = &GetData(device)->acquisition;
	OSc_Error err = OSc_Error_OK;

	EnterCriticalSection(&acq->mutex);
	while (acq->isRunning)
		SleepConditionVariableCS(&acq->acquisitionFinishCondition, &acq->mutex, INFINITE);
	LeaveCriticalSection(&acq->mutex);
	return err;
}


struct OSc_Device_Impl BH_TCSCP150_Device_Impl = {
	.GetModelName = BH_GetModelName,
	.GetInstances = BH_GetInstances,
	.ReleaseInstance = BH_ReleaseInstance,
	.GetName = BH_GetName,
	.Open = BH_Open,
	.Close = BH_Close,
	.HasScanner = BH_HasScanner,
	.HasDetector = BH_HasDetector,
	.GetSettings = BH_GetSettings,
	.GetAllowedResolutions = BH_GetAllowedResolutions,
	.GetResolution = BH_GetResolution,
	.SetResolution = BH_SetResolution,
	.GetImageSize = BH_GetImageSize,
	.GetNumberOfChannels = BH_GetNumberOfChannels,
	.GetBytesPerSample = BH_GetBytesPerSample,
	.ArmDetector = BH_ArmDetector,
	.StartDetector = BH_StartDetector,
	.StopDetector = BH_StopDetector,
	.IsRunning = BH_IsRunning,
	.Wait = BH_Wait,
};