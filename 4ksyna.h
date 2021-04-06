//4ksyna.h -- 4k introsynth

#define SINE 0
#define SAW 1
#define SQUARE 2
#define NOISE 3 //metallic noise
#define NOISE2 4 //real noise

//flags
#define DELAYTRACK 0x1
#define HPFILTER 0x2

short synthinit(int samplerate, unsigned char *track);
short synthplay(short *out, int len);
void spcallback(void *data, unsigned char *out, int len);
float synthgettime();
