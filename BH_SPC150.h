#pragma once

#include "BH_SPC150Private.h"


// TODO This shouldn't be in header
OScDev_DeviceImpl BH_TCSCP150_Device_Impl;

// TODO These functions should be static and prototypes can be in the source file
unsigned short compute_checksum(void* hdr);
OScDev_Error save_photons_in_file(struct AcqPrivateData *acq);
OScDev_Error SaveHistogramAndIntensityImage(void *param);
OScDev_Error set_measurement_params();
void BH_FinishAcquisition(OScDev_Device *device);
OScDev_Error BH_WaitForAcquisitionToFinish(OScDev_Device *device);