#include "BH_SPC150Private.h"
#include "OpenScanLibPrivate.h"
#include "OpenScanDeviceImpl.h"

static OSc_Error GetFileName(OSc_Setting *setting, char *value)
 {
	strcpy(value, GetData(setting->device)->flimFileName);
	return OSc_Error_OK;
	}

static OSc_Error SetFileName(OSc_Setting *setting, const char *value)
 {
	strcpy(GetData(setting->device)->flimFileName, value);
	return OSc_Error_OK;
	}

static struct OSc_Setting_Impl SettingImpl_FileName = {
	.GetString = GetFileName,
	.SetString = SetFileName,
};

static OSc_Error GetFLIMStarted(OSc_Setting *setting, bool *value)
{
	*value = GetData(setting->device)->flimStarted;
	return OSc_Error_OK;
}


static OSc_Error SetFLIMStarted(OSc_Setting *setting, bool value)
{
	GetData(setting->device)->flimStarted = value;
	GetData(setting->device)->settingsChanged = true;
	return OSc_Error_OK;
}


static struct OSc_Setting_Impl SettingImpl_FLIMStarted = {
	.GetBool = GetFLIMStarted,
	.SetBool = SetFLIMStarted,
};


static OSc_Error GetAcqTime(OSc_Setting *setting, int32_t *value)
{
	*value = GetData(setting->device)->acqTime;
	return OSc_Error_OK;
}


static OSc_Error SetAcqTime(OSc_Setting *setting, int32_t value)
{
	GetData(setting->device)->acqTime = value;
	GetData(setting->device)->settingsChanged = true;
	return OSc_Error_OK;
}


static OSc_Error GetAcqTimeRange(OSc_Setting *setting, int32_t *min, int32_t *max)
{
	*min = 1;
	*max = 3600;  // 1 hour
	return OSc_Error_OK;
}


static struct OSc_Setting_Impl SettingImpl_BHAcqTime = {
	.GetInt32 = GetAcqTime,
	.SetInt32 = SetAcqTime,
	.GetNumericConstraintType = OSc_Setting_NumericConstraintRange,
	.GetInt32Range = GetAcqTimeRange,
};


OSc_Error BH_SPC150PrepareSettings(OSc_Device *device)
{
	if (GetData(device)->settings)
		return OSc_Error_OK;

	OSc_Setting *fileName;
	OSc_Return_If_Error(OSc_Setting_Create(&fileName, device, "BH-FileName", OSc_Value_Type_String,
		&SettingImpl_FileName, NULL));

	OSc_Setting *flimStarted;
	OSc_Return_If_Error(OSc_Setting_Create(&flimStarted, device, "BH-StartFLIM", OSc_Value_Type_Bool,
		&SettingImpl_FLIMStarted, NULL));


	OSc_Setting *acqTime;
	OSc_Return_If_Error(OSc_Setting_Create(&acqTime, device, "BH_AcqTime", OSc_Value_Type_Int32,
		&SettingImpl_BHAcqTime, NULL));


	OSc_Setting *ss[] = {
		fileName, flimStarted, acqTime,
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