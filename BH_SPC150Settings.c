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


static OScDev_Error GetPixelMappingModeNumValues(OScDev_Setting *setting, uint32_t *count)
{
	*count = PixelMappingModeNumValues;
	return OScDev_OK;
}


static OScDev_Error GetPixelMappingModeNameForValue(OScDev_Setting *setting, uint32_t value, char *name)
{
	switch (value) {
	case PixelMappingModeLineStartMarkers:
		strcpy(name, "LineStartMarkers");
		break;
	case PixelMappingModeLineEndMarkers:
		strcpy(name, "LineEndMarkers");
		break;
	default:
		return OScDev_Error_Illegal_Argument;
	}
	return OScDev_OK;
}


static OScDev_Error GetPixelMappingModeValueForName(OScDev_Setting *setting, uint32_t *value, const char *name)
{
	if (strcmp(name, "LineStartMarkers") == 0) {
		*value = PixelMappingModeLineStartMarkers;
	}
	else if (strcmp(name, "LineEndMarkers") == 0) {
		*value = PixelMappingModeLineEndMarkers;
	}
	else {
		return OScDev_Error_Illegal_Argument;
	}
	return OScDev_OK;
}


static OScDev_Error GetPixelMappingMode(OScDev_Setting *setting, uint32_t *value)
{
	*value = GetSettingDeviceData(setting)->pixelMappingMode;
	return OScDev_OK;
}


static OScDev_Error SetPixelMappingMode(OScDev_Setting *setting, uint32_t value)
{
	GetSettingDeviceData(setting)->pixelMappingMode = value;
	return OScDev_OK;
}


static OScDev_SettingImpl SettingImpl_PixelMappingMode = {
	.GetEnumNumValues = GetPixelMappingModeNumValues,
	.GetEnumNameForValue = GetPixelMappingModeNameForValue,
	.GetEnumValueForName = GetPixelMappingModeValueForName,
	.GetEnum = GetPixelMappingMode,
	.SetEnum = SetPixelMappingMode,
};


static OScDev_Error GetLineDelayPxRange(OScDev_Setting *setting, double *min, double *max)
{
	// This is just an arbitrary, generous range
	*min = -1000.0;
	*max = +1000.0;
	return OScDev_OK;
}


static OScDev_Error GetLineDelayPx(OScDev_Setting *setting, double *value)
{
	*value = GetSettingDeviceData(setting)->lineDelayPx;
	return OScDev_OK;
}


static OScDev_Error SetLineDelayPx(OScDev_Setting *setting, double value)
{
	GetSettingDeviceData(setting)->lineDelayPx = value;
	return OScDev_OK;
}


static OScDev_SettingImpl SettingImpl_LineDelayPx = {
	.GetNumericConstraintType = GetNumericConstraintTypeImpl_Range,
	.GetFloat64Range = GetLineDelayPxRange,
	.GetFloat64 = GetLineDelayPx,
	.SetFloat64 = SetLineDelayPx,
};


static OScDev_Error GetSPCFilename(OScDev_Setting *setting, char *value)
{
	strcpy(value, GetSettingDeviceData(setting)->spcFilename);
	return OScDev_OK;
}


static OScDev_Error SetSPCFilename(OScDev_Setting *setting, const char *value)
{
	strcpy(GetSettingDeviceData(setting)->spcFilename, value);
	return OScDev_OK;
}


static OScDev_SettingImpl SettingImpl_SPCFilename = {
	.GetString = GetSPCFilename,
	.SetString = SetSPCFilename,
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

	OScDev_Setting *pixelMappingMode;
	if (OScDev_CHECK(err, OScDev_Setting_Create(&pixelMappingMode, "PixelMappingMode", OScDev_ValueType_Enum,
		&SettingImpl_PixelMappingMode, device)))
		goto error;
	OScDev_PtrArray_Append(*settings, pixelMappingMode);

	OScDev_Setting *lineDelayPx;
	if (OScDev_CHECK(err, OScDev_Setting_Create(&lineDelayPx, "LineDelay_px", OScDev_ValueType_Float64,
		&SettingImpl_LineDelayPx, device)))
		goto error;
	OScDev_PtrArray_Append(*settings, lineDelayPx);

	OScDev_Setting *spcFilename;
	if (OScDev_CHECK(err, OScDev_Setting_Create(&spcFilename, "SPCFilename", OScDev_ValueType_String,
		&SettingImpl_SPCFilename, device)))
		goto error;
	OScDev_PtrArray_Append(*settings, spcFilename);

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