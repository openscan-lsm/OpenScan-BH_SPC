#include "BH_SPC150Private.h"

#include "AcquisitionControl.h"
#include "RateCounters.h"

#include <Spcm_def.h>
#include <ss8str.h>

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static bool g_BH_initialized = false;
static size_t g_openDeviceCount = 0;

// Forward declaration
static OScDev_DeviceImpl BH_TCSPC_Device_Impl;

static bool FileExists(const char *name) {
    FILE *f = fopen(name, "r");
    if (f != NULL) {
        fclose(f);
        return true;
    }
    return false;
}

static int IndexOfFirstFileThatExists(const char **candidates) {
    for (size_t i = 0; candidates[i] != NULL; ++i) {
        if (FileExists(candidates[i])) {
            return (int)i;
        }
    }
    return -1;
}

static void PopulateDefaultParameters(struct BH_PrivateData *data) {
    memset(data, 0, sizeof(struct BH_PrivateData));

    data->channelMask = 1; // Enable channel 0 only by default
    data->accumulateIntensity = true;

    for (int i = 0; i < NUM_MARKER_BITS; ++i) {
        data->markerActiveEdges[i] = MarkerPolarityRisingEdge;
    }
    data->pixelMarkerBit = -1;
    data->lineMarkerBit = 1;
    data->frameMarkerBit = 2;

    data->pixelMappingMode = PixelMappingModeLineStartMarkers;
    data->lineDelayPx = 0.0;
    strcpy(data->fileNamePrefix, "OpenScan-BHSPC");
    data->senderPort = 0;
    data->checkSyncBeforeAcq = true;
}

static OScDev_RichError *EnsureFLIMBoardInitialized(void) {
    // There is a mismatch between the OpenScan model of initializing a device
    // with how BH SPC works: multiple BH modules (boards) are initialized at
    // once using a single .ini file. We will need to figure out a workaround
    // for this when we support multiple modules, but for now we assume a
    // single module.

    if (g_BH_initialized) {
        // Reject second instance, as we don't yet support multiple modules
        return OScDev_Error_Create("Device already open");
    }

    OScDev_RichError *err = OScDev_RichError_OK;

    ss8str mmPathSpcmIni;
    ss8str iniFileName;
    ss8str msg;
    ss8_init(&mmPathSpcmIni);
    ss8_init(&iniFileName);
    ss8_init(&msg);

    const char *iniFileCandidates[] = {
        "spcm.ini",
        "sspcm.ini", // For compatibility with earlier versions of this module
        NULL,        // Space for $MICROMANAGER_PATH/spcm.ini
        NULL,
    };
    // A bit of a hack (since OpenScan is not necessarily running in MM), but
    // allow spcm.ini (not sspcm.ini) to be in $MICROMANAGER_PATH (e.g., for
    // use with pymmcore-plus).
    const char *mmpath = getenv("MICROMANAGER_PATH");
    if (mmpath != NULL) {
        ss8_copy_cstr(&mmPathSpcmIni, mmpath);
        if (!ss8_is_empty(&mmPathSpcmIni)) {
            ss8_cat_ch(&mmPathSpcmIni, '/');
            ss8_cat_cstr(&mmPathSpcmIni, "spcm.ini");
            iniFileCandidates[2] = ss8_cstr(&mmPathSpcmIni);
        }
    }

    int iniFileIndex = IndexOfFirstFileThatExists(iniFileCandidates);
    if (iniFileIndex < 0) {
        ss8_copy_cstr(&msg, "Cannot find the SPCM .ini file (tried ");
        for (size_t i = 0; iniFileCandidates[i] != NULL; ++i) {
            ss8_cat_cstr(&msg, iniFileCandidates[i]);
            if (iniFileCandidates[i + 1] != NULL) {
                ss8_cat_cstr(&msg, ", ");
            }
        }
        ss8_cat_ch(&msg, ')');
        err = OScDev_Error_Create(ss8_cstr(&msg));
        goto finish;
    }

    // SPC_init() wants a non-const string, so make a copy.
    ss8_copy_cstr(&iniFileName, iniFileCandidates[iniFileIndex]);
    short spcErr = SPC_init(ss8_mutable_cstr(&iniFileName));
    if (spcErr < 0) {
        ss8_copy_cstr(&msg, "Cannot initialize BH SPC using ");
        ss8_cat(&msg, &iniFileName);
        ss8_cat_cstr(&msg, ": ");

        size_t offset = ss8_len(&msg);
        ss8_set_len(&msg, OScDev_MAX_STR_LEN);
        size_t maxlen = ss8_len(&msg) - offset;
        assert(maxlen < SHRT_MAX);
        SPC_get_error_string(spcErr, ss8_mutable_cstr_suffix(&msg, offset),
                             (short)maxlen);
        ss8_set_len_to_cstrlen(&msg);

        err = OScDev_Error_Create(ss8_cstr(&msg));
        goto finish;
    }

    g_BH_initialized = true;

finish:
    ss8_destroy(&msg);
    ss8_destroy(&iniFileName);
    ss8_destroy(&mmPathSpcmIni);
    return err;
}

static OScDev_RichError *DeinitializeFLIMBoard(void) {
    if (!g_BH_initialized)
        return OScDev_RichError_OK;

    // See comment where we call SPC_init(). We assume only one module is in
    // use.
    SPC_close();

    g_BH_initialized = false;
    return OScDev_RichError_OK;
}

static OScDev_Error BH_EnumerateInstances(OScDev_PtrArray **devices) {
    // TODO SPC_test_id(0...7) can be used to get all modules present.
    // We would then have the user supply a .ini file for each module,
    // and check that the .ini file specifies the correct module number
    // before calling SPC_init().

    struct BH_PrivateData *data = calloc(1, sizeof(struct BH_PrivateData));
    // For now, support just one board
    data->moduleNr = 0;

    OScDev_Device *device;
    OScDev_RichError *err;
    err = OScDev_Error_AsRichError(
        OScDev_Device_Create(&device, &BH_TCSPC_Device_Impl, data));
    if (err) {
        err = OScDev_Error_Wrap(err, "Failed to create device for BH SPC");
        return OScDev_Error_ReturnAsCode(err);
    }

    PopulateDefaultParameters(GetData(device));

    *devices = OScDev_PtrArray_Create();
    OScDev_PtrArray_Append(*devices, device);
    return OScDev_OK;
}

static OScDev_Error BH_GetModelName(const char **name) {
    // TODO Get actual model (store in device private data)
    *name = "Becker & Hickl TCSPC";
    return OScDev_OK;
}

static OScDev_Error BH_ReleaseInstance(OScDev_Device *device) {
    free(GetData(device));
    return OScDev_OK;
}

static OScDev_Error BH_GetName(OScDev_Device *device, char *name) {
    // TODO Name should probably include model name and module number
    strncpy(name, "BH-TCSPC", OScDev_MAX_STR_LEN);
    return OScDev_OK;
}

static OScDev_Error BH_Open(OScDev_Device *device) {
    OScDev_RichError *err = OScDev_RichError_OK;
    err = EnsureFLIMBoardInitialized();
    if (err)
        return OScDev_Error_ReturnAsCode(err);

    PopulateDefaultParameters(GetData(device));

    err = InitializeDeviceForAcquisition(device);
    if (err)
        return OScDev_Error_ReturnAsCode(err);

    GetData(device)->rates =
        StartRateCounterMonitor(GetData(device)->moduleNr, 0.25);

    ++g_openDeviceCount;

    return OScDev_OK;
}

static OScDev_Error BH_Close(OScDev_Device *device) {
    ShutdownAcquisitionState(device);

    StopRateCounterMonitor(GetData(device)->rates);
    GetData(device)->rates = NULL;

    --g_openDeviceCount;

    if (g_openDeviceCount == 0) {
        OScDev_RichError *err = DeinitializeFLIMBoard();
        if (err)
            return OScDev_Error_ReturnAsCode(err);
    }

    return OScDev_OK;
}

static OScDev_Error BH_HasScanner(OScDev_Device *device, bool *hasScanner) {
    *hasScanner = false;
    return OScDev_OK;
}

static OScDev_Error BH_HasDetector(OScDev_Device *device, bool *hasDetector) {
    *hasDetector = true;
    return OScDev_OK;
}

static OScDev_Error BH_HasClock(OScDev_Device *device, bool *hasDetector) {
    *hasDetector = false;
    return OScDev_OK;
}

static OScDev_Error BH_GetPixelRates(OScDev_Device *device,
                                     OScDev_NumRange **pixelRatesHz) {
    *pixelRatesHz = OScDev_NumRange_CreateContinuous(1e3, 1e7);
    return OScDev_OK;
}

static OScDev_Error BH_GetNumberOfChannels(OScDev_Device *device,
                                           uint32_t *nChannels) {
    *nChannels = 1;
    return OScDev_OK;
}

static OScDev_Error BH_GetBytesPerSample(OScDev_Device *device,
                                         uint32_t *bytesPerSample) {
    *bytesPerSample = 2;
    return OScDev_OK;
}

static OScDev_Error Arm(OScDev_Device *device, OScDev_Acquisition *acq) {
    bool useClock, useScanner, useDetector;
    OScDev_Acquisition_IsClockRequested(acq, &useClock);
    OScDev_Acquisition_IsScannerRequested(acq, &useScanner);
    OScDev_Acquisition_IsDetectorRequested(acq, &useDetector);
    if (useClock || useScanner || !useDetector) {
        return OScDev_Error_ReturnAsCode(OScDev_Error_Create(
            "Unsupported operation (only detector role supported)"));
    }

    OScDev_ClockSource clockSource;
    OScDev_Acquisition_GetClockSource(acq, &clockSource);
    if (clockSource != OScDev_ClockSource_External) {
        return OScDev_Error_ReturnAsCode(OScDev_Error_Create(
            "Unsupported operation (only external clock source supported)"));
    }

    return OScDev_Error_ReturnAsCode(StartAcquisition(device, acq));
}

static OScDev_Error Start(OScDev_Device *device) {
    // We have no software start trigger
    return OScDev_OK;
}

static OScDev_Error Stop(OScDev_Device *device) {
    StopAcquisition(device);
    WaitForAcquisitionToFinish(device);
    return OScDev_OK;
}

static OScDev_Error IsRunning(OScDev_Device *device, bool *isRunning) {
    *isRunning = IsAcquisitionRunning(device);
    return OScDev_OK;
}

static OScDev_Error Wait(OScDev_Device *device) {
    WaitForAcquisitionToFinish(device);
    return OScDev_OK;
}

static OScDev_DeviceImpl BH_TCSPC_Device_Impl = {
    .GetModelName = BH_GetModelName,
    .EnumerateInstances = BH_EnumerateInstances,
    .ReleaseInstance = BH_ReleaseInstance,
    .GetName = BH_GetName,
    .Open = BH_Open,
    .Close = BH_Close,
    .HasScanner = BH_HasScanner,
    .HasDetector = BH_HasDetector,
    .HasClock = BH_HasClock,
    .MakeSettings = BH_MakeSettings,
    .GetPixelRates = BH_GetPixelRates,
    .GetNumberOfChannels = BH_GetNumberOfChannels,
    .GetBytesPerSample = BH_GetBytesPerSample,
    .Arm = Arm,
    .Start = Start,
    .Stop = Stop,
    .IsRunning = IsRunning,
    .Wait = Wait,
};

static OScDev_Error GetDeviceImpls(OScDev_PtrArray **impls) {
    *impls = OScDev_PtrArray_CreateFromNullTerminated(
        (OScDev_DeviceImpl *[]){&BH_TCSPC_Device_Impl, NULL});
    return OScDev_OK;
}

OScDev_MODULE_IMPL = {
    .displayName = "Becker & Hickl TCSPC Module",
    .GetDeviceImpls = GetDeviceImpls,
    .supportsRichErrors = true,
};
