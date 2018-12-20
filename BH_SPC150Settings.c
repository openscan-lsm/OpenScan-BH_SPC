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


static OSc_Error GetFLIMFinished(OSc_Setting *setting, bool *value)
{
	*value = GetData(setting->device)->flimDone;
	return OSc_Error_OK;
}


static OSc_Error SetFLIMFinished(OSc_Setting *setting, bool value)
{
	GetData(setting->device)->flimDone = value;
	return OSc_Error_OK;
}


static struct OSc_Setting_Impl SettingImpl_FLIMFinished = {
	.GetBool = GetFLIMFinished,
	.SetBool = SetFLIMFinished,
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


static OSc_Error GetCFD(OSc_Setting *setting, double *value)
{
	*value = GetData(setting->device)->acquisition.cfd_value;
	return OSc_Error_OK;
}


static OSc_Error SetCFD(OSc_Setting *setting, double value)
{
	// read only
	return OSc_Error_OK;
}


static OSc_Error GetCFDRange(OSc_Setting *setting, double *min, double *max)
{
	*min = 1.0;
	*max = 1e8;
	return OSc_Error_OK;
}


static struct OSc_Setting_Impl SettingImpl_CFD = {
	.GetFloat64 = GetCFD,
	.SetFloat64 = SetCFD,
	.GetNumericConstraintType = OSc_Setting_NumericConstraintRange,
	.GetFloat64Range = GetCFDRange,
};


static OSc_Error GetSyncValue(OSc_Setting *setting, double *value)
{
	*value = GetData(setting->device)->acquisition.sync_value;
	return OSc_Error_OK;
}


static OSc_Error SetSyncValue(OSc_Setting *setting, double value)
{
	// read only
	return OSc_Error_OK;
}


static OSc_Error GetSyncValueRange(OSc_Setting *setting, double *min, double *max)
{
	*min = 1.0;
	*max = 1e8;
	return OSc_Error_OK;
}


static struct OSc_Setting_Impl SettingImpl_Sync = {
	.GetFloat64 = GetSyncValue,
	.SetFloat64 = SetSyncValue,
	.GetNumericConstraintType = OSc_Setting_NumericConstraintRange,
	.GetFloat64Range = GetSyncValueRange,
};


static OSc_Error GetADC(OSc_Setting *setting, double *value)
{
	*value = GetData(setting->device)->acquisition.adc_value;
	return OSc_Error_OK;
}


static OSc_Error SetADC(OSc_Setting *setting, double value)
{
	// read only
	return OSc_Error_OK;
}


static OSc_Error GetADCRange(OSc_Setting *setting, double *min, double *max)
{
	*min = 1.0;
	*max = 1e8;
	return OSc_Error_OK;
}


static struct OSc_Setting_Impl SettingImpl_ADC = {
	.GetFloat64 = GetADC,
	.SetFloat64 = SetADC,
	.GetNumericConstraintType = OSc_Setting_NumericConstraintRange,
	.GetFloat64Range = GetADCRange,
};


static OSc_Error GetTAC(OSc_Setting *setting, double *value)
{
	*value = GetData(setting->device)->acquisition.tac_value;
	return OSc_Error_OK;
}


static OSc_Error SetTAC(OSc_Setting *setting, double value)
{
	// read only
	return OSc_Error_OK;
}


static OSc_Error GetTACRange(OSc_Setting *setting, double *min, double *max)
{
	*min = 1.0;
	*max = 1e8;
	return OSc_Error_OK;
}


static struct OSc_Setting_Impl SettingImpl_TAC = {
	.GetFloat64 = GetTAC,
	.SetFloat64 = SetTAC,
	.GetNumericConstraintType = OSc_Setting_NumericConstraintRange,
	.GetFloat64Range = GetTACRange,
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

	OSc_Setting *flimFinished;
	OSc_Return_If_Error(OSc_Setting_Create(&flimFinished, device, "BH-FLIMFinished", OSc_Value_Type_Bool,
		&SettingImpl_FLIMFinished, NULL));
	
	OSc_Setting *acqTime;
	OSc_Return_If_Error(OSc_Setting_Create(&acqTime, device, "BH_AcqTime", OSc_Value_Type_Int32,
		&SettingImpl_BHAcqTime, NULL));

	OSc_Setting *cfd_value;
	OSc_Return_If_Error(OSc_Setting_Create(&cfd_value, device, "BH_CFD", OSc_Value_Type_Float64,
		&SettingImpl_CFD, NULL));

	OSc_Setting *sync_value;
	OSc_Return_If_Error(OSc_Setting_Create(&sync_value, device, "BH_Sync", OSc_Value_Type_Float64,
		&SettingImpl_Sync, NULL));

	OSc_Setting *adc_value;
	OSc_Return_If_Error(OSc_Setting_Create(&adc_value, device, "BH_ADC", OSc_Value_Type_Float64,
		&SettingImpl_ADC, NULL));


	OSc_Setting *tac_value;
	OSc_Return_If_Error(OSc_Setting_Create(&tac_value, device, "BH_TAC", OSc_Value_Type_Float64,
		&SettingImpl_TAC, NULL));


	OSc_Setting *ss[] = {
		fileName, flimStarted, flimFinished, acqTime, 
		cfd_value, sync_value, adc_value, tac_value,
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