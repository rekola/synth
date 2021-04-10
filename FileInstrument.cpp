#include "FileInstrument.h"

#include <sndfile.h>
#include <cstring>

using namespace std;

#define BLOCK_SIZE 4096

void
FileInstrument::openFile() {
  SNDFILE * infile = 0;
  SF_INFO sfinfo;

  memset(&sfinfo, 0, sizeof(sfinfo));

  if ((infile = sf_open(filename.c_str(), SFM_READ, &sfinfo)) == NULL) {
    // printf ("Not able to open input file %s.\n", filename.c_str());
    // puts (sf_strerror (NULL)) ;
    return;
  }

  // printf("# Channels %d, Sample rate %d\n", sfinfo.channels, sfinfo.samplerate) ;
  int channels = sfinfo.channels;
  
  float * buf = (float *)malloc(BLOCK_SIZE * sizeof (float));
  if (buf == NULL) {
    // printf ("Error : Out of memory.\n\n") ;
    return;
  }
  
  sf_count_t frames = BLOCK_SIZE / channels;

  int k, readcount;
  while ((readcount = (int) sf_readf_float (infile, buf, frames)) > 0) {
    for (k = 0 ; k < readcount; k++) {
      samples.push_back(buf[k * channels + 0]);
    }
  }

  free(buf);
  sf_close(infile);
}
