#include "BH_SPC150Private.h"

#include "AcquisitionControl.h"
#include "RateCounters.h"

#include <Spcm_def.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>

static bool g_BH_initialized = false;
static size_t g_openDeviceCount = 0;

// Forward declaration
static OScDev_DeviceImpl BH_TCSPC_Device_Impl;

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

static OScDev_Error EnsureFLIMBoardInitialized(void) {
    // There is a mismatch between the OpenScan model of initializing a device
    // with how BH SPC works: multiple BH modules (boards) are initialized at
    // once using a single .ini file. We will need to figure out a workaround
    // for this when we support multiple modules, but for now we assume a
    // single module.

    if (g_BH_initialized) {
        // Reject second instance, as we don't yet support multiple modules
        return OScDev_Error_Device_Already_Open;
    }

    char iniFileName[] = "sspcm.ini";
    short spcErr = SPC_init(iniFileName);
    if (spcErr < 0) {
        char msg[OScDev_MAX_STR_LEN + 1] = "Cannot initialize BH SPC using ";
        strcat(msg, iniFileName);
        strcat(msg, ": ");
        char bhMsg[OScDev_MAX_STR_LEN + 1];
        SPC_get_error_string(spcErr, bhMsg, sizeof(bhMsg));
        strncat(msg, bhMsg, sizeof(msg) - strlen(msg) - 1);
        bhMsg[sizeof(bhMsg) - 1] = '\0';
        OScDev_Log_Error(NULL, msg);
        return OScDev_Error_Unknown; // TODO: error reporting
    }

    // Note: The force-initialize code that used to be here was removed after
    // testing to ensure it is not necessary (even after a crash).

    g_BH_initialized = true;
    return OScDev_OK;
}

static OScDev_Error DeinitializeFLIMBoard(void) {
    if (!g_BH_initialized)
        return OScDev_OK;

    // See comment where we call SPC_init(). We assume only one module is in
    // use.
    SPC_close();

    g_BH_initialized = false;
    return OScDev_OK;
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
    OScDev_Error err;
    if (OScDev_CHECK(
            err, OScDev_Device_Create(&device, &BH_TCSPC_Device_Impl, data))) {
        char msg[OScDev_MAX_STR_LEN + 1] =
            "Failed to create device for BH SPC";
        OScDev_Log_Error(device, msg);
        return err;
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
    OScDev_Error err;
    if (OScDev_CHECK(err, EnsureFLIMBoardInitialized()))
        return err;

    PopulateDefaultParameters(GetData(device));

    if (OScDev_CHECK(err, InitializeDeviceForAcquisition(device)))
        return err;

    GetData(device)->rates =
        StartRateCounterMonitor(GetData(device)->moduleNr, 0.25);

    ++g_openDeviceCount;

    OScDev_Log_Debug(device, "BH SPC board initialized");
    return OScDev_OK;
}

static OScDev_Error BH_Close(OScDev_Device *device) {
    ShutdownAcquisitionState(device);

    StopRateCounterMonitor(GetData(device)->rates);
    GetData(device)->rates = NULL;

    --g_openDeviceCount;

    OScDev_Error err;
    if (g_openDeviceCount == 0 && OScDev_CHECK(err, DeinitializeFLIMBoard()))
        return err;

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
    if (useClock || useScanner || !useDetector)
        return OScDev_Error_Unsupported_Operation;

    OScDev_ClockSource clockSource;
    OScDev_Acquisition_GetClockSource(acq, &clockSource);
    if (clockSource != OScDev_ClockSource_External)
        return OScDev_Error_Unsupported_Operation;

    int err = StartAcquisition(device, acq);
    if (err != 0) {
        return err;
    }

    return OScDev_OK;
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
};
