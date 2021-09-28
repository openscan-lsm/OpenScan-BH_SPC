#!/bin/sh

# Run this script in Git Bash to generate a compilable version of
# SPC_data_file_structure.h.

# The tested version of SPC_data_file_structure.h had the following MD5 sum:
# 175c3ebb156565446a492e6823f21b2d

mkdir -p generated_include

# Remove typedef struct ... XYZ blocks for the following types:
#   MIMGParam (uses undefined type Window)
#   MOMParam (uses undefined type Window)
#   WndLayout (uses undefined type BHPanelData)
#   GR_Param32 (uses Trace2d32 before it's defined)
#   HST3Dtrace (uses undefined type Interval)
#   HST3DParam (uses HST3Dtrace)
# Rename first of the two definitions of BHFileBlockHeader (append 'Old')
# Replace U_LONG, U_INT, U_SHORT, U_CHAR with corresponding types
# Make char[32] difinitions 'static' to prevent link errors
tac '/c/Program Files (x86)/BH/SPCM/SPC_data_file_structure.h' \
	|sed '/}MIMGParam;/,/typedef struct/d' \
	|sed '/}MOMParam;/,/typedef struct/d' \
	|sed '/}WndLayout;/,/typedef struct/d' \
	|sed '/}GR_Param32;/,/typedef struct/d' \
	|sed '/}HST3Dtrace;/,/typedef struct/d' \
	|sed '/}HST3DParam;/,/typedef struct/d' \
	|tac \
	|sed '0,/}BHFileBlockHeader;/ s/Header/HeaderOld/' \
	|sed 's/U_LONG/unsigned long/' \
	|sed 's/U_INT/unsigned int/' \
	|sed 's/U_SHORT/unsigned short/' \
	|sed 's/U_CHAR/unsigned char/' \
	|sed 's/^const char /static const char /' \
	>generated_include/SPC_data_file_structure_fixed.h
