/*
** rawadf
**
** A tool for manipulating Extended (Raw) ADF images created
** with rawread (http://aminet.net/package/disk/bakup/rawread)
**
** Copyright (C) 2010 Gregory Saunders.
**
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see <http://www.gnu.org/licenses/>.
**
** $VER: rawadf.c 0.4 (30.07.2010)
**
** Changelog:
**
** 0.4 (30.07.2010):
**     - Add the split command
**
** 0.3 (13.01.2010):
**     - Fix help text for merge command
**
** 0.2 (12.01.2010):
**     - Initial release
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VERSION "0.4"

/* Amiga version string */
const char AMI_VERSION[] = "$VER: rawadf " VERSION " (30.07.2010)";
const char USAGE[] = "rawadf: Type 'rawadf help' for usage.";

/*
** Constants related to the extended ADF file format
*/
#define EADF_MAXTRACKS 166
#define EADF_BYTESPERRECORD 12
#define EADF_HEADERSIZE 2004
#define EADF_MAGICLEN 8
#define EADF_BUFSIZE 1024

const char EADF_MAGIC[] = "UAE-1ADF";

enum EADFTrackType {
    EADFTRACKTYPE_DOS,
    EADFTRACKTYPE_RAW
}; 
typedef enum EADFTrackType EADFTrackType;
const char *EADFTRACKTYPE_NAMES[] = { "DOS", "RAW" };

typedef struct {
    char magic[EADF_MAGICLEN + 1];
    unsigned long numTracks;
    EADFTrackType trackType[EADF_MAXTRACKS];
    unsigned long trackSizeBytes[EADF_MAXTRACKS];
    unsigned long trackSizeBits[EADF_MAXTRACKS];
    unsigned long trackOffset[EADF_MAXTRACKS];
} EADFHeader;

enum EADFStatus {
    EADFSTATUS_SUCCESS,
    EADFSTATUS_FAILURE
};
typedef enum EADFStatus EADFStatus;

enum EADFTrackSource {
    EADFTRACKSOURCE_NONE,
    EADFTRACKSOURCE_SOURCE1,
    EADFTRACKSOURCE_SOURCE2
};
typedef enum EADFTrackSource EADFTrackSource;

enum EADFError {
    EADFERROR_NOERROR,
    EADFERROR_WRONGMAGIC,
    EADFERROR_INVALIDNUMTRACKS,
    EADFERROR_INVALIDTRACKTYPE,
    EADFERROR_READERROR,
    EADFERROR_WRITEERROR,
    EADFERROR_SEEKERROR,
    EADFERROR_EOFERROR,
    EADFERROR_UNKNOWNERROR
};

enum EADFError eadf_errno;

const char *EADFERROR_MESSAGES[] = {
    /* EADFERROR_NOERROR */
    "No error",

    /* EADFERROR_WRONGMAGIC */
    "Incorrect magic (is this really an extended ADF?)",

    /* EADFERROR_INVALIDNUMTRACKS */
    "Invalid number of tracks",

    /* EADFERROR_INVALIDTRACKTYPE */
    "Invalid track type",

    /* EADFERROR_READERROR */
    "Error reading from file",

    /* EADFERROR_WRITEERROR */
    "Error writing to file",

    /* EADFERROR_SEEKERROR */
    "Error seeking in file",

    /* EADFERROR_EOFERROR */
    "Premature end-of-file",

    /* EADFERROR_UNKNOWNERROR */
    "Unknown error"
};

EADFStatus eadfHeaderInitWithFile(EADFHeader *, FILE *);
void eadfPrintErrorWithContext(const char *context);
EADFStatus eadfMergeFiles(EADFHeader *, FILE *, const char *,
    EADFHeader *, FILE *, const char *, FILE *,
    const EADFTrackSource *);
EADFStatus eadfSplitFile(EADFHeader *, FILE *, const char *, FILE *,
    const EADFTrackSource *);

/*
** Commands
*/
#define COMMAND_BUFSIZE 1024

enum Command {
    COMMAND_COMPARE,
    COMMAND_DOSMERGE,
    COMMAND_HELP,
    COMMAND_INFO,
    COMMAND_MERGE,
    COMMAND_REPLACE,
    COMMAND_SPLIT,
    COMMAND_UNKNOWN
};
typedef enum Command Command;

const char *COMMAND_NAMES[] = {
    "compare",
    "dosmerge",
    "help",
    "info",
    "merge",
    "replace",
    "split",
    "unknown"
};

#define COMMAND_NUMALIASES 12
const char *COMMAND_ALIASES[] = {
    "compare", "cmp",
    "dosmerge", "dos",
    "help", "?", "h",
    "info",
    "merge",
    "replace", "rpl",
    "split"
};

const Command COMMAND_ALIASMAP[] = {
    COMMAND_COMPARE, COMMAND_COMPARE,
    COMMAND_DOSMERGE, COMMAND_DOSMERGE,
    COMMAND_HELP, COMMAND_HELP, COMMAND_HELP,
    COMMAND_INFO,
    COMMAND_MERGE,
    COMMAND_REPLACE, COMMAND_REPLACE,
    COMMAND_SPLIT
};

const char *COMMAND_BASICHELP =
    "usage: rawadf <command> [args]\n"
    "rawadf, version " VERSION ".\n"
    "Type 'rawadf help <command>' for help on a specific command.\n"
    "Type 'rawadf --version' to see the program version.\n\n"
    "rawadf  Copyright (C) 2010 Gregory Saunders\n"
    "This program comes with ABSOLUTELY NO WARRANTY. This is free\n"
    "software, and you are welcome to redistribute it under certain\n"
    "conditions. See the GNU General Public License for more details.\n\n"
    "Available commands:";

const char *COMMAND_HELPTEXT[] = {
    /* COMMAND_COMPARE */
    "compare (cmp): Compare two Extended ADF images.\n"
    "usage: compare SOURCE1 SOURCE2\n\n"
    "Print the extended ADF headers of SOURCE1 and SOURCE2 side by\n"
    "side, highlighting differences with a '*' in the last column.\n\n"
    "Two tracks are considered different if they have different\n"
    "types, different sizes (in either bytes or bits) or the data\n"
    "contained within the track is different.\n",
    
    /* COMMAND_DOSMERGE */
    "dosmerge (dos): Merge two Extended ADF images, preferring DOS tracks.\n"
    "usage: dosmerge SOURCE1 SOURCE2 DESTINATION\n\n"
    "Copy SOURCE1 to DESTINATION replacing non-DOS tracks with the\n"
    "corresponding DOS track from SOURCE2. If the corresponding\n"
    "track in SOURCE2 is not a DOS track, the track from SOURCE1\n"
    "is used.\n\n"
    "The resulting image will have the larger of the number of\n"
    "tracks in SOURCE1 and SOURCE2. Non-DOS tracks from SOURCE2 will\n"
    "be used where there are more tracks in SOURCE2 than SOURCE1.\n",

    /* COMMAND_HELP */
    "help (?, h): Describe the usage of this program or its commands.\n"
    "usage: help [SUBCOMMAND...]\n",

    /* COMMAND_INFO */
    "info: Print the Extended ADF headers of the specified files.\n"
    "usage: info FILENAME...\n\n"
    "The track type, track size in bytes, track size in bits and the\n"
    "offset of the track data within the Extended ADF file are shown.\n",

    /* COMMAND_MERGE */
    "merge: Merge two Extended ADF images.\n"
    "usage: merge SOURCE1 SOURCE2 DESTINATION\n\n"
    "Copy SOURCE1 to DESTINATION replacing empty tracks from\n"
    "SOURCE1 with the corresponding track from SOURCE2. Where a\n"
    "track is not empty in both SOURCE1 and SOURCE2, the data\n"
    "from SOURCE1 is used.\n\n"
    "The resulting image will have the larger of the number of\n"
    "tracks in SOURCE1 and the number in SOURCE2.\n",

    /* COMMAND_REPLACE */
    "replace (rpl): Replace tracks in an Extended ADF image.\n"
    "usage: replace SOURCE1 SOURCE2 DESTINATION TRACKSPEC...\n\n"
    "Copy SOURCE1 to DESTINATION replacing the specified tracks\n"
    "from SOURCE1 with those from SOURCE2.\n\n"
    "A TRACKSPEC may specify a single track (e.g. \"35\") or a range\n"
    "of tracks (e.g. \"35-45\"). For example:\n\n"
    "rawadf replace src1.adf src2.adf dest.adf 15 57-59 77\n\n"
    "will copy src1.adf to dest.adf replacing tracks 15, 57, 58, 59\n"
    "and 77 with those from src2.adf.\n",

    /* COMMAND_SPLIT */
    "split: Split an Extended ADF image.\n"
    "usage: split SOURCE DESTINATION TRACKSPEC...\n\n"
    "Copy SOURCE to DESTINATION including only the specifed tracks.\n"
    "The resulting image will have empty (zero length) tracks for\n"
    "all tracks other than the specified tracks.\n\n"
    "A TRACKSPEC may specify a single track (e.g. \"74\") or a range\n"
    "of tracks (e.g. \"74-84\"). For example:\n\n"
    "rawadf split src1.adf dest.adf 12 21 38-47\n\n"
    "will create dest.adf containing tracks 12, 21 and 38-47 from\n"
    "src1.adf.\n"
};

enum CommandStatus {
    COMMANDSTATUS_SUCCESS,
    COMMANDSTATUS_FAILURE
};
typedef enum CommandStatus CommandStatus;

enum CommandError {
    COMMANDERROR_NOERROROR,
    COMMANDERROR_UNKNOWNCOMMMAND,
    COMMANDERROR_WRONGNUMBEROFARGS,
    COMMANDERROR_NOMEMORY,
    COMMANDERROR_CANNOTOPENFILE,
    COMMANDERROR_INVALIDFILE,
    COMMANDERROR_MERGEERROR,
    COMMANDERROR_INVALIDTRACKSPEC,
    COMMANDERROR_READERROR,
    COMMANDERROR_SEEKERROR,
    COMMANDERROR_EOFERROR,
    COMMANDERROR_INTERNALERROR
};

enum CommandError command_errno;

const char *COMMANDERROR_MESSAGES[] = {
    /* COMMANDERROR_NOERROR */
    "No error",

    /* COMMANDERROR_UNKOWN_COMMAND */
    "Unknown command",

    /* COMMANDEROR_NUMARGUMENTS */
    "Wrong number of arguments",

    /* COMMANDERROR_NOMEMORY */
    "Memory error (out of memory?)",

    /* COMMANDERROR_CANNOTOPENFILE */
    "Error opening file",

    /* COMMANDERROR_INVALIDFILE */
    "Invalid file error",

    /* COMMANDERROR_MERGEERROR */
    "Error while merging files",

    /* COMMANDERROR_INVALIDTRACKSPEC */
    "Invalid track specification",

    /* COMMANDERROR_READERROR */
    "Error reading from file",

    /* COMMANDERROR_SEEKERROR */
    "Error seeking in file",

    /* COMMANDERROR_EOFERROR */
    "Premature end-of-file",

    /* COMMANDERROR_INTERNALERROR */
    "Internal error"
};

/*
** Function pointer to initialise an EADFTrackSource array from
** the EADFHeaders of two source files, along with user supplied
** data.
*/
typedef CommandStatus (*CommandTrackSourceCallback)(EADFTrackSource *,
    EADFHeader *, EADFHeader *, void *);

Command commandFromString(const char *);
const char *commandNameFromCommand(Command);
void commandPrintErrorWithContext(const char *);
CommandStatus mergeFiles(const char *, const char *, const char *,
    CommandTrackSourceCallback, void *);
CommandStatus splitFile(const char *, const char *,
    CommandTrackSourceCallback, void *);


void usage()
{
    fprintf(stderr, "%s\n", USAGE);
}

void version()
{
    fprintf(stdout, "%s\n", AMI_VERSION + 6);
}

/*
** Convert a four-byte char array in big-endian format to a long
*/
unsigned long longFromBigEndianBytes(const unsigned char nptr[4])
{
    unsigned long result;
    unsigned long temp;

    result = 0;
    temp = nptr[0] & 0xff;
    result = temp << 24;
    temp = nptr[1] & 0xff;
    result = result + (temp << 16);
    temp = nptr[2] & 0xff;
    result = result + (temp << 8);
    temp = nptr[3] & 0xff;
    result = result + temp;

    return result;
}

/*
** Convert a long to a four-byte char array in big-endian format.
*/
void bigEndianBytesFromLong(unsigned char buf[4], const long l)
{
    buf[3] = l & 0xff;
    buf[2] = (l >> 8) & 0xff;
    buf[1] = (l >> 16) & 0xff;
    buf[0] = (l >> 24) & 0xff; 
}

/*
** Initialise an EADFHeader with the contents of a file.
**
** Returns EADFSTATUS_SUCCESS on success. Otherwise EADFSTATUS_FAILURE
** is returned and eadf_errno is set.
*/ 
EADFStatus eadfHeaderInitWithFile(EADFHeader *h, FILE *f)
{
    unsigned char buffer[EADF_MAXTRACKS * EADF_BYTESPERRECORD];
    size_t numRead, fileOffset;
    unsigned long i;

    fileOffset = 0;

    numRead = fread(h->magic, 1, EADF_MAGICLEN, f);
    if (numRead < EADF_MAGICLEN) {
        eadf_errno = EADFERROR_UNKNOWNERROR;
        if (feof(f)) {
            eadf_errno = EADFERROR_EOFERROR;
        } else if (ferror(f)) {
            eadf_errno = EADFERROR_READERROR;
        }
        return EADFSTATUS_FAILURE;
    }

    h->magic[EADF_MAGICLEN] = '\0';
    if (strcmp(h->magic, EADF_MAGIC)) {
        eadf_errno = EADFERROR_WRONGMAGIC;
        return EADFSTATUS_FAILURE;
    }
    fileOffset += numRead;

    numRead = fread(buffer, 1, 4, f);
    if (numRead != 4) {
        eadf_errno = EADFERROR_UNKNOWNERROR;
        if (feof(f)) {
            eadf_errno = EADFERROR_EOFERROR;
        } else if (ferror(f)) {
            eadf_errno = EADFERROR_READERROR;
        }
        return EADFSTATUS_FAILURE;
    }
    fileOffset += numRead;

    h->numTracks = longFromBigEndianBytes(buffer);
    if (h->numTracks > EADF_MAXTRACKS) {
        eadf_errno = EADFERROR_INVALIDNUMTRACKS;
        return EADFSTATUS_FAILURE;
    }

    numRead = fread(buffer, 1, h->numTracks * EADF_BYTESPERRECORD, f);
    if (numRead != h->numTracks * EADF_BYTESPERRECORD) {
        eadf_errno = EADFERROR_UNKNOWNERROR;
        if (feof(f)) {
            eadf_errno = EADFERROR_EOFERROR;
        } else if (ferror(f)) {
            eadf_errno = EADFERROR_READERROR;
        }
        return EADFSTATUS_FAILURE;
    }
    fileOffset += numRead;

    for (i = 0; i < h->numTracks; i++) {
        int bufOffset = i * EADF_BYTESPERRECORD;

        h->trackType[i] = longFromBigEndianBytes(&buffer[bufOffset]);
        if (h->trackType[i] != EADFTRACKTYPE_DOS
            && h->trackType[i] != EADFTRACKTYPE_RAW)
        {
            eadf_errno = EADFERROR_INVALIDTRACKTYPE;
            return EADFSTATUS_FAILURE;
        }

        h->trackSizeBytes[i] = longFromBigEndianBytes(&buffer[bufOffset + 4]);
        h->trackSizeBits[i] = longFromBigEndianBytes(&buffer[bufOffset + 8]);
        h->trackOffset[i] = fileOffset;

        fileOffset += h->trackSizeBytes[i];
    }

    return EADFSTATUS_SUCCESS;
}

/*
** Print an EADF error message, based on the eadf_errno global, to stderr.
**
** If "context" is not NULL and does not begin with '\0' the
** error message is prefixed with the contents of "context"
** followed by a ':' and a space.
*/
void eadfPrintErrorWithContext(const char *context)
{
    if (context != NULL && *context != '\0') {
        fprintf(stderr, "%s: ", context);
    }
    fprintf(stderr, "%s\n", EADFERROR_MESSAGES[eadf_errno]);
}

/*
** Merge two extended ADF files into one.
*/
EADFStatus eadfMergeFiles(EADFHeader *h1, FILE *f1, const char *n1,
    EADFHeader *h2, FILE *f2, const char *n2, FILE *dest,
    const EADFTrackSource trackSources[])
{
    unsigned char buffer[EADF_BUFSIZE], *upto;
    unsigned long numTracks, bufLength;
    unsigned long track;

    strncpy((char *)buffer, EADF_MAGIC, EADF_MAGICLEN);

    numTracks = (h1->numTracks > h2->numTracks) ? h1->numTracks:h2->numTracks;
    bigEndianBytesFromLong(buffer + EADF_MAGICLEN, numTracks);

    upto = buffer + EADF_MAGICLEN + 4;
    for (track = 0; track < numTracks; track++) {
        if (trackSources[track] == EADFTRACKSOURCE_SOURCE1
            && track < h1->numTracks)
        {
            bigEndianBytesFromLong(upto, h1->trackType[track]);
            bigEndianBytesFromLong(upto + 4, h1->trackSizeBytes[track]);
            bigEndianBytesFromLong(upto + 8, h1->trackSizeBits[track]);
        } else if (trackSources[track] == EADFTRACKSOURCE_SOURCE2
                   && track < h2->numTracks)
        {
            bigEndianBytesFromLong(upto, h2->trackType[track]);
            bigEndianBytesFromLong(upto + 4, h2->trackSizeBytes[track]);
            bigEndianBytesFromLong(upto + 8, h2->trackSizeBits[track]);
        } else {
            bigEndianBytesFromLong(upto, EADFTRACKTYPE_RAW);
            bigEndianBytesFromLong(upto + 4, 0);
            bigEndianBytesFromLong(upto + 8, 0);
        }
        upto += EADF_BYTESPERRECORD;

        bufLength = upto - buffer;
        if (bufLength > (EADF_BUFSIZE - EADF_BYTESPERRECORD)) {
            if (fwrite(buffer, 1, bufLength, dest) < bufLength) {
                eadf_errno = EADFERROR_WRITEERROR;
                return EADFSTATUS_FAILURE;
            }
            upto = buffer;
        }
    }
    
    bufLength = upto - buffer;
    if (fwrite(buffer, 1, bufLength, dest) < bufLength) {
        eadf_errno = EADFERROR_WRITEERROR;
        return EADFSTATUS_FAILURE;
    }

    for (track = 0; track < numTracks; track++) {
        FILE *src;
        long numBytes;

        if (trackSources[track] == EADFTRACKSOURCE_SOURCE1)
        {
            src = f1;
            numBytes = h1->trackSizeBytes[track];
            if (fseek(f1, h1->trackOffset[track], SEEK_SET) < 0) {
                perror(n1);
                eadf_errno = EADFERROR_SEEKERROR;
                return EADFSTATUS_FAILURE;
            }
        } else if (trackSources[track] == EADFTRACKSOURCE_SOURCE2) {
            src = f2;
            numBytes = h2->trackSizeBytes[track];
            if (fseek(f2, h2->trackOffset[track], SEEK_SET) < 0) {
                perror(n2);
                eadf_errno = EADFERROR_SEEKERROR;
                return EADFSTATUS_FAILURE;
            }
        } else { /* trackSources[track] == EADFTRACKSOURCE_NONE */
            continue;
        }

        for (; numBytes > 0; numBytes -= EADF_BUFSIZE) {
            unsigned int count;
            count = (numBytes > EADF_BUFSIZE) ? EADF_BUFSIZE : numBytes;

            if (fread(buffer, 1, count, src) < count) {
                eadf_errno = EADFERROR_UNKNOWNERROR;
                if (feof(src)) {
                    eadf_errno = EADFERROR_EOFERROR;
                } else if (ferror(src)) {
                    eadf_errno = EADFERROR_READERROR;
                }
                return EADFSTATUS_FAILURE;
            }

            if (fwrite(buffer, 1, count, dest) < count) {
                eadf_errno = EADFERROR_WRITEERROR;
                return EADFSTATUS_FAILURE;
            }
        }
    }

    return EADFSTATUS_SUCCESS;
}

EADFStatus eadfSplitFile(EADFHeader *h, FILE *f, const char *n, FILE *dest,
    const EADFTrackSource *trackSources)
{
    unsigned char buffer[EADF_BUFSIZE], *upto;
    unsigned long track, bufLength;

    strncpy((char *)buffer, EADF_MAGIC, EADF_MAGICLEN);

    bigEndianBytesFromLong(buffer + EADF_MAGICLEN, h->numTracks);

    upto = buffer + EADF_MAGICLEN + 4;
    for (track = 0; track < h->numTracks; track++) {
        if (trackSources[track] == EADFTRACKSOURCE_SOURCE1)
        {
            bigEndianBytesFromLong(upto, h->trackType[track]);
            bigEndianBytesFromLong(upto + 4, h->trackSizeBytes[track]);
            bigEndianBytesFromLong(upto + 8, h->trackSizeBits[track]);
        } else {
            bigEndianBytesFromLong(upto, EADFTRACKTYPE_RAW);
            bigEndianBytesFromLong(upto + 4, 0);
            bigEndianBytesFromLong(upto + 8, 0);
        }
        upto += EADF_BYTESPERRECORD;

        bufLength = upto - buffer;
        if (bufLength > (EADF_BUFSIZE - EADF_BYTESPERRECORD)) {
            if (fwrite(buffer, 1, bufLength, dest) < bufLength) {
                eadf_errno = EADFERROR_WRITEERROR;
                return EADFSTATUS_FAILURE;
            }
            upto = buffer;
        }
    }

    bufLength = upto - buffer;
    if (fwrite(buffer, 1, bufLength, dest) < bufLength) {
        eadf_errno = EADFERROR_WRITEERROR;
        return EADFSTATUS_FAILURE;
    }

    for (track = 0; track < h->numTracks; track++) {
        long numBytes;

        if (trackSources[track] != EADFTRACKSOURCE_SOURCE1)
            continue;

        numBytes = h->trackSizeBytes[track];
        if (fseek(f, h->trackOffset[track], SEEK_SET) < 0) {
            perror(n);
            eadf_errno = EADFERROR_SEEKERROR;
            return EADFSTATUS_FAILURE;
        }

        for (; numBytes > 0; numBytes -= EADF_BUFSIZE) {
            unsigned int count;
            count = (numBytes > EADF_BUFSIZE) ? EADF_BUFSIZE : numBytes;

            if (fread(buffer, 1, count, f) < count) {
                eadf_errno = EADFERROR_UNKNOWNERROR;
                if (feof(f)) {
                    eadf_errno = EADFERROR_EOFERROR;
                } else if (ferror(f)) {
                    eadf_errno = EADFERROR_READERROR;
                }
                return EADFSTATUS_FAILURE;
            }

            if (fwrite(buffer, 1, count, dest) < count) {
                eadf_errno = EADFERROR_WRITEERROR;
                return EADFSTATUS_FAILURE;
            }
        }
    }

    return EADFSTATUS_SUCCESS;
}
/*
** End of EADF stuff
*/

/*
** Compare the specified track of two extended ADF files.
**
** Sets *result to zero if the tracks are equal, or non-zero otherwise.
*/
CommandStatus compareTracks(int *result,
    EADFHeader *h1, FILE *f1, const char *n1,
    EADFHeader *h2, FILE *f2, const char *n2,
    unsigned long track)
{
    unsigned char buf1[COMMAND_BUFSIZE], buf2[COMMAND_BUFSIZE];
    long numBytes;

    if (track > h1->numTracks
        || track > h2->numTracks
        || h1->trackType[track] != h2->trackType[track]
        || h1->trackSizeBytes[track] != h2->trackSizeBytes[track]
        || h1->trackSizeBits[track] != h2->trackSizeBits[track])
    {
        *result = 1;
        return COMMANDSTATUS_SUCCESS;
    }

    if (fseek(f1, h1->trackOffset[track], SEEK_SET) < 0) {
        perror(n1);
        command_errno = COMMANDERROR_SEEKERROR;
        return COMMANDSTATUS_FAILURE;
    }

    if (fseek(f2, h2->trackOffset[track], SEEK_SET) < 0) {
        perror(n2);
        command_errno = COMMANDERROR_SEEKERROR;
        return COMMANDSTATUS_FAILURE;
    }

    numBytes = h1->trackSizeBytes[track];
    for (; numBytes > 0; numBytes -= COMMAND_BUFSIZE) {
        unsigned int count;
        count = (numBytes > COMMAND_BUFSIZE) ? COMMAND_BUFSIZE : numBytes;

        if (fread(buf1, 1, count, f1) != count) {
            command_errno = COMMANDERROR_INVALIDFILE;
            if (feof(f1)) {
                command_errno = COMMANDERROR_EOFERROR;
            } else if (ferror(f1)) {
                command_errno = COMMANDERROR_READERROR;
            }
            return COMMANDSTATUS_FAILURE;
        }

        if (fread(buf2, 1, count, f2) != count) {
            command_errno = COMMANDERROR_INVALIDFILE;
            if (feof(f2)) {
                command_errno = COMMANDERROR_EOFERROR;
            } else if (ferror(f2)) {
                command_errno = COMMANDERROR_READERROR;
            }
            return COMMANDSTATUS_FAILURE;
        }

        if (memcmp(buf1, buf2, count) != 0) {
            *result = 1;
            return COMMANDSTATUS_SUCCESS;
        }
    }

    *result = 0;
    return COMMANDSTATUS_SUCCESS;
}

CommandStatus printComparison(EADFHeader *h1, FILE *f1, const char *n1,
    EADFHeader *h2, FILE *f2, const char *n2)
{
    unsigned long numTracks;
    unsigned long track;

    fprintf(stdout, "       SOURCE1             SOURCE2\n"
        "Track  Type Bytes   Bits   Type Bytes   Bits D\n");

    numTracks = (h1->numTracks > h2->numTracks) ? h1->numTracks:h2->numTracks;
    for (track = 0; track < numTracks; track++) {
        EADFTrackType type1 = EADFTRACKTYPE_RAW, type2 = EADFTRACKTYPE_RAW;
        long bytes1 = 0, bits1 = 0, bytes2 = 0, bits2 = 0;
        CommandStatus status;
        char diff = '*';
        int cmp;

        if (track < h1->numTracks) {
            type1 = h1->trackType[track];
            bytes1 = h1->trackSizeBytes[track];
            bits1 = h1->trackSizeBits[track];
        }

        if (track < h2->numTracks) {
            type2 = h2->trackType[track];
            bytes2 = h2->trackSizeBytes[track];
            bits2 = h2->trackSizeBits[track];
        }

        status = compareTracks(&cmp, h1, f1, n1, h2, f2, n2, track);
        if (status != COMMANDSTATUS_SUCCESS) {
            return COMMANDSTATUS_FAILURE;
        }

        if (cmp == 0) {
            diff = ' ';
        }

        fprintf(stdout, "%5lu  %4s %5ld %6ld   %4s %5ld %6ld %c\n",
            track,
            EADFTRACKTYPE_NAMES[type1], bytes1, bits1,
            EADFTRACKTYPE_NAMES[type2], bytes2, bits2,
            diff);
    }

    return COMMANDSTATUS_SUCCESS;
}

CommandStatus executeCompareCommand(int argc, char **argv)
{
    EADFHeader *h;
    FILE *f1, *f2;
    CommandStatus status;

    if (argc != 4) {
        command_errno = COMMANDERROR_WRONGNUMBEROFARGS;
        return COMMANDSTATUS_FAILURE;
    }

    if ((h = malloc(2 * sizeof(EADFHeader))) == NULL) {
        command_errno = COMMANDERROR_NOMEMORY;
        return COMMANDSTATUS_FAILURE;
    }

    if ((f1 = fopen(argv[2], "rb")) == NULL) {
        perror(argv[2]);
        free(h);
        command_errno = COMMANDERROR_CANNOTOPENFILE;
        return COMMANDSTATUS_FAILURE;
    }

    if (eadfHeaderInitWithFile(h, f1) != EADFSTATUS_SUCCESS) {
        free(h);
        fclose(f1);
        eadfPrintErrorWithContext(argv[2]);
        command_errno = COMMANDERROR_INVALIDFILE;
        return COMMANDSTATUS_FAILURE;
    }

    if ((f2 = fopen(argv[3], "rb")) == NULL) {
        perror(argv[3]);
        free(h);
        fclose(f1);
        command_errno = COMMANDERROR_CANNOTOPENFILE;
        return COMMANDSTATUS_FAILURE;
    }

    if (eadfHeaderInitWithFile(h + 1, f2) != EADFSTATUS_SUCCESS) {
        free(h);
        fclose(f1);
        fclose(f2);
        eadfPrintErrorWithContext(argv[3]);
        command_errno = COMMANDERROR_INVALIDFILE;
        return COMMANDSTATUS_FAILURE;
    }
 
    status = printComparison(h, f1, argv[2], h + 1, f2, argv[3]);

    free(h);
    fclose(f1);
    fclose(f2);

    return status;
}

/*
** Merge two extended ADF files.
**
** Arguments "src1" and "src2" are the names of the two source files,
** "dest" is the name of the destination file and "callback" is a
** pointer to a function which will populate the trackSources array.
** The "data" argument is passed unchanged to "callback".
*/
CommandStatus mergeFiles(const char *src1, const char *src2,
    const char *dest, CommandTrackSourceCallback callback, void *data)
{
    FILE *f1, *f2, *f3;
    EADFHeader *h1, *h2;
    EADFTrackSource trackSources[EADF_MAXTRACKS];
    EADFStatus status;

    if ((h1 = malloc(2 * sizeof(EADFHeader))) == NULL) {
        command_errno = COMMANDERROR_NOMEMORY;
        return COMMANDSTATUS_FAILURE;
    }
    h2 = h1 + 1;

    if ((f1 = fopen(src1, "rb")) == NULL) {
        perror(src1);
        free(h1);
        command_errno = COMMANDERROR_CANNOTOPENFILE;
        return COMMANDSTATUS_FAILURE;
    }

    if ((f2 = fopen(src2, "rb")) == NULL) {
        perror(src2);
        free(h1);
        fclose(f1);
        command_errno = COMMANDERROR_CANNOTOPENFILE;
        return COMMANDSTATUS_FAILURE;
    }

    if ((f3 = fopen(dest, "wb")) == NULL) {
        perror(dest);
        free(h1);
        fclose(f1);
        fclose(f2);
        command_errno = COMMANDERROR_CANNOTOPENFILE;
        return COMMANDSTATUS_FAILURE;
    }

    if (eadfHeaderInitWithFile(h1, f1) != EADFSTATUS_SUCCESS) {
        eadfPrintErrorWithContext(src1);
        free(h1);
        fclose(f1);
        fclose(f2);
        fclose(f3);
        command_errno = COMMANDERROR_INVALIDFILE;
        return COMMANDSTATUS_FAILURE;
    }

    if (eadfHeaderInitWithFile(h2, f2) != EADFSTATUS_SUCCESS) {
        eadfPrintErrorWithContext(src2);
        free(h1);
        fclose(f1);
        fclose(f2);
        fclose(f3);
        command_errno = COMMANDERROR_INVALIDFILE;
        return COMMANDSTATUS_FAILURE;
    }

    if (callback(trackSources, h1, h2, data) == COMMANDSTATUS_FAILURE) {
        free(h1);
        fclose(f1);
        fclose(f2);
        fclose(f3);
        return COMMANDSTATUS_FAILURE;
    }

    status = eadfMergeFiles(h1, f1, src1, h2, f2, src2, f3, trackSources);
    free(h1);
    fclose(f1);
    fclose(f2);
    fclose(f3);

    if (status != EADFSTATUS_SUCCESS) {
        eadfPrintErrorWithContext(NULL);
        command_errno = COMMANDERROR_MERGEERROR;
        return COMMANDSTATUS_FAILURE;
    }

    return COMMANDSTATUS_SUCCESS;
}

CommandStatus splitFile(const char *src, const char *dest,
    CommandTrackSourceCallback callback, void *data)
{
    FILE *f1, *f2;
    EADFHeader *h;
    EADFTrackSource trackSources[EADF_MAXTRACKS];
    EADFStatus status;

    if ((h = malloc(sizeof(EADFHeader))) == NULL) {
        command_errno = COMMANDERROR_NOMEMORY;
        return COMMANDSTATUS_FAILURE;
    }

    if ((f1 = fopen(src, "rb")) == NULL) {
        perror(src);
        free(h);
        command_errno = COMMANDERROR_CANNOTOPENFILE;
        return COMMANDSTATUS_FAILURE;
    }

    if ((f2 = fopen(dest, "wb")) == NULL) {
        perror(dest);
        free(h);
        fclose(f1);
        command_errno = COMMANDERROR_CANNOTOPENFILE;
        return COMMANDSTATUS_FAILURE;
    }

    if (eadfHeaderInitWithFile(h, f1) != EADFSTATUS_SUCCESS) {
        eadfPrintErrorWithContext(src);
        free(h);
        fclose(f1);
        fclose(f2);
        command_errno = COMMANDERROR_INVALIDFILE;
        return COMMANDSTATUS_FAILURE;
    }

    if (callback(trackSources, h, NULL, data) == COMMANDSTATUS_FAILURE) {
        free(h);
        fclose(f1);
        fclose(f2);
        return COMMANDSTATUS_FAILURE;
    }

    status = eadfSplitFile(h, f1, src, f2, trackSources);
    free(h);
    fclose(f1);
    fclose(f2);

    if (status != EADFSTATUS_SUCCESS) {
        eadfPrintErrorWithContext(NULL);
        command_errno = COMMANDERROR_MERGEERROR;
        return COMMANDSTATUS_FAILURE;
    }

    return COMMANDSTATUS_SUCCESS;
}

/*
** Populate an EADFTrackSource array using source1 for each track unless
** it is a non-DOS track and the corresponding track from source2 is
** a DOS track.
*/
CommandStatus dosMergeTrackSourceCallback(EADFTrackSource *trackSources,
    EADFHeader *h1, EADFHeader *h2, void *data)
{
    unsigned long track, numTracks;

    (void) data; /* prevent compiler issuing unused variable warnings */

    numTracks = (h1->numTracks > h2->numTracks) ? h1->numTracks:h2->numTracks;
    for (track = 0; track < numTracks; track++) {
        if (track >= h2->numTracks
            || (track < h1->numTracks
                && (h1->trackType[track] == EADFTRACKTYPE_DOS
                    || h2->trackType[track] == EADFTRACKTYPE_RAW)))
        {
            trackSources[track] = EADFTRACKSOURCE_SOURCE1;
        } else {
            trackSources[track] = EADFTRACKSOURCE_SOURCE2;
        }
    }

    return COMMANDSTATUS_SUCCESS;
}

CommandStatus executeDosMergeCommand(int argc, char **argv)
{
    if (argc != 5) {
        command_errno = COMMANDERROR_WRONGNUMBEROFARGS;
        return COMMANDSTATUS_FAILURE;
    }

    return mergeFiles(argv[2], argv[3], argv[4], dosMergeTrackSourceCallback,
        NULL);
}

void printHelpForCommand(const char *cmd)
{
    Command which = commandFromString(cmd);

    if (which == COMMAND_UNKNOWN) {
        commandPrintErrorWithContext(cmd);
        return;
    }

    fprintf(stdout, "%s\n", COMMAND_HELPTEXT[which]);
}

CommandStatus executeHelpCommand(int argc, char **argv)
{
    int i, j;

    if (argc > 2) {
        printHelpForCommand(argv[2]);

        for (i = 3; i < argc; i++) {
            fprintf(stdout, "\n");
            printHelpForCommand(argv[i]);
        }
        return COMMANDSTATUS_SUCCESS;
    }

    fprintf(stdout, "%s\n   %s", COMMAND_BASICHELP, COMMAND_ALIASES[0]);
    for (i = 1, j = 0; i < COMMAND_NUMALIASES; i++) {
        if (COMMAND_ALIASMAP[i] == COMMAND_ALIASMAP[j])
            continue;

        if (j == i-1) {
            j = i;
            fprintf(stdout, "\n   %s", COMMAND_ALIASES[j]);
            continue;
        }

        fprintf(stdout, " (%s", COMMAND_ALIASES[j+1]);
        for (j = j+2; j < i; j++) {
            fprintf(stdout, ", %s", COMMAND_ALIASES[j]);
        }
        fprintf(stdout, ")\n   %s", COMMAND_ALIASES[j]);
    }
    fprintf(stdout, "\n");

    return COMMANDSTATUS_SUCCESS;
}

void displayInfo(EADFHeader *h, const char *name)
{
    unsigned int track;

    fprintf(stdout, "File name: %s\nNumber of tracks: %lu\n"
        "Track Cyl Side Type  Length    Bits  Offset\n", name, h->numTracks);

    for (track = 0; track < h->numTracks; track++) {
        fprintf(stdout, "%5u %3u %4u %4s %7lu %7lu %7lu\n",
            track,
            track / 2,
            (track % 2) + 1,
            EADFTRACKTYPE_NAMES[h->trackType[track]],
            h->trackSizeBytes[track],
            h->trackSizeBits[track],
            h->trackOffset[track]);
    }
}

CommandStatus executeInfoCommand(int argc, char **argv)
{
    EADFHeader *h;
    int i;
    CommandStatus status = COMMANDSTATUS_SUCCESS;

    if (argc < 3) {
        command_errno = COMMANDERROR_WRONGNUMBEROFARGS;
        return COMMANDSTATUS_FAILURE;
    }

    if ((h = malloc(sizeof(EADFHeader))) == NULL) {
        command_errno = COMMANDERROR_NOMEMORY;
        return COMMANDSTATUS_FAILURE;
    }

    for (i = 2; i < argc; i++) {
        FILE *f;

        if ((f = fopen(argv[i], "rb")) == NULL) {
            perror(argv[i]);
            command_errno = COMMANDERROR_CANNOTOPENFILE;
            status = COMMANDSTATUS_FAILURE;
            continue;
        }

        if (eadfHeaderInitWithFile(h, f) != EADFSTATUS_SUCCESS) {
            eadfPrintErrorWithContext(argv[i]);
            command_errno = COMMANDERROR_INVALIDFILE;
            status = COMMANDSTATUS_FAILURE;
        } else {
            displayInfo(h, argv[i]);
        }

        fclose(f);
    }

    free(h);
    return status;
}

/*
** Populate an EADFTrackSource array using source1 for each track unless
** it is an empty (zero bytes in length) track and the corresponding track
** in source2 is non-empty.
*/
CommandStatus mergeTrackSourceCallback(EADFTrackSource *trackSources,
    EADFHeader *h1, EADFHeader *h2, void *data)
{
    unsigned long track, numTracks;

    (void) data; /* prevent compiler issuing unused variable warnings */

    numTracks = (h1->numTracks > h2->numTracks) ? h1->numTracks:h2->numTracks;
    for (track = 0; track < numTracks; track++) {
        if (track >= h2->numTracks
            || (track < h1->numTracks && h1->trackSizeBytes[track] > 0)
            || (track < h1->numTracks && h2->trackSizeBytes[track] == 0))
        {
            trackSources[track] = EADFTRACKSOURCE_SOURCE1;
        } else {
            trackSources[track] = EADFTRACKSOURCE_SOURCE2;
        }
    }

    return COMMANDSTATUS_SUCCESS;
}

CommandStatus executeMergeCommand(int argc, char **argv)
{
    if (argc != 5) {
        command_errno = COMMANDERROR_WRONGNUMBEROFARGS;
        return COMMANDSTATUS_FAILURE;
    }

    return mergeFiles(argv[2], argv[3], argv[4], mergeTrackSourceCallback,
        NULL);
}

/*
** Populate an EADFTrackSource array using the supplied data (which should
** be a pointer to an EADFTrackSource array).
*/
CommandStatus replaceTrackSourceCallback(EADFTrackSource *trackSources,
    EADFHeader *h1, EADFHeader *h2, void *data)
{
    long track, numTracks;
    EADFTrackSource *replacements = (EADFTrackSource *)data;

    numTracks = (h1->numTracks > h2->numTracks) ? h1->numTracks:h2->numTracks;
    for (track = 0; track < numTracks; track++) {
        trackSources[track] = replacements[track];
    }

    return COMMANDSTATUS_SUCCESS;
}

CommandStatus parseTrackSpecs(int argc, char **argv, int start,
    EADFTrackSource *trackSources, EADFTrackSource value)
{
    int i, track;

    for (i = start; i < argc; i++) {
        char *endptr;
        long val1, val2;

        val1 = strtol(argv[i], &endptr, 10);
        if ((endptr[0] != '\0' && endptr[0] != '-')
            || val1 < 0
            || val1 >= EADF_MAXTRACKS)
        {
            command_errno = COMMANDERROR_INVALIDTRACKSPEC;
            return COMMANDSTATUS_FAILURE;
        }

        if (endptr[0] == '\0') {
            trackSources[val1] = value;
            continue;
        }

        val2 = strtol(endptr + 1, &endptr, 10);
        if (endptr[0] != '\0' || val2 < val1 || val2 >= EADF_MAXTRACKS) {
            command_errno = COMMANDERROR_INVALIDTRACKSPEC;
            return COMMANDSTATUS_FAILURE;
        }
 
        for (track = val1; track <= val2; track++) {
            trackSources[track] = value;
        }
    }

    return COMMANDSTATUS_SUCCESS;
}

CommandStatus executeReplaceCommand(int argc, char **argv)
{
    EADFTrackSource replacements[EADF_MAXTRACKS];
    CommandStatus st;
    long track;

    if (argc < 6) {
        command_errno = COMMANDERROR_WRONGNUMBEROFARGS;
        return COMMANDSTATUS_FAILURE;
    }

    for (track = 0; track < EADF_MAXTRACKS; track++) {
        replacements[track] = EADFTRACKSOURCE_SOURCE1;
    }

    st = parseTrackSpecs(argc, argv, 5, replacements, EADFTRACKSOURCE_SOURCE2);
    if (st == COMMANDSTATUS_FAILURE)
        return COMMANDSTATUS_FAILURE;

    return mergeFiles(argv[2], argv[3], argv[4], replaceTrackSourceCallback,
        (void *)replacements);
}

/*
** Populate an EADFTrackSource array using the supplied data (which should
** be a pointer to an EADFTrackSource array).
*/
CommandStatus splitTrackSourceCallback(EADFTrackSource *trackSources,
    EADFHeader *h1, EADFHeader *h2, void *data)
{
    unsigned long track;
    EADFTrackSource *specified = (EADFTrackSource *)data;

    (void) h2; /* prevent compiler issuing unused variable warning */

    for (track = 0; track < h1->numTracks; track++) {
        trackSources[track] = specified[track];
    }

    return COMMANDSTATUS_SUCCESS;
}

CommandStatus executeSplitCommand(int argc, char **argv)
{
    EADFTrackSource specified[EADF_MAXTRACKS];
    CommandStatus st;
    long track;

    if (argc < 5) {
        command_errno = COMMANDERROR_WRONGNUMBEROFARGS;
        return COMMANDSTATUS_FAILURE;
    }

    for (track = 0; track < EADF_MAXTRACKS; track++) {
        specified[track] = EADFTRACKSOURCE_NONE;
    }

    st = parseTrackSpecs(argc, argv, 4, specified, EADFTRACKSOURCE_SOURCE1);
    if (st == COMMANDSTATUS_FAILURE)
        return COMMANDSTATUS_FAILURE;

    return splitFile(argv[2], argv[3], splitTrackSourceCallback,
        (void *)specified);
}

Command commandFromString(const char *s)
{
    int i;

    for (i = 0; i < COMMAND_NUMALIASES; i++) { 
        if (!strcmp(s, COMMAND_ALIASES[i])) {
            return COMMAND_ALIASMAP[i];
        }
    }

    command_errno = COMMANDERROR_UNKNOWNCOMMMAND;
    return COMMAND_UNKNOWN;
}

const char *commandNameFromCommand(Command c)
{
    return COMMAND_NAMES[c];
}

/*
** Print a Command error message, based on the command_errno global, to stderr.
**
** If "context" is not NULL and does not begin with '\0' the
** error message is prefixed with the contents of "context"
** followed by a ':' and a space.
*/
void commandPrintErrorWithContext(const char *context)
{
    if (context != NULL && *context != '\0') {
        fprintf(stderr, "%s: ", context);
    }
    fprintf(stderr, "%s\n", COMMANDERROR_MESSAGES[command_errno]);
}

CommandStatus dispatchCommand(Command which, int argc, char **argv)
{
    switch (which) {
    case COMMAND_COMPARE:
        return executeCompareCommand(argc, argv);
        break;
    case COMMAND_DOSMERGE:
        return executeDosMergeCommand(argc, argv);
        break;
    case COMMAND_HELP:
        return executeHelpCommand(argc, argv);
        break;
    case COMMAND_INFO:
        return executeInfoCommand(argc, argv);
        break;
    case COMMAND_MERGE:
        return executeMergeCommand(argc, argv);
        break;
    case COMMAND_REPLACE:
        return executeReplaceCommand(argc, argv);
        break;
    case COMMAND_SPLIT:
        return executeSplitCommand(argc, argv);
        break;
    case COMMAND_UNKNOWN:
        command_errno = COMMANDERROR_UNKNOWNCOMMMAND;
        return COMMANDSTATUS_FAILURE;
        break;
    };

    /* Should not reach here */
    command_errno = COMMANDERROR_INTERNALERROR;
    return COMMANDSTATUS_FAILURE;   
}

int main(int argc, char **argv)
{
    Command c;

    if (argc < 2) {
        usage();
        return EXIT_FAILURE;
    }

    if (argv[1][0] == '-') {
        if (!strcmp(argv[1], "--version")) {
            version();
            return EXIT_SUCCESS;
        }

        fprintf(stderr, "invalid option: %s\n", argv[1]);
        usage();
        return EXIT_FAILURE;
    }

    if ((c = commandFromString(argv[1])) == COMMAND_UNKNOWN) {
        commandPrintErrorWithContext(argv[1]);
        usage();
        return EXIT_FAILURE;
    }
    
    if (dispatchCommand(c, argc, argv) == COMMANDSTATUS_FAILURE) {
        commandPrintErrorWithContext(commandNameFromCommand(c));
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
