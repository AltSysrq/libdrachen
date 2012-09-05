#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drachen.h"

static int decode(const char*);
static int encode(int, const char*, const char*const*, unsigned);

int main(int argc, const char*const* argv) {
  if (argc < 3 || (argv[1][0] != 'i' && argv[1][0] != 'o')) {
    fprintf(stdout, "Usage: %s {i file}|{o framesize outfile file...}\n",
            argv[0]);
    return 255;
  }

  if (argv[1][0] == 'i')
    return decode(argv[2]);
  else
    return encode(atoi(argv[2]), argv[3], argv+4, argc-4);
}

static int decode(const char* infilename) {
  FILE* infile = fopen(infilename, "rb");
  FILE* outfile;
  drachen_encoder* dec;
  unsigned char* buffer;
  char name[256];
  unsigned fs;

  if (!infile) {
    perror("fopen");
    return 1;
  }

  dec = drachen_create_decoder(infile, 0);
  if (!dec) {
    perror("drachen_create_decoder");
    fclose(infile);
    return 1;
  }
  if (drachen_error(dec)) {
    fprintf(stderr, "%s\n", drachen_get_error(dec));
    drachen_free(dec);
    return 1;
  }

  buffer = malloc(fs = drachen_frame_size(dec));
  while (!drachen_decode(buffer, name, sizeof(name), dec)) {
    fprintf(stderr, "%s...\n", name);
    outfile = fopen(name, "wb");
    fwrite(buffer, fs, 1, outfile);
    fclose(outfile);
  }

  if (drachen_error(dec))
    fprintf(stderr, "%s\n", drachen_get_error(dec));

  drachen_free(dec);
  free(buffer);
  return drachen_error(dec);
}

static int encode(int fs, const char* outfilename,
                  const char*const* infiles, unsigned num_infiles) {
  FILE* outfile = fopen(outfilename, "wb");
  FILE* infile;
  drachen_encoder* enc;
  unsigned char* buffer;
  unsigned ix;

  if (!outfile) {
    perror("fopen");
    return 1;
  }

  enc = drachen_create_encoder(outfile, fs, NULL);
  if (!enc) {
    perror("drachen_create_encoder");
    return 1;
  }
  if (drachen_error(enc)) {
    fprintf(stderr, "%s\n", drachen_get_error(enc));
    drachen_free(enc);
    return 1;
  }

  buffer = malloc(fs);
  for (ix = 0; ix < num_infiles && !drachen_error(enc); ++ix) {
    infile = fopen(infiles[ix], "rb");
    fread(buffer, fs, 1, infile);
    fclose(infile);
    drachen_encode(enc, buffer, infiles[ix]);
  }

  if (drachen_error(enc))
    fprintf(stderr, "%s\n", drachen_get_error(enc));

  return drachen_error(enc);
}
