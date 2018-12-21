#pragma once

#include "OpenScanDeviceImpl.h"


struct OSc_Device_Impl BH_TCSCP150_Device_Impl;

unsigned short compute_checksum(void* hdr);
OSc_Error save_photons_in_file(struct AcqPrivateData *acq);
OSc_Error BH_LTDataSave(void *param);
OSc_Error set_measurement_params();
void BH_FinishAcquisition(OSc_Device *device);
OSc_Error BH_WaitForAcquisitionToFinish(OSc_Device *device);