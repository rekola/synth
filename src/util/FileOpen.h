#ifndef _FILEOPEN_H_
#define _FILEOPEN_H_

#include <cstdio>
#include <string>

#include <sndfile.h>

// Opens files by UTF-8 path. Paths in songs and on the command line are
// UTF-8; Windows' narrow fopen()/sf_open() read them as the ANSI code page
// instead, so there the path goes through the wide-character calls.
FILE * openFile(const std::string & path, const char * mode);
SNDFILE * openSoundFile(const std::string & path, int mode, SF_INFO * info);

#endif
