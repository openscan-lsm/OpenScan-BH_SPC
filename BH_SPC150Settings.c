#include "BH_SPC150Private.h"


// For most settings, we set the setting's implData to the device.
// This function can then be used to retrieve the device implData.
static inline struct BH_PrivateData *GetSettingDeviceData(OScDev_Setting *setting)
{
	return (struct BH_PrivateData *)OScDev_Device_GetImplData((OScDev_Device *)OScDev_Setting_GetImplData(setting));
}


static OScDev_Error GetNumericConstraintTypeImpl_DiscreteValues(OScDev_Setting *setting, OScDev_ValueConstraint *constraintType)
{
	*constraintType = OScDev_ValueConstraint_DiscreteValues;
	return OScDev_OK;
}


static OScDev_Error GetNumericConstraintTypeImpl_Range(OScDev_Setting *setting, OScDev_ValueConstraint *constraintType)
{
	*constraintType = OScDev_ValueConstraint_Range;
	return OScDev_OK;
}


static OScDev_Error IsWritableImpl_ReadOnly(OScDev_Setting *setting, bool *writable)
{
	*writable = false;
	return OScDev_OK;
}


static OScDev_Error GetFileName(OScDev_Setting *setting, char *value)
 {
	strcpy(value, GetSettingDeviceData(setting)->flimFileName);
	return OScDev_OK;
	}

static OScDev_Error SetFileName(OScDev_Setting *setting, const char *value)
 {
	strcpy(GetSettingDeviceData(setting)->flimFileName, value);
	return OScDev_OK;
	}

static OScDev_SettingImpl SettingImpl_FileName = {
	.GetString = GetFileName,
	.SetString = SetFileName,
};


static OScDev_Error GetAcqTime(OScDev_Setting *setting, int32_t *value)
{
	*value = GetSettingDeviceData(setting)->acqTime;
	return OScDev_OK;
}


static OScDev_Error SetAcqTime(OScDev_Setting *setting, int32_t value)
{
	GetSettingDeviceData(setting)->acqTime = value;
	return OScDev_OK;
}


static OScDev_Error GetAcqTimeRange(OScDev_Setting *setting, int32_t *min, int32_t *max)
{
	*min = 1;
	*max = 3600;  // 1 hour
	return OScDev_OK;
}


static OScDev_SettingImpl SettingImpl_BHAcqTime = {
	.GetInt32 = GetAcqTime,
	.SetInt32 = SetAcqTime,
	.GetNumericConstraintType = GetNumericConstraintTypeImpl_Range,
	.GetInt32Range = GetAcqTimeRange,
};


static OScDev_Error GetSyncValue(OScDev_Setting *setting, double *value)
{
	EnterCriticalSection(&GetSettingDeviceData(setting)->rateCountersMutex);
	*value = GetSettingDeviceData(setting)->syncRate;
	LeaveCriticalSection(&GetSettingDeviceData(setting)->rateCountersMutex);
	return OScDev_OK;
}


static OScDev_SettingImpl SettingImpl_Sync = {
	.GetFloat64 = GetSyncValue,
	.IsWritable = IsWritableImpl_ReadOnly,
};


static OScDev_Error GetCFD(OScDev_Setting *setting, double *value)
{
	EnterCriticalSection(&GetSettingDeviceData(setting)->rateCountersMutex);
	*value = GetSettingDeviceData(setting)->cfdRate;
	LeaveCriticalSection(&GetSettingDeviceData(setting)->rateCountersMutex);
	return OScDev_OK;
}


static OScDev_SettingImpl SettingImpl_CFD = {
	.GetFloat64 = GetCFD,
	.IsWritable = IsWritableImpl_ReadOnly,
};


static OScDev_Error GetTAC(OScDev_Setting *setting, double *value)
{
	EnterCriticalSection(&GetSettingDeviceData(setting)->rateCountersMutex);
	*value = GetSettingDeviceData(setting)->tacRate;
	LeaveCriticalSection(&GetSettingDeviceData(setting)->rateCountersMutex);
	return OScDev_OK;
}


static OScDev_SettingImpl SettingImpl_TAC = {
	.GetFloat64 = GetTAC,
	.IsWritable = IsWritableImpl_ReadOnly,
};


static OScDev_Error GetADC(OScDev_Setting *setting, double *value)
{
	EnterCriticalSection(&GetSettingDeviceData(setting)->rateCountersMutex);
	*value = GetSettingDeviceData(setting)->adcRate;
	LeaveCriticalSection(&GetSettingDeviceData(setting)->rateCountersMutex);
	return OScDev_OK;
}


static OScDev_SettingImpl SettingImpl_ADC = {
	.GetFloat64 = GetADC,
	.IsWritable = IsWritableImpl_ReadOnly,
};


OScDev_Error BH_MakeSettings(OScDev_Device *device, OScDev_PtrArray **settings)
{
	OScDev_Error err = OScDev_OK;
	*settings = OScDev_PtrArray_Create();

	OScDev_Setting *fileName;
	if (OScDev_CHECK(err, OScDev_Setting_Create(&fileName, "BH-FileName", OScDev_ValueType_String,
		&SettingImpl_FileName, device)))
		goto error;
	OScDev_PtrArray_Append(*settings, fileName);

	OScDev_Setting *acqTime;
	if (OScDev_CHECK(err, OScDev_Setting_Create(&acqTime, "BH_AcqTime", OScDev_ValueType_Int32,
		&SettingImpl_BHAcqTime, device)))
		goto error;
	OScDev_PtrArray_Append(*settings, acqTime);

	OScDev_Setting *sync_value;
	if (OScDev_CHECK(err, OScDev_Setting_Create(&sync_value, "BH_SyncRate", OScDev_ValueType_Float64,
		&SettingImpl_Sync, device)))
		goto error;
	OScDev_PtrArray_Append(*settings, sync_value);

	OScDev_Setting *cfd_value;
	if (OScDev_CHECK(err, OScDev_Setting_Create(&cfd_value, "BH_CFDRate", OScDev_ValueType_Float64,
		&SettingImpl_CFD, device)))
		goto error;
	OScDev_PtrArray_Append(*settings, cfd_value);

	OScDev_Setting *tac_value;
	if (OScDev_CHECK(err, OScDev_Setting_Create(&tac_value, "BH_TACRate", OScDev_ValueType_Float64,
		&SettingImpl_TAC, device)))
		goto error;
	OScDev_PtrArray_Append(*settings, tac_value);

	OScDev_Setting *adc_value;
	if (OScDev_CHECK(err, OScDev_Setting_Create(&adc_value, "BH_ADCRate", OScDev_ValueType_Float64,
		&SettingImpl_ADC, device)))
		goto error;
	OScDev_PtrArray_Append(*settings, adc_value);

	return OScDev_OK;

error:
	for (size_t i = 0; i < OScDev_PtrArray_Size(*settings); ++i) {
		OScDev_Setting_Destroy(OScDev_PtrArray_At(*settings, i));
	}
	free(*settings);
	*settings = NULL;
	return err;
}