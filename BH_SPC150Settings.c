#include "BH_SPC150Private.h"


// For most settings, we set the setting's implData to the device.
// This function can then be used to retrieve the device implData.
static inline struct BH_PrivateData *GetSettingDeviceData(OScDev_Setting *setting)
{
	return (struct BH_PrivateData *)OScDev_Device_GetImplData((OScDev_Device *)OScDev_Setting_GetImplData(setting));
}


OScDev_Error GetNumericConstraintTypeImpl_DiscreteValues(OScDev_Setting *setting, enum OScDev_ValueConstraint *constraintType)
{
	*constraintType = OScDev_ValueConstraint_DiscreteValues;
	return OScDev_OK;
}


OScDev_Error GetNumericConstraintTypeImpl_Range(OScDev_Setting *setting, enum OScDev_ValueConstraint *constraintType)
{
	*constraintType = OScDev_ValueConstraint_Range;
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

static struct OScDev_SettingImpl SettingImpl_FileName = {
	.GetString = GetFileName,
	.SetString = SetFileName,
};

static OScDev_Error GetFLIMStarted(OScDev_Setting *setting, bool *value)
{
	*value = GetSettingDeviceData(setting)->flimStarted;
	return OScDev_OK;
}


static OScDev_Error SetFLIMStarted(OScDev_Setting *setting, bool value)
{
	GetSettingDeviceData(setting)->flimStarted = value;
	GetSettingDeviceData(setting)->settingsChanged = true;
	return OScDev_OK;
}


static struct OScDev_SettingImpl SettingImpl_FLIMStarted = {
	.GetBool = GetFLIMStarted,
	.SetBool = SetFLIMStarted,
};


static OScDev_Error GetFLIMFinished(OScDev_Setting *setting, bool *value)
{
	*value = GetSettingDeviceData(setting)->flimDone;
	return OScDev_OK;
}


static OScDev_Error SetFLIMFinished(OScDev_Setting *setting, bool value)
{
	GetSettingDeviceData(setting)->flimDone = value;
	return OScDev_OK;
}


static struct OScDev_SettingImpl SettingImpl_FLIMFinished = {
	.GetBool = GetFLIMFinished,
	.SetBool = SetFLIMFinished,
};


static OScDev_Error GetAcqTime(OScDev_Setting *setting, int32_t *value)
{
	*value = GetSettingDeviceData(setting)->acqTime;
	return OScDev_OK;
}


static OScDev_Error SetAcqTime(OScDev_Setting *setting, int32_t value)
{
	GetSettingDeviceData(setting)->acqTime = value;
	GetSettingDeviceData(setting)->settingsChanged = true;
	return OScDev_OK;
}


static OScDev_Error GetAcqTimeRange(OScDev_Setting *setting, int32_t *min, int32_t *max)
{
	*min = 1;
	*max = 3600;  // 1 hour
	return OScDev_OK;
}


static struct OScDev_SettingImpl SettingImpl_BHAcqTime = {
	.GetInt32 = GetAcqTime,
	.SetInt32 = SetAcqTime,
	.GetNumericConstraintType = GetNumericConstraintTypeImpl_Range,
	.GetInt32Range = GetAcqTimeRange,
};


static OScDev_Error GetCFD(OScDev_Setting *setting, double *value)
{
	EnterCriticalSection(&GetSettingDeviceData(setting)->rateCountersMutex);
	*value = GetSettingDeviceData(setting)->cfdRate;
	LeaveCriticalSection(&GetSettingDeviceData(setting)->rateCountersMutex);
	return OScDev_OK;
}


static OScDev_Error SetCFD(OScDev_Setting *setting, double value)
{
	// read only
	return OScDev_OK;
}


static OScDev_Error GetCFDRange(OScDev_Setting *setting, double *min, double *max)
{
	*min = 1.0;
	*max = 1e8;
	return OScDev_OK;
}


static struct OScDev_SettingImpl SettingImpl_CFD = {
	.GetFloat64 = GetCFD,
	.SetFloat64 = SetCFD,
	.GetNumericConstraintType = GetNumericConstraintTypeImpl_Range,
	.GetFloat64Range = GetCFDRange,
};


static OScDev_Error GetSyncValue(OScDev_Setting *setting, double *value)
{
	EnterCriticalSection(&GetSettingDeviceData(setting)->rateCountersMutex);
	*value = GetSettingDeviceData(setting)->syncRate;
	LeaveCriticalSection(&GetSettingDeviceData(setting)->rateCountersMutex);
	return OScDev_OK;
}


static OScDev_Error SetSyncValue(OScDev_Setting *setting, double value)
{
	// read only
	return OScDev_OK;
}


static OScDev_Error GetSyncValueRange(OScDev_Setting *setting, double *min, double *max)
{
	*min = 1.0;
	*max = 1e8;
	return OScDev_OK;
}


static struct OScDev_SettingImpl SettingImpl_Sync = {
	.GetFloat64 = GetSyncValue,
	.SetFloat64 = SetSyncValue,
	.GetNumericConstraintType = GetNumericConstraintTypeImpl_Range,
	.GetFloat64Range = GetSyncValueRange,
};


static OScDev_Error GetADC(OScDev_Setting *setting, double *value)
{
	EnterCriticalSection(&GetSettingDeviceData(setting)->rateCountersMutex);
	*value = GetSettingDeviceData(setting)->adcRate;
	LeaveCriticalSection(&GetSettingDeviceData(setting)->rateCountersMutex);
	return OScDev_OK;
}


static OScDev_Error SetADC(OScDev_Setting *setting, double value)
{
	// read only
	return OScDev_OK;
}


static OScDev_Error GetADCRange(OScDev_Setting *setting, double *min, double *max)
{
	*min = 1.0;
	*max = 1e8;
	return OScDev_OK;
}


static struct OScDev_SettingImpl SettingImpl_ADC = {
	.GetFloat64 = GetADC,
	.SetFloat64 = SetADC,
	.GetNumericConstraintType = GetNumericConstraintTypeImpl_Range,
	.GetFloat64Range = GetADCRange,
};


static OScDev_Error GetTAC(OScDev_Setting *setting, double *value)
{
	EnterCriticalSection(&GetSettingDeviceData(setting)->rateCountersMutex);
	*value = GetSettingDeviceData(setting)->tacRate;
	LeaveCriticalSection(&GetSettingDeviceData(setting)->rateCountersMutex);
	return OScDev_OK;
}


static OScDev_Error SetTAC(OScDev_Setting *setting, double value)
{
	// read only
	return OScDev_OK;
}


static OScDev_Error GetTACRange(OScDev_Setting *setting, double *min, double *max)
{
	*min = 1.0;
	*max = 1e8;
	return OScDev_OK;
}


static struct OScDev_SettingImpl SettingImpl_TAC = {
	.GetFloat64 = GetTAC,
	.SetFloat64 = SetTAC,
	.GetNumericConstraintType = GetNumericConstraintTypeImpl_Range,
	.GetFloat64Range = GetTACRange,
};

OScDev_Error BH_SPC150PrepareSettings(OScDev_Device *device)
{
	if (GetData(device)->settings)
		return OScDev_OK;

	OScDev_Error err;

	OScDev_Setting *fileName;
	if (OScDev_CHECK(err, OScDev_Setting_Create(&fileName, "BH-FileName", OScDev_ValueType_String,
		&SettingImpl_FileName, device)))
		return err;

	OScDev_Setting *flimStarted;
	if (OScDev_CHECK(err, OScDev_Setting_Create(&flimStarted, "BH-StartFLIM", OScDev_ValueType_Bool,
		&SettingImpl_FLIMStarted, device)))
		return err;

	OScDev_Setting *flimFinished;
	if (OScDev_CHECK(err, OScDev_Setting_Create(&flimFinished, "BH-FLIMFinished", OScDev_ValueType_Bool,
		&SettingImpl_FLIMFinished, device)))
		return err;

	OScDev_Setting *acqTime;
	if (OScDev_CHECK(err, OScDev_Setting_Create(&acqTime, "BH_AcqTime", OScDev_ValueType_Int32,
		&SettingImpl_BHAcqTime, device)))
		return err;

	OScDev_Setting *cfd_value;
	if (OScDev_CHECK(err, OScDev_Setting_Create(&cfd_value, "BH_CFD", OScDev_ValueType_Float64,
		&SettingImpl_CFD, device)))
		return err;

	OScDev_Setting *sync_value;
	if (OScDev_CHECK(err, OScDev_Setting_Create(&sync_value, "BH_Sync", OScDev_ValueType_Float64,
		&SettingImpl_Sync, device)))
		return err;

	OScDev_Setting *adc_value;
	if (OScDev_CHECK(err, OScDev_Setting_Create(&adc_value, "BH_ADC", OScDev_ValueType_Float64,
		&SettingImpl_ADC, device)))
		return err;


	OScDev_Setting *tac_value;
	if (OScDev_CHECK(err, OScDev_Setting_Create(&tac_value, "BH_TAC", OScDev_ValueType_Float64,
		&SettingImpl_TAC, device)))
		return err;


	OScDev_Setting *ss[] = {
		fileName, flimStarted, flimFinished, acqTime,
		cfd_value, sync_value, adc_value, tac_value,
	};  // OSc_Device_Get_Settings() returns count = 1 when this has NULL inside
	size_t nSettings = sizeof(ss) / sizeof(OScDev_Setting *);
	if (*ss == NULL)
		nSettings = 0;
	OScDev_Setting **settings = malloc(sizeof(ss));
	memcpy(settings, ss, sizeof(ss));

	GetData(device)->settings = settings;
	GetData(device)->settingCount = nSettings;
	return OScDev_OK;
}