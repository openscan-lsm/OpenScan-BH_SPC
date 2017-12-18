#include "BH_SPC150Private.h"
#include "OpenScanLibPrivate.h"
#include "OpenScanDeviceImpl.h"


static OSc_Error GetResolution(OSc_Setting *setting, int32_t *value)
{
	SPCdata data;
	short spcRet = SPC_get_parameters(GetData(setting->device)->moduleNr, &data);
	if (spcRet)
		return OSc_Error_Unknown;

	if (data.scan_size_x != data.scan_size_y)
		return OSc_Error_Unknown;

	*value = data.scan_size_x;
	return OSc_Error_OK;
}


static OSc_Error SetResolution(OSc_Setting *setting, int32_t value)
{
	float fValue = (float)value;
	short spcRet = SPC_set_parameter(GetData(setting->device)->moduleNr,
		SCAN_SIZE_X, fValue);
	if (spcRet)
		return OSc_Error_Unknown;
	spcRet = SPC_set_parameter(GetData(setting->device)->moduleNr,
		SCAN_SIZE_Y, fValue);
	if (spcRet)
		return OSc_Error_Unknown;
	return OSc_Error_OK;
}


static OSc_Error GetResolutionValues(OSc_Setting *setting, int32_t **values, size_t *count)
{
	static int32_t v[] = {
		256,
		512,
		1024,
		2048,
	};
	*values = v;
	*count = sizeof(v) / sizeof(int32_t);
	return OSc_Error_OK;
}


static struct OSc_Setting_Impl SettingImpl_Resolution = {
	.GetInt32 = GetResolution,
	.SetInt32 = SetResolution,
	.GetNumericConstraintType = OSc_Setting_NumericConstraintDiscreteValues,
	.GetInt32DiscreteValues = GetResolutionValues,
};


OSc_Error BH_SPC150PrepareSettings(OSc_Device *device)
{
	if (GetData(device)->settings)
		return OSc_Error_OK;

	OSc_Setting *resolution;
	OSc_Return_If_Error(OSc_Setting_Create(&resolution, device, "Resolution", OSc_Value_Type_Int32,
		&SettingImpl_Resolution, NULL));

	OSc_Setting *ss[] = {
		resolution,
	};
	size_t nSettings = sizeof(ss) / sizeof(OSc_Setting *);
	OSc_Setting **settings = malloc(sizeof(ss));
	memcpy(settings, ss, sizeof(ss));

	GetData(device)->settings = settings;
	GetData(device)->settingCount = nSettings;
	return OSc_Error_OK;
}