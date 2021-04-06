//4ksyna.c -- 4k introsynth
/*TODO:
        -modulation
        -note slide
        -better exponential(?) ADSR
        -optimization
*/

#define USESDL
#define _USE_MATH_DEFINES
#undef __STRICT_ANSI__

#include <math.h>

#include <assert.h>

// #define MEASURETIME

// #define USEFILTER
// #define USEDELAY

#include <stdio.h>
#include <stdlib.h>
#ifdef USESDL
#include <SDL/SDL.h>
#endif
#ifdef MEASURETIME
#include <sys/time.h>
#include <time.h>
#endif

#include <math.h>

#include "4ksyna.h"
#ifdef USESDL// || USESTANDARDMAIN
#include "track.h"
#endif

#define PATTLEN 32
#define NOTEDOMAIN (float)1/4
#define MAXPATT 50
#define MAXTRK 32
#define MAXTRKPTRN 100

#define VOLGAIN 1.0f
#define ACCENTAMT 1.5f
#define WAVESIZE 1024
#define MIDINOTES 128
#define MAXDELAYSAMPLES 44100 * 5
#define MAXOUTBUF 44100

static float waves[4][WAVESIZE], freqtab[MIDINOTES], freq[MAXPATT], fphase[MAXPATT], fscaler, gvol, mastervol;
static unsigned char bpm, ptrncnt, trkcnt;
static int sinterval, samplepos, ptrnpos, trkpos, adsrstate[MAXPATT], adsrpos[MAXPATT], acc[MAXPATT], srate, trkmaxlen, loops;
static float bufl[MAXOUTBUF], bufr[MAXOUTBUF];
//global delay parameters
static int delay1, delay2;
static float fd1, fd2, delaymix1, delaymix2;

#ifdef USEFILTER
static float in1[MAXPATT], in2[MAXPATT], in3[MAXPATT], in4[MAXPATT];
static float out1[MAXPATT], out2[MAXPATT], out3[MAXPATT], out4[MAXPATT];
#endif
#ifdef USEDELAY
static int delc1[MAXPATT], delc2[MAXPATT];
static float delaybuf1[MAXPATT][MAXDELAYSAMPLES], delaybuf2[MAXPATT][MAXDELAYSAMPLES];
#endif


static struct patts
{
        unsigned char type;
        int a, d, r;
        float s;
        float vol;
        unsigned char flags;
        float detune;
        float pan;
        float fcut, fres;
        unsigned char notes[PATTLEN];
} patt[MAXPATT];

static struct tracks
{
        int pattern[MAXTRKPTRN];
} trk[MAXTRK];

short synthinit(int samplerate, unsigned char *track)
{
        int i, j;
        fscaler = (float)WAVESIZE / samplerate;
	float k = 1.059463094359f;	// 12th root of 2
	float a = 8.1757989156f;	// C

	for (i = 0; i < MIDINOTES; i++) {
		freqtab[i] = (float)a;
		a *= k;
	}

        for (i = 0; i < WAVESIZE; i++) {
                waves[SINE][i] = sinf(i * 2.0 * M_PI / (float)WAVESIZE);
                waves[SAW][i] = -1.0 + fmod(1.0 + 2.0 * i / (float)WAVESIZE, 2.0);
                waves[SQUARE][i] = (i < WAVESIZE / 2) ? -1.0 : 1.0;
                waves[NOISE][i] = ((float)rand() / RAND_MAX) * 2.0 - 1.0;
        }

        bpm = *track++;
        mastervol = (float)(*track++) / 127;
        delay1 = (int)(MAXDELAYSAMPLES * ((float)(*track++) / 255));
        delay2 = (int)(MAXDELAYSAMPLES * ((float)(*track++) / 255));
        fd1 = (float)(*track++) / 255;
        fd2 = (float)(*track++) / 255;
        delaymix1 = (float)(*track++) / 255;
        delaymix2 = (float)(*track++) / 255;

        ptrncnt = *track++;
        for (i = 0; i < ptrncnt; i++) {
#ifdef USEFILTER
                in1[i] = in2[i] = in3[i] = in4[i] = 0;
                out1[i] = out2[i] = out3[i] = out4[i] = 0;
#endif

                for (j = 0; j < PATTLEN; j++) {
                        patt[i].notes[j] = 0;
                }
                patt[i].type = *track++;
                patt[i].a = (*track++) * 44100 * 5 / 255;
                patt[i].d = (*track++) * 44100 * 5 / 255;
                patt[i].s = (float)(*track++) / 255;
                patt[i].r = (*track++) * 44100 * 5 / 255;
                patt[i].vol = (float)(*track++) / 128;
                patt[i].flags = *track++;
                patt[i].detune = (float)((*track++) - 127) / 512;
                patt[i].pan = (float)(*track++) / 255;
                patt[i].fcut = (float)(*track++) / 255;
                patt[i].fres = (float)(*track++) / 63;

                j = 0;
                while (1) {
                        unsigned char val = *track++;
                        if (val == 255) break;
                        patt[i].notes[j++] = val;
                }
        }
        trkcnt = *track++;
        trkmaxlen = 0;
        for (i = 0; i < trkcnt; i++) {
                for (j = 0; j < MAXTRKPTRN; j++) {
                        trk[i].pattern[j] = 255;
                }
                j = 0;
                int tmp = 0;
                while (1) {
                        unsigned char val = *track++;
                        trk[i].pattern[j++] = val;
                        tmp++;
                        if (val == 255) break;
                }
                if (tmp > trkmaxlen) trkmaxlen = tmp;
        }

        float tnote = (float)60 / bpm * NOTEDOMAIN * 2;
        sinterval = (int)(tnote * samplerate);
        srate = samplerate;
        samplepos = 0;
        ptrnpos = 0;
        trkpos = 0;
        loops = 0;
        //gvol = 1.0 / trkcnt;
        gvol = 1.0;
	return 1;
}

#ifdef USEFILTER
inline float filtersample(char type, int i, float fc, float res, float input)
{
        float si = input;
        float f = fc * 1.16;
        float ff = f * f;
        float fb = res * (1.0 - 0.15 * ff);
        f = 1 - f;

        input -= out4[i] * fb;
        input *= 0.35013 * ff * ff;
        out1[i] = input + 0.3 * in1[i] + f * out1[i]; // Pole 1
        in1[i]  = input;
        out2[i] = out1[i] + 0.3 * in2[i] + f * out2[i];  // Pole 2
        in2[i] = out1[i];
        out3[i] = out2[i] + 0.3 * in3[i] + f * out3[i];  // Pole 3
        in3[i]  = out2[i];
        out4[i] = out3[i] + 0.3 * in4[i] + f * out4[i];  // Pole 4
        in4[i]  = out3[i];

        if (!type) return out4[i];
        else return si - out4[i];
}
#endif

#ifdef USEDELAY
inline void delaysample(int i, float *in1, float *in2)
{
	float x, y;

      	x = *in1;
      	y = delaybuf1[i][delc1[i]];

      	delaybuf1[i][delc1[i]++] = x + y * fd1;
      	if (delc1[i] >= delay1) delc1[i] = 0;
      	*in1 += delaymix1 * y;

      	x = *in2;
      	y = delaybuf2[i][delc2[i]];

      	delaybuf2[i][delc2[i]++] = x + y * fd2;
      	if (delc2[i] >= delay2) delc2[i] = 0;
      	*in2 += delaymix2 * y;
}
#endif

#ifdef MEASURETIME
float getTime() {
#ifdef WIN32
  return (float)clock() / CLK_TCK;
#else
  static int starttime_sec = 0, starttime_usec = 0;
  struct timeval tv;
  struct timezone tz;
  int r = gettimeofday(&tv, &tz);
  assert(r == 0);
  float t = 0;
  if (r == 0) {
    if (starttime_sec == 0) {
      starttime_sec = tv.tv_sec;
      starttime_usec = tv.tv_usec;
    }
    t = (tv.tv_sec - starttime_sec) + (tv.tv_usec - starttime_usec) / 1000000.0;
  }
  return t;
#endif
}
#endif

short synthplay(short *out, int len) {
#ifdef MEASURETIME
  float start = getTime();
#endif
  int i, j, k, chk;
  len >>= 1;

  long mask = WAVESIZE - 1;
  float adsrvol;

  for (i = 0; i < len; i++) {
    bufl[i] = bufr[i] = 0;
    chk = 0;
    if (samplepos % sinterval == 0) chk = 1;
    if (chk) {
      for (k = 0; k < trkcnt; k++) {
	j = trk[k].pattern[trkpos];
	if (j == 255) continue;

	int note = patt[j].notes[ptrnpos] & 0x7f;
	int acct = patt[j].notes[ptrnpos] & 0x80;
	if (note > 1) {
	  freq[j] = freqtab[note] * fscaler + patt[j].detune;
	  acc[j] = acct;
	  adsrstate[j] = 0;
	  adsrpos[j] = 0;
	  fphase[j] = 0;
	} else if (note == 1) {
	  adsrstate[j] = 3;
	  adsrpos[j] = 0;
	}
      }
    }
    adsrvol = 0;

    for (k = 0; k < trkcnt; k++) {
      j = trk[k].pattern[trkpos];
      if (j == 255) continue;

      switch (adsrstate[j])
	{
	case 0:
	  if (patt[j].a == 0 || adsrpos[j] >= patt[j].a) {
	    adsrstate[j]++;
	    adsrpos[j] = 0;
	    adsrvol = 1.0f;
	    break;
	  }
	  adsrvol = (float)adsrpos[j] / patt[j].a;
	  break;
	case 1:
	  if (patt[j].d == 0 || adsrpos[j] >= patt[j].d) {
	    adsrstate[j]++;
	    adsrpos[j] = 0;
	    adsrvol = patt[j].s;
	    break;
	  }
	  adsrvol = 1.0 - ((1.0 - patt[j].s) * (float)adsrpos[j] / patt[j].d);
	  break;
	case 2:
	  adsrvol = patt[j].s;
	  break;
	case 3:
	  if (patt[j].r == 0 || adsrpos[j] >= patt[j].r) {
	    adsrstate[j]++;
	    adsrvol = 0;
	    break;
	  }
	  adsrvol = patt[j].s - (patt[j].s * (float)adsrpos[j] / patt[j].r);
	  break;
	default:
	  adsrvol = 0;
	  break;
	}
      adsrpos[j]++;

      float ss, ssl, ssr;
      if (patt[j].type == NOISE2) {
	ss = ((float)rand() / RAND_MAX) * 2.0 - 1.0;
      } else {
	ss = waves[patt[j].type][(long)fphase[j] & mask];
      }

#ifdef USEFILTER
      float fcut = patt[j].fcut;
      float fres = patt[j].fres;
      char ftype = patt[j].flags & HPFILTER;
      if (fcut < 1.0 || fres > 0.0) ss = filtersample(ftype, j, fcut, fres, ss);
#endif
      ss*=patt[j].vol * adsrvol * gvol;
      if (acc[j]) ss*=ACCENTAMT;
      if (ss > 1.0) ss = 1.0;
      else if (ss < -1.0) ss = -1.0;

      ssl=ss * sqrtf(1.0 - patt[j].pan);
      ssr=ss * sqrtf(patt[j].pan);
#ifdef USEDELAY
      if (patt[j].flags & DELAYTRACK) delaysample(j, &ssl, &ssr);
#endif
      bufl[i]+=ssl;
      bufr[i]+=ssr;

      fphase[j]+=freq[j];
      if (fphase[j] > mask) fphase[j] = 0;
    }

    if (chk) {
      ptrnpos++;
      if (ptrnpos >= PATTLEN) {
	ptrnpos = 0;
	trkpos++;
	if (trkpos >= trkmaxlen - 1) {
	  trkpos = 0;
	  //samplepos = 0;
	  loops++;
	}
      }
    }
    samplepos++;
  }

  for (i = 0; i < len; i+=2) {
    float ls = bufl[i] * mastervol * VOLGAIN;
    float rs = bufr[i] * mastervol * VOLGAIN;

    if (ls > 1.0) ls = 1.0;
    else if (ls < -1.0) ls = -1.0;
    if (rs > 1.0) rs = 1.0;
    else if (rs < -1.0) rs = -1.0;

    out[i] = (short)(ls * 32000);
    out[i + 1] = (short)(rs * 32000);
  }

#if 0
  fprintf(stderr, "buflen: %d\n", len);
#endif
#ifdef MEASURETIME
#ifdef DEBUG
  fprintf(stderr, "playbuffer time %f, samples %f\n", getTime() - start, (float)1 / 44100 * len);
#endif
#endif
  return 1;
}

void spcallback(void *data, unsigned char *out, int len)
{
        synthplay((short *)out, len);
}

float synthgettime()
{
        return (float)samplepos / srate;
}

#ifdef USESDL
int main(int argc, char *argv[])
{
        int q = 0;
        SDL_Event e;
        SDL_AudioSpec w;

        SDL_Init(SDL_INIT_AUDIO|SDL_INIT_TIMER);

        w.freq=44100;
        w.format=AUDIO_S16SYS;
        w.channels=2;
        w.samples=1024;
        w.callback=spcallback;
        w.userdata=NULL;

        SDL_OpenAudio(&w,NULL);
        synthinit(w.freq, tr);

        SDL_SetVideoMode(640,480,32,0);

        SDL_PauseAudio(0);

        while(!q)
        {
                //if (synthgettime() > 10.0) exit(1);
                while(SDL_PollEvent(&e)>0)
                {
                        if(e.type == SDL_KEYDOWN)
                        q = 1;
                }
                //if (loops > 0) q = 1;
        }

        SDL_Quit();
        exit(1);
}
#else

int main(void)
{


}

#endif
