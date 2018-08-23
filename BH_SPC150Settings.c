#include "BH_SPC150Private.h"
#include "OpenScanLibPrivate.h"
#include "OpenScanDeviceImpl.h"


OSc_Error BH_SPC150PrepareSettings(OSc_Device *device)
{
	if (GetData(device)->settings)
		return OSc_Error_OK;

	// ...

	OSc_Setting *ss[] = {
		NULL, // ...
	};  // OSc_Device_Get_Settings() returns count = 1 when this has NULL inside
	size_t nSettings = sizeof(ss) / sizeof(OSc_Setting *);
	if (*ss == NULL)
		nSettings = 0;
	OSc_Setting **settings = malloc(sizeof(ss));
	memcpy(settings, ss, sizeof(ss));

	GetData(device)->settings = settings;
	GetData(device)->settingCount = nSettings;
	return OSc_Error_OK;
}