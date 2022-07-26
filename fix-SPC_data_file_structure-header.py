# Modify the SPC_data_file_structure.h file provided by Becker-Hickl to make it
# compilable.

# The tested version of SPC_data_file_structure.h had the following MD5 sum:
# 175c3ebb156565446a492e6823f21b2d

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
# Convert encoding to UTF-8

import re
import sys


def skip_from_to(it, p1, p2):
    """sed /p1/,/p2/d"""
    p1 = re.compile(p1)
    p2 = re.compile(p2)
    try:
        while True:
            line = next(it)
            if re.search(p1, line):
                while True:
                    line = next(it)
                    if re.search(p2, line):
                        break
            else:
                yield line
    except StopIteration:
        return


def replace_up_to_first_match(it, p1, p2, s):
    """sed 0,/p1/ s/p2/s/"""
    p1 = re.compile(p1)
    p2 = re.compile(p2)
    try:
        while True:
            line = next(it)
            yield re.sub(p2, s, line)
            if re.search(p1, line):
                break
        yield from it
    except StopIteration:
        return


def replace_all(it, p, s):
    """sed s/p1/s/"""
    p = re.compile(p)
    for line in it:
        yield re.sub(p, s, line)


infile, outfile = sys.argv[1:]

with open(infile, encoding="iso-8859-1", errors="strict") as infp:
    it = reversed(infp.readlines())

it = skip_from_to(it, "}MIMGParam;", "typedef struct")
it = skip_from_to(it, "}MOMParam;", "typedef struct")
it = skip_from_to(it, "}WndLayout;", "typedef struct")
it = skip_from_to(it, "}GR_Param32;", "typedef struct")
it = skip_from_to(it, "}HST3Dtrace;", "typedef struct")
it = skip_from_to(it, "}HST3DParam;", "typedef struct")
it = reversed(list(it))
it = replace_up_to_first_match(it, "}BHFileBlockHeader;", "Header", "HeaderOld")
it = replace_all(it, "U_LONG", "unsigned long")
it = replace_all(it, "U_INT", "unsigned int")
it = replace_all(it, "U_SHORT", "unsigned short")
it = replace_all(it, "U_CHAR", "unsigned char")
it = replace_all(it, "^const char ", "static const char ")

with open(outfile, "w", encoding="utf-8", errors="strict") as outfp:
    outfp.write("".join(it))
