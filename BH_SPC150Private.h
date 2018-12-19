#pragma once
#pragma pack(1)

#include "OpenScanLibPrivate.h"

#include <Spcm_def.h>

#include <Windows.h>
#define BH_HEADER_VALID       0x5555
#define FLOW_DATA             2   // continuos flow measurement ( BIFL )  
#define MEAS_DATA_FROM_FILE   3
#define CALC_DATA             4   // calculated data
#define SIM_DATA              5   // simulated data
#define FIFO_DATA             8   // FIFO mode data
#define FIFO_DATA_FROM_FILE   9   // FIFO mode data
#define MOM_DATA             10   // moments mode data
#define MOM_DATA_FROM_FILE   11


#define DECAY_BLOCK    0x0     // one decay curve
#define PAGE_BLOCK     0x10    // set of decay curves = measured page
#define FCS_BLOCK      0x20    // FCS histogram curve                 
#define FIDA_BLOCK     0x30    // FIDA histogram curve                 
#define FILDA_BLOCK    0x40    // FILDA histogram curve                 
#define MCS_BLOCK      0x50    // MCS histogram curve                 
#define IMG_BLOCK      0x60    // fifo image - set of curves = PS FLIM
#define MCSTA_BLOCK    0x70    // MCS Triggered Accumulation histogram curve                 
#define IMG_MCS_BLOCK  0x80    // fifo image - set of curves = MCS FLIM
#define MOM_BLOCK      0x90    // moments mode - set of moments data frames
#define IMG_INT_BLOCK  0xa0    // fifo image - INTENSITY ( only for SPC-160)
#define IMG_WF_BLOCK   0xb0    // fifo image - Wide-Field FLIM

/*---- for computing header check_sum----*/
#define BH_HDR_LENGTH         42
#define BH_HEADER_CHKSUM      0x55aa
#define BH_HEADER_NOT_VALID   0x1111
#define BH_HEADER_VALID       0x5555


// bits 8 - 11 in block_type define data type      

#define DATA_USHORT           0x0     // 16-bit unsigned short
//   (it was the only type up to 20.08.04)
#define DATA_ULONG            0x100   // 32-bit unsigned long, for FIFO decay curves                                 
#define DATA_DBL              0x200   // 64-bit double, for histogram data blocks 

// bit 12 = 1 -   block contains compressed data      
#define DATA_ZIPPED           0x1000  // data block is compressed  

// bits 13- 15 in block_type are not yet defined, reserved for future use

// defines for page data sets
#define MEAS_PAGE             0x11
#define FLOW_PAGE             0x12
#define MEAS_PAGE_FROM_FILE   0x13
#define CALC_PAGE             0x14
#define SIM_PAGE              0x15
#define MODULE					 0

struct AcqPrivateData
{
	OSc_Acquisition *acquisition;

	uint16_t *frameBuffer;
	size_t width;
	size_t height;
	uint64_t pixelTime;
	double cfd_value;

	CRITICAL_SECTION mutex;
	CONDITION_VARIABLE acquisitionFinishCondition;
	bool stopRequested;
	bool isRunning;
	HANDLE thread;
	HANDLE readoutThread;
	short streamHandle;

	bool wroteHeader;
	char fileName[OSc_MAX_STR_LEN];

	//FIFO params
	short fifo_type;
	unsigned long words_in_buf;
	unsigned short *buffer;
	short initVariableTyope;
	short firstWrite;
	char phot_fname[80];//TODO


};


struct BH_PrivateData
{
	short moduleNr;

	char flimFileName[OSc_MAX_STR_LEN + 1]; // for saving the raw data to hard drive

	OSc_Setting **settings;
	size_t settingCount;

	bool settingsChanged;

	bool flimStarted;   // actual FLIM acquitiion starts when True
	bool flimDone;
	uint32_t acqTime; //seconds

	struct AcqPrivateData acquisition;
};


static inline struct BH_PrivateData *GetData(OSc_Device *device)
{
	return (struct BH_PrivateData *)(device->implData);
}

typedef struct {
	short revision;
	long info_offset;
	short info_length;
	long setup_offset;
	short setup_length;
	long data_block_offset;
	short no_of_data_blocks;
	long data_block_length;
	long meas_desc_block_offset;
	short no_of_meas_desc_blocks;
	short meas_desc_block_length;
	unsigned short header_valid;
	unsigned long reserved1;
	unsigned short reserved2;
	unsigned short chksum;
} sdt_file_header;

//  1 - File header
typedef struct {
	short    revision;  // software revision & module identification
						//   lowest bits 0-3 -   software revision ( >= 12(decimal))
						//        current value = 15 - support for huge data blocks >128MB <= 2GB
						//   bits 11-4   - BH module type
						//      meaning of this field values (hex):
						//        0x20 -SPC-130, 0x21 - SPC-600, 0x22 - SPC-630,
						//        0x23 -SPC-700, 0x24 - SPC-730, 0x25 - SPC-830,
						//        0x26 -SPC-140, 0x27 - SPC-930, 0x28 - SPC-150,
						//        0x29 -DPC-230, 0x2a - SPC-130EM
						//   highest bits 15-12 - module subtype - not used yet
	long     info_offs; // offset of the info part which contains general text 
						//   information (Title, date, time, contents etc.)
	short    info_length;  // length of the info part
	long     setup_offs;   // offset of the setup text data 
						   // (system parameters, display parameters, trace parameters etc.)
	short    setup_length;  // length of the setup data
	long     data_block_offs;   // offset of the first data block 
	short    no_of_data_blocks; // no_of_data_blocks valid only when in 0 .. 0x7ffe range,
								// if equal to 0x7fff  the  field 'reserved1' contains 
								//     valid no_of_data_blocks
	unsigned long     data_block_length;     // length of the longest block in the file  
											 //        ( not compressed ) in bytes
	long     meas_desc_block_offs;  // offset to 1st. measurement description block 
									//   (system parameters connected to data blocks)
	short    no_of_meas_desc_blocks;  // number of measurement description blocks
	short    meas_desc_block_length;  // length of the measurement description blocks
	unsigned short    header_valid;   // valid: 0x5555, not valid: 0x1111
	unsigned long     reserved1;      // reserved1 now contains no_of_data_blocks
									  // reserved2 now contains length (in int words) of data block extension, normally 0,
									  //   data block extension contains info data for histograms data blocks
	unsigned short    reserved2;
	unsigned short    chksum;            // checksum of file header
}bhfile_header;

typedef struct _bhfile_block_header {
	unsigned char  data_offs_ext;        // extension of data_offs field - address bits 32-39
	unsigned char  next_block_offs_ext;  // extension of next_block_offs field - address bits 32-39
	unsigned long  data_offs;       // offset of the block's data, bits 0-31  
	unsigned long  next_block_offs; // offset to the data block header of the next data block, bits 0-31 
	unsigned short block_type;      // see block_type defines above
	short          meas_desc_block_no;
	unsigned long  lblock_no;       // long block_no - see remarks below
	unsigned long  block_length;    // block( set ) length ( not compressed ) in bytes up to 2GB
}BHFileBlockHeader;



typedef struct {
	unsigned short  status;  // last SPC_test_state return value ( status )
	unsigned short  flags;   // scan clocks bits 2-0( frame, line, pixel), 
							 //  bit  3   - user break occured
							 //      bits 4-8 for FIFO Image mode
							 //  bit  4   - FIFO overrun happened 
							 //  bit  5   - FIFO read up to frame where collection timer expired
							 //  bits 7-6 - wait_for_end_frame = 01 - wait for end of frame, 
							 //                                = 10 - end of frame was found
							 //  bit  8   - measurement was running ( 1st Frame&Line present)
							 //  bits 9   - wait_for_marker 3 to start the measurement 
							 //  bit  15  - reading rates was on

	float  stop_time;        // time from start to  - disarm ( simple measurement )
							 //    - or to the end of the cycle (for complex measurement )
	int    cur_step;         // current step  ( if multi-step measurement )
	int    cur_cycle;        // current cycle (accumulation cycle in FLOW mode ) -
							 //  ( if multi-cycle measurement ) 
	int    cur_page;         // current measured page
	float  min_sync_rate;    // minimum rates during the measurement
	float  min_cfd_rate;     //   ( -1.0 - not set )
	float  min_tac_rate;
	float  min_adc_rate;
	float  max_sync_rate;    // maximum rates during the measurement
	float  max_cfd_rate;     //   ( -1.0 - not set )
	float  max_tac_rate;
	float  max_adc_rate;
	int    reserved1;
	float  reserved2;
}MeasStopInfo;           // information collected when measurement is finished



typedef struct {
	float           fida_time;          // interval time [ms] for FIDA histogram
	float           filda_time;         // interval time [ms] for FILDA histogram
	int             fida_points;        // no of FIDA values  
										//    or current frame number ( fifo_image)
										//    or no of not matched events in T channel (WF FLIM mode)
	int             filda_points;       // no of FILDA values
										//    or current line  number ( fifo_image)
										//    or no of not matched events in X channel (WF FLIM mode)
	float           mcs_time;           // interval time [ms] for MCS histogram
	int             mcs_points;         // no of MCS values
										//    or current pixel number ( fifo_image)
										//    or no of not matched events in Y channel (WF FLIM mode)
	unsigned int    cross_calc_phot;    //  no of calculated photons from cross_channel 
										//    for Cross FCS histogram
										//    or no of calculated photons in X channel (WF FLIM mode)
	unsigned short  mcsta_points;       // no of MCS_TA values
	unsigned short  mcsta_flags;        // MCS_TA flags   bit 0 = 1 - use 'invalid' photons,
										//      bit 1-2  =  marker no used as trigger
	unsigned int    mcsta_tpp;          // MCS_TA Time per point  in Macro Time units 
										// time per point[s] = mcsta_tpp * mt_resol( from MeasFCSInfo)
	unsigned int    calc_markers;       // no of calculated markers for MCS_TA 
	unsigned int    fcs_calc_phot;      //  no of calculated photons for FCS histogram
										//    or no of calculated photons in Y channel (WF FLIM mode)
	unsigned int    reserved3;
}MeasHISTInfo; // extension of MeasFCSInfo for histograms ( FIDA, FILDA, MCS, FCS, MCS_TA ) 




typedef struct {  // keep always 64 bytes size for compatibility
				  // subsequent 4 values are valid only in fifo_image mode
	float   first_frame_time;  //   macro time of the 1st frame marker
	float   frame_time;        //   time between first two frame markers
	float   line_time;         //   time between first two line markers ( in the 1st frame)
	float   pixel_time;        //   time between first two pixel markers ( in the 1st frame&line)
	char    info[48];          //   not used yet
}MeasHISTInfoExt; // extension of MeasHISTInfo for additional histograms info



typedef struct {
	unsigned short  chan;               // routing channel number
	unsigned short  fcs_decay_calc;     // defines which histograms were calculated
										// bit 0 = 1 - decay curve calculated
										// bit 1 = 1 - fcs   curve calculated
										// bit 2 = 1 - FIDA  curve calculated
										// bit 3 = 1 - FILDA curve calculated
										// bit 4 = 1 - MCS curve calculated
										// bit 5 = 1 - 3D Image calculated
										// bit 6 = 1 - MCSTA curve calculated
										// bit 7 = 1 - 3D MCS Image calculated
										// bit 8 = 1 - INTENSITY image calculated
										// bit 9 = 1 - WF (Wide-Field) FLIM image calculated
	unsigned int    mt_resol;           // macro time clock in 0.1 ns ( 1fs for DPC-230) units
	float           cortime;            // correlation time [ms] 
	unsigned int    calc_photons;       //  total no of photons in decay histogram  
	int             fcs_points;         // no of FCS values
	float           end_time;           // macro time of the last photon 
	unsigned short  overruns;           // no of Fifo overruns 
										//   when > 0  fcs curve & end_time are not valid
	unsigned short  fcs_type;   // 0 - linear FCS with log binning ( 100 bins/log )
								// when bit 15 = 1 ( 0x8000 ) - Multi-Tau FCS 
								//           where bits 14-0 = ktau parameter
	unsigned short  cross_chan;         // cross FCS routing channel number
										//   when chan = cross_chan and mod == cross_mod - Auto FCS
										//        otherwise - Cross FCS
	unsigned short  mod;                // module number
	unsigned short  cross_mod;          // cross FCS module number
	unsigned int    cross_mt_resol;     // macro time clock of cross FCS module in 0.1 ns units
}MeasFCSInfo;   // information collected when FIFO measurement is finished


typedef struct {
	short block_no;
	long data_offset;
	long next_block_offset;
	unsigned short block_type;
	short meas_desc_block_no;
	unsigned long lblock_no;
	unsigned long block_length;
} data_block_header;

typedef struct _MeasureInfo {
	char     time[9];        /* time of creation */
	char     date[11];       /* date of creation */
	char     mod_ser_no[16]; /* serial number of the module */
	short    meas_mode;
	float    cfd_ll;
	float    cfd_lh;
	float    cfd_zc;
	float    cfd_hf;
	float    syn_zc;
	short    syn_fd;
	float    syn_hf;
	float    tac_r;
	short    tac_g;
	float    tac_of;
	float    tac_ll;
	float    tac_lh;
	short    adc_re;        /* 0 means 65536 */
	short    eal_de;
	short    ncx;
	short    ncy;
	unsigned short  page;
	float    col_t;
	float    rep_t;
	short    stopt;
	char     overfl;
	short    use_motor;
	unsigned short    steps;
	float    offset;
	short    dither;
	short    incr;
	short    mem_bank;
	char     mod_type[16];   /* module type */
	float    syn_th;
	short    dead_time_comp;
	short    polarity_l;   //  2 = disabled line markers
	short    polarity_f;
	short    polarity_p;
	short    linediv;      // line predivider = 2 ** ( linediv)
	short    accumulate;
	int      flbck_y;
	int      flbck_x;
	int      bord_u;
	int      bord_l;
	float    pix_time;
	short    pix_clk;
	short    trigger;
	int      scan_x;
	int      scan_y;
	int      scan_rx;
	int      scan_ry;
	short    fifo_typ;
	int      epx_div;
	unsigned short  mod_type_code;
	unsigned short  mod_fpga_ver;    // new in v.8.4
	float    overflow_corr_factor;
	int      adc_zoom;
	int      cycles;        //  cycles ( accumulation cycles in FLOW mode ) 
	MeasStopInfo StopInfo;
	MeasFCSInfo  FCSInfo;   // valid only for FIFO meas
	int      image_x;       // 4 subsequent fields valid only for Camera mode
	int      image_y;       //     or FIFO_IMAGE mode
	int      image_rx;
	int      image_ry;
	short    xy_gain;       // gain for XY ADCs ( SPC930 )
	short    dig_flags; // SP_MST_CLK parameter bits 0-7 - digital flags : 
						//   bit 0 - use or not  Master Clock (SPC140 multi-module )
						//   bit 1 - Continuous Flow On/Off for scan modes in SPC150
						//   bit 2 - time(X) axis of decay curves reversed by software(1), or not (0)
	short    adc_de;        // ADC sample delay ( SPC-930 )
	short    det_type;      // detector type ( SPC-930 in camera mode ) 
	short    x_axis;        // X axis representation ( SPC-930 ) 
	MeasHISTInfo  HISTInfo; // extension of FCSInfo, valid only for FIFO meas
	MeasHISTInfoExt  HISTInfoExt; // extension of HSTInfo, valid only for FIFO meas
	float    sync_delay;    // Sync Delay [ns] when using BH SyncDel USB box
	unsigned short sdel_ser_no;  // serial number of Sync Delay box, 
								 //         = 0 - SyncDelay box was not used  
	char     sdel_input;    // active input of SyncDelay box, 0 - IN 1, 1 - IN 2                         
	char     mosaic_ctrl;   // bit 0 - mosaic imaging was used, 
							// bit 1 - mosaic type ( 00 - sequence of frames, 01 - rout. channels)
							// bit 4 - mosaic type extension( 10 - sequence of Z planes of Axio Observer.Z1)
							// bit 2-3 mosaic restart type ( 0 - no, 1 - Marker 3, 2 - Ext. trigger
	unsigned char     mosaic_x;      //  no of mosaic elements in X dir.
	unsigned char     mosaic_y;      //  no of mosaic elements in Y dir.
	short    frames_per_el; //  frames per mosaic element 1 .. 32767
	short    chan_per_el;   //  routing channels per mosaic element 1 .. 256
	int      mosaic_cycles_done;  // number of mosaic accumulation cycles done 
	unsigned short mla_ser_no;  // serial number of MLA4 device, 
								//         = 0 - MLA4 device was not used 
	unsigned char  DCC_in_use;  // bits 0..3 = 1 when DCC module M1..M4 was in use
	char     dcc_ser_no[12];    //  serial number of used DCC module
	char     reserve[32];       // total size of MeasureInfo = 512 bytes
}MeasureInfo;




OSc_Error BH_SPC150PrepareSettings(OSc_Device *device);