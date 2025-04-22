#include "BH_SPC150Private.h"

#include "RateCounters.h"

#include <stdio.h>

// For most settings, we set the setting's implData to the device.
// This function can then be used to retrieve the device implData.
static inline struct BH_PrivateData *
GetSettingDeviceData(OScDev_Setting *setting) {
    return (struct BH_PrivateData *)OScDev_Device_GetImplData(
        (OScDev_Device *)OScDev_Setting_GetImplData(setting));
}

static OScDev_Error GetNumericConstraintTypeImpl_DiscreteValues(
    OScDev_Setting *setting, OScDev_ValueConstraint *constraintType) {
    *constraintType = OScDev_ValueConstraint_DiscreteValues;
    return OScDev_OK;
}

static OScDev_Error
GetNumericConstraintTypeImpl_Range(OScDev_Setting *setting,
                                   OScDev_ValueConstraint *constraintType) {
    *constraintType = OScDev_ValueConstraint_Range;
    return OScDev_OK;
}

static OScDev_Error IsWritableImpl_ReadOnly(OScDev_Setting *setting,
                                            bool *writable) {
    *writable = false;
    return OScDev_OK;
}

struct EnableChannelData {
    OScDev_Device *device;
    int hwChannel;
};

static void ReleaseEnableChannel(OScDev_Setting *setting) {
    free(OScDev_Setting_GetImplData(setting));
}

static OScDev_Error GetEnableChannel(OScDev_Setting *setting, bool *value) {
    struct EnableChannelData *data = OScDev_Setting_GetImplData(setting);
    struct BH_PrivateData *deviceData =
        OScDev_Device_GetImplData(data->device);
    *value = deviceData->channelMask & (1 << data->hwChannel);
    return OScDev_OK;
}

static OScDev_Error SetEnableChannel(OScDev_Setting *setting, bool value) {
    struct EnableChannelData *data = OScDev_Setting_GetImplData(setting);
    struct BH_PrivateData *deviceData =
        OScDev_Device_GetImplData(data->device);
    if (value) {
        deviceData->channelMask |= 1 << data->hwChannel;
    } else {
        deviceData->channelMask &= ~(uint16_t)(1 << data->hwChannel);
    }
    return OScDev_OK;
}

static OScDev_SettingImpl SettingImpl_EnableChannel = {
    .Release = ReleaseEnableChannel,
    .GetBool = GetEnableChannel,
    .SetBool = SetEnableChannel,
};

static OScDev_Error GetIntensityImagesCumulative(OScDev_Setting *setting,
                                                 bool *value) {
    *value = GetSettingDeviceData(setting)->accumulateIntensity;
    return OScDev_OK;
}

static OScDev_Error SetIntensityImagesCumulative(OScDev_Setting *setting,
                                                 bool value) {
    GetSettingDeviceData(setting)->accumulateIntensity = value;
    return OScDev_OK;
}

static OScDev_SettingImpl SettingImpl_IntensityImagesCumulative = {
    .GetBool = GetIntensityImagesCumulative,
    .SetBool = SetIntensityImagesCumulative,
};

struct MarkerActiveEdgeSettingData {
    OScDev_Device *device;
    uint32_t markerBit;
};

static void ReleaseMarkerActiveEdge(OScDev_Setting *setting) {
    free(OScDev_Setting_GetImplData(setting));
}

static OScDev_Error GetMarkerActiveEdgeNumValues(OScDev_Setting *setting,
                                                 uint32_t *count) {
    *count = MarkerPolarityNumValues;
    return OScDev_OK;
}

static OScDev_Error GetMarkerActiveEdgeNameForValue(OScDev_Setting *setting,
                                                    uint32_t value,
                                                    char *name) {
    switch (value) {
    case MarkerPolarityDisabled:
        strcpy(name, "Disabled");
        break;
    case MarkerPolarityRisingEdge:
        strcpy(name, "RisingEdge");
        break;
    case MarkerPolarityFallingEdge:
        strcpy(name, "FallingEdge");
        break;
    default:
        return OScDev_Error_Illegal_Argument;
    }
    return OScDev_OK;
}

static OScDev_Error GetMarkerActiveEdgeValueForName(OScDev_Setting *setting,
                                                    uint32_t *value,
                                                    const char *name) {
    if (strcmp(name, "Disabled") == 0) {
        *value = MarkerPolarityDisabled;
    } else if (strcmp(name, "RisingEdge") == 0) {
        *value = MarkerPolarityRisingEdge;
    } else if (strcmp(name, "FallingEdge") == 0) {
        *value = MarkerPolarityFallingEdge;
    } else {
        return OScDev_Error_Illegal_Argument;
    }
    return OScDev_OK;
}

static OScDev_Error GetMarkerActiveEdge(OScDev_Setting *setting,
                                        uint32_t *value) {
    struct MarkerActiveEdgeSettingData *data =
        OScDev_Setting_GetImplData(setting);
    struct BH_PrivateData *deviceData =
        OScDev_Device_GetImplData(data->device);
    *value = deviceData->markerActiveEdges[data->markerBit];
    return OScDev_OK;
}

static OScDev_Error SetMarkerActiveEdge(OScDev_Setting *setting,
                                        uint32_t value) {
    struct MarkerActiveEdgeSettingData *data =
        OScDev_Setting_GetImplData(setting);
    struct BH_PrivateData *deviceData =
        OScDev_Device_GetImplData(data->device);
    deviceData->markerActiveEdges[data->markerBit] = value;
    return OScDev_OK;
}

static OScDev_SettingImpl SettingImpl_MarkerActiveEdge = {
    .Release = ReleaseMarkerActiveEdge,
    .GetEnumNumValues = GetMarkerActiveEdgeNumValues,
    .GetEnumNameForValue = GetMarkerActiveEdgeNameForValue,
    .GetEnumValueForName = GetMarkerActiveEdgeValueForName,
    .GetEnum = GetMarkerActiveEdge,
    .SetEnum = SetMarkerActiveEdge,
};

struct ScanMarkerAssignmentData {
    OScDev_Device *device;
    enum ScanMarkerType markerType;
};

static void ReleaseScanMarkerAssignment(OScDev_Setting *setting) {
    free(OScDev_Setting_GetImplData(setting));
}

static OScDev_Error GetScanMarkerAssignmentNumValues(OScDev_Setting *setting,
                                                     uint32_t *count) {
    *count = NUM_MARKER_BITS + 1; // +1 for "none"
    return OScDev_OK;
}

static OScDev_Error
GetScanMarkerAssignmentNameForValue(OScDev_Setting *setting, uint32_t value,
                                    char *name) {
    if (value < NUM_MARKER_BITS) {
        snprintf(name, OScDev_MAX_STR_SIZE, "Marker%d", value);
    } else {
        strcpy(name, "None");
    }
    return OScDev_OK;
}

static OScDev_Error
GetScanMarkerAssignmentValueForName(OScDev_Setting *setting, uint32_t *value,
                                    const char *name) {
    const char *prefix = "Marker";
    size_t prefixLen = strlen(prefix);
    if (strncmp(name, prefix, prefixLen) == 0) {
        int n = atoi(name + prefixLen);
        if (n >= NUM_MARKER_BITS) {
            return OScDev_Error_Illegal_Argument;
        }
        *value = n;
    } else if (strcmp(name, "None") == 0) {
        *value = -1;
    } else {
        return OScDev_Error_Illegal_Argument;
    }
    return OScDev_OK;
}

static OScDev_Error GetScanMarkerAssignment(OScDev_Setting *setting,
                                            uint32_t *value) {
    struct ScanMarkerAssignmentData *data =
        OScDev_Setting_GetImplData(setting);
    struct BH_PrivateData *deviceData =
        OScDev_Device_GetImplData(data->device);
    switch (data->markerType) {
    case ScanMarkerTypePixelMarker:
        *value = deviceData->pixelMarkerBit;
        break;
    case ScanMarkerTypeLineMarker:
        *value = deviceData->lineMarkerBit;
        break;
    case ScanMarkerTypeFrameMarker:
        *value = deviceData->frameMarkerBit;
        break;
    }
    return OScDev_OK;
}

static OScDev_Error SetScanMarkerAssignment(OScDev_Setting *setting,
                                            uint32_t value) {
    struct ScanMarkerAssignmentData *data =
        OScDev_Setting_GetImplData(setting);
    struct BH_PrivateData *deviceData =
        OScDev_Device_GetImplData(data->device);
    switch (data->markerType) {
    case ScanMarkerTypePixelMarker:
        deviceData->pixelMarkerBit = value;
        break;
    case ScanMarkerTypeLineMarker:
        deviceData->lineMarkerBit = value;
        break;
    case ScanMarkerTypeFrameMarker:
        deviceData->frameMarkerBit = value;
        break;
    }
    return OScDev_OK;
}

static OScDev_SettingImpl SettingImpl_ScanMarkerAssignment = {
    .Release = ReleaseScanMarkerAssignment,
    .GetEnumNumValues = GetScanMarkerAssignmentNumValues,
    .GetEnumNameForValue = GetScanMarkerAssignmentNameForValue,
    .GetEnumValueForName = GetScanMarkerAssignmentValueForName,
    .GetEnum = GetScanMarkerAssignment,
    .SetEnum = SetScanMarkerAssignment,
};

static OScDev_Error GetPixelMappingModeNumValues(OScDev_Setting *setting,
                                                 uint32_t *count) {
    *count = PixelMappingModeNumValues;
    return OScDev_OK;
}

static OScDev_Error GetPixelMappingModeNameForValue(OScDev_Setting *setting,
                                                    uint32_t value,
                                                    char *name) {
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

static OScDev_Error GetPixelMappingModeValueForName(OScDev_Setting *setting,
                                                    uint32_t *value,
                                                    const char *name) {
    if (strcmp(name, "LineStartMarkers") == 0) {
        *value = PixelMappingModeLineStartMarkers;
    } else if (strcmp(name, "LineEndMarkers") == 0) {
        *value = PixelMappingModeLineEndMarkers;
    } else {
        return OScDev_Error_Illegal_Argument;
    }
    return OScDev_OK;
}

static OScDev_Error GetPixelMappingMode(OScDev_Setting *setting,
                                        uint32_t *value) {
    *value = GetSettingDeviceData(setting)->pixelMappingMode;
    return OScDev_OK;
}

static OScDev_Error SetPixelMappingMode(OScDev_Setting *setting,
                                        uint32_t value) {
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

static OScDev_Error GetLineDelayPxRange(OScDev_Setting *setting, double *min,
                                        double *max) {
    // This is just an arbitrary, generous range
    *min = -1000.0;
    *max = +1000.0;
    return OScDev_OK;
}

static OScDev_Error GetLineDelayPx(OScDev_Setting *setting, double *value) {
    *value = GetSettingDeviceData(setting)->lineDelayPx;
    return OScDev_OK;
}

static OScDev_Error SetLineDelayPx(OScDev_Setting *setting, double value) {
    GetSettingDeviceData(setting)->lineDelayPx = value;
    return OScDev_OK;
}

static OScDev_SettingImpl SettingImpl_LineDelayPx = {
    .GetNumericConstraintType = GetNumericConstraintTypeImpl_Range,
    .GetFloat64Range = GetLineDelayPxRange,
    .GetFloat64 = GetLineDelayPx,
    .SetFloat64 = SetLineDelayPx,
};

static OScDev_Error GetCheckSync(OScDev_Setting *setting, bool *value) {
    *value = GetSettingDeviceData(setting)->checkSyncBeforeAcq;
    return OScDev_OK;
}

static OScDev_Error SetCheckSync(OScDev_Setting *setting, bool value) {
    GetSettingDeviceData(setting)->checkSyncBeforeAcq = value;
    return OScDev_OK;
}

static OScDev_SettingImpl SettingImpl_CheckSync = {
    .GetBool = GetCheckSync,
    .SetBool = SetCheckSync,
};

static OScDev_Error GetSaveFiles(OScDev_Setting *setting, bool *value) {
    *value = GetSettingDeviceData(setting)->saveFiles;
    return OScDev_OK;
}

static OScDev_Error SetSaveFiles(OScDev_Setting *setting, bool value) {
    GetSettingDeviceData(setting)->saveFiles = value;
    return OScDev_OK;
}

static OScDev_SettingImpl SettingImpl_SaveFiles = {
    .GetBool = GetSaveFiles,
    .SetBool = SetSaveFiles,
};

static OScDev_Error GetFileNamePrefix(OScDev_Setting *setting, char *value) {
    strcpy(value, GetSettingDeviceData(setting)->fileNamePrefix);
    return OScDev_OK;
}

static OScDev_Error SetFileNamePrefix(OScDev_Setting *setting,
                                      const char *value) {
    strcpy(GetSettingDeviceData(setting)->fileNamePrefix, value);
    return OScDev_OK;
}

static OScDev_SettingImpl SettingImpl_FileNamePrefix = {
    .GetString = GetFileNamePrefix,
    .SetString = SetFileNamePrefix,
};

static OScDev_Error GetSenderPort(OScDev_Setting *setting, int32_t *value) {
    *value = GetSettingDeviceData(setting)->senderPort;
    return OScDev_OK;
}

static OScDev_Error SetSenderPort(OScDev_Setting *setting, int32_t value) {
    if (value < 0 || value > 65535)
        value = 0;
    GetSettingDeviceData(setting)->senderPort = value;
    return OScDev_OK;
}

static OScDev_SettingImpl SettingImpl_SenderPort = {
    .GetInt32 = GetSenderPort,
    .SetInt32 = SetSenderPort,
};

static OScDev_Error GetSDTCompression(OScDev_Setting *setting, bool *value) {
    *value = GetSettingDeviceData(setting)->compressHistograms;
    return OScDev_OK;
}

static OScDev_Error SetSDTCompression(OScDev_Setting *setting, bool value) {
    GetSettingDeviceData(setting)->compressHistograms = value;
    return OScDev_OK;
}

static OScDev_SettingImpl SettingImpl_SDTCompression = {
    .GetBool = GetSDTCompression,
    .SetBool = SetSDTCompression,
};

struct RateCounterData {
    OScDev_Device *device;
    int index;
};

static void ReleaseRateCounter(OScDev_Setting *setting) {
    free(OScDev_Setting_GetImplData(setting));
}

static OScDev_Error GetRateCounter(OScDev_Setting *setting, double *value) {
    struct RateCounterData *data = OScDev_Setting_GetImplData(setting);
    float values[4];
    GetRates(GetData(data->device)->rates, values);
    *value = values[data->index];
    return OScDev_OK;
}

static OScDev_SettingImpl SettingImpl_RateCounter = {
    .Release = ReleaseRateCounter,
    .IsWritable = IsWritableImpl_ReadOnly,
    .GetFloat64 = GetRateCounter,
};

OScDev_Error BH_MakeSettings(OScDev_Device *device,
                             OScDev_PtrArray **settings) {
    OScDev_RichError *err = OScDev_RichError_OK;
    *settings = OScDev_PtrArray_Create();

    for (int i = 0; i < MAX_NUM_CHANNELS; ++i) {
        struct EnableChannelData *data =
            calloc(1, sizeof(struct EnableChannelData));
        data->device = device;
        data->hwChannel = i;
        char name[64];
        snprintf(name, sizeof(name), "EnableChannel%d", i);
        OScDev_Setting *enableChannel;
        err = OScDev_Error_AsRichError(
            OScDev_Setting_Create(&enableChannel, name, OScDev_ValueType_Bool,
                                  &SettingImpl_EnableChannel, data));
        if (err) {
            free(data);
            goto error;
        }
        OScDev_PtrArray_Append(*settings, enableChannel);
    }

    OScDev_Setting *accumulateIntensity;
    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &accumulateIntensity, "IntensityImagesAreCumulative",
        OScDev_ValueType_Bool, &SettingImpl_IntensityImagesCumulative,
        device));
    if (err)
        goto error;
    OScDev_PtrArray_Append(*settings, accumulateIntensity);

    for (int i = 0; i < NUM_MARKER_BITS; ++i) {
        struct MarkerActiveEdgeSettingData *data =
            calloc(1, sizeof(struct MarkerActiveEdgeSettingData));
        data->device = device;
        data->markerBit = i;
        char name[64];
        snprintf(name, sizeof(name), "Marker%dActiveEdge", i);
        OScDev_Setting *markerActiveEdge;
        err = OScDev_Error_AsRichError(OScDev_Setting_Create(
            &markerActiveEdge, name, OScDev_ValueType_Enum,
            &SettingImpl_MarkerActiveEdge, data));
        if (err) {
            free(data);
            goto error;
        }
        OScDev_PtrArray_Append(*settings, markerActiveEdge);
    }

    const char *scanMarkers[] = {"PixelMarker", "LineMarker", "FrameMarker"};
    int scanMarkerTypes[] = {ScanMarkerTypePixelMarker,
                             ScanMarkerTypeLineMarker,
                             ScanMarkerTypeFrameMarker};
    for (int i = 0; i < 3; ++i) {
        struct ScanMarkerAssignmentData *data =
            calloc(1, sizeof(struct ScanMarkerAssignmentData));
        data->device = device;
        data->markerType = scanMarkerTypes[i];
        OScDev_Setting *markerAssignment;
        err = OScDev_Error_AsRichError(OScDev_Setting_Create(
            &markerAssignment, scanMarkers[i], OScDev_ValueType_Enum,
            &SettingImpl_ScanMarkerAssignment, data));
        if (err) {
            free(data);
            goto error;
        }
        OScDev_PtrArray_Append(*settings, markerAssignment);
    }

    OScDev_Setting *pixelMappingMode;
    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &pixelMappingMode, "PixelMappingMode", OScDev_ValueType_Enum,
        &SettingImpl_PixelMappingMode, device));
    if (err)
        goto error;
    OScDev_PtrArray_Append(*settings, pixelMappingMode);

    OScDev_Setting *lineDelayPx;
    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &lineDelayPx, "LineDelay_px", OScDev_ValueType_Float64,
        &SettingImpl_LineDelayPx, device));
    if (err)
        goto error;
    OScDev_PtrArray_Append(*settings, lineDelayPx);

    OScDev_Setting *checkSync;
    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &checkSync, "CheckSyncBeforeAcquisition", OScDev_ValueType_Bool,
        &SettingImpl_CheckSync, device));
    if (err)
        goto error;
    OScDev_PtrArray_Append(*settings, checkSync);

    OScDev_Setting *saveFiles;
    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &saveFiles, "FLIMFileSaving", OScDev_ValueType_Bool,
        &SettingImpl_SaveFiles, device));
    if (err)
        goto error;
    OScDev_PtrArray_Append(*settings, saveFiles);

    OScDev_Setting *fileNamePrefix;
    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &fileNamePrefix, "FLIMFileNamePrefix", OScDev_ValueType_String,
        &SettingImpl_FileNamePrefix, device));
    if (err)
        goto error;
    OScDev_PtrArray_Append(*settings, fileNamePrefix);

    OScDev_Setting *senderPort;
    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &senderPort, "SendFLIMHistogramsToUDPPort", OScDev_ValueType_Int32,
        &SettingImpl_SenderPort, device));
    if (err)
        goto error;
    OScDev_PtrArray_Append(*settings, senderPort);

    OScDev_Setting *sdtCompression;
    err = OScDev_Error_AsRichError(OScDev_Setting_Create(
        &sdtCompression, "SDTCompression", OScDev_ValueType_Bool,
        &SettingImpl_SDTCompression, device));
    if (err)
        goto error;
    OScDev_PtrArray_Append(*settings, sdtCompression);

    const char *rateCounters[] = {"Sync", "CFD", "TAC", "ADC"};
    for (int i = 0; i < 4; ++i) {
        struct RateCounterData *data =
            calloc(1, sizeof(struct RateCounterData));
        data->device = device;
        data->index = i;
        char name[64];
        snprintf(name, sizeof(name), "RateCounter-%s", rateCounters[i]);
        OScDev_Setting *rateCounter;
        err = OScDev_Error_AsRichError(
            OScDev_Setting_Create(&rateCounter, name, OScDev_ValueType_Float64,
                                  &SettingImpl_RateCounter, data));
        if (err) {
            free(data);
            goto error;
        }
        OScDev_PtrArray_Append(*settings, rateCounter);
    }

    return OScDev_OK;

error:
    for (size_t i = 0; i < OScDev_PtrArray_Size(*settings); ++i) {
        OScDev_Setting_Destroy(OScDev_PtrArray_At(*settings, i));
    }
    free(*settings);
    *settings = NULL;
    return OScDev_Error_ReturnAsCode(
        OScDev_Error_Wrap(err, "Failed to create settings"));
}
