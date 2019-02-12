#pragma once

#include "BH_SPC150Private.h"


struct OScDev_DeviceImpl BH_TCSCP150_Device_Impl;

unsigned short compute_checksum(void* hdr);
OScDev_Error save_photons_in_file(struct AcqPrivateData *acq);
OScDev_Error BH_LTDataSave(void *param);
OScDev_Error set_measurement_params();
void BH_FinishAcquisition(OScDev_Device *device);
OScDev_Error BH_WaitForAcquisitionToFinish(OScDev_Device *device);