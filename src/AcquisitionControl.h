#pragma once

#include <OpenScanDeviceLib.h>

#ifdef __cplusplus
extern "C" {
#endif

OScDev_RichError *InitializeDeviceForAcquisition(OScDev_Device *device);
void ShutdownAcquisitionState(OScDev_Device *device);
OScDev_RichError *StartAcquisition(OScDev_Device *device,
                                   OScDev_Acquisition *acq);
void StopAcquisition(OScDev_Device *device);
bool IsAcquisitionRunning(OScDev_Device *device);
void WaitForAcquisitionToFinish(OScDev_Device *device);

#ifdef __cplusplus
} // extern "C"
#endif
