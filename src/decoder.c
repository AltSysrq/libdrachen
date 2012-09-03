#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "drachen.h"
#include "common.h"

static int decompress_noop(unsigned char* dst, unsigned char* end,
                           FILE* in, int sex) {
  size_t size = end-dst;
  return !fread(dst, size, 1, in);
}

static int decompress_zero(unsigned char* dst, unsigned char* end,
                           FILE* in, int sex) {
  memset(dst, 0, end-dst);
  return 0;
}

#define RLE(runlength,datum) \
  if (((unsigned)runlength) > dst-end) return DRACHEN_OVERRUN; \
  memset(dst, datum, runlength); \
  dst += runlength

#define GETDATUM(datum) \
  datum = fgetc(in); \
  if (datum == EOF) return DRACHEN_PREMATURE_EOF

static int decompress_rle88(unsigned char* dst, unsigned char* end,
                            FILE* in, int sex) {
  int runlength, datum;
  while (dst != end) {
    runlength = fgetc(in), datum = fgetc(in);
    if (runlength == EOF || datum == EOF)
      return DRACHEN_PREMATURE_EOF;
    if (!runlength) runlength = 256;

    RLE(runlength, datum);
  }
  return 0;
}

static int decompress_rle48(unsigned char* dst, unsigned char* end,
                            FILE* in, int sex) {
  int runlength, datum;
  unsigned rl0, rl1;
  while (dst != end) {
    runlength = fgetc(in);
    if (runlength == EOF)
      return DRACHEN_PREMATURE_EOF;

    rl0 = runlength & 0xF;
    rl1 = (runlength >> 4) & 0xF;
    if (rl0 == 0) rl0 = 16;
    if (rl1 == 0) rl1 = 16;

    //Handle each datum
    GETDATUM(datum);
    RLE(rl0, datum);

    //Second half may be extra
    if (dst == end) break;

    datum = fgetc(in);
    if (datum == EOF)
      return DRACHEN_PREMATURE_EOF;

    RLE(rl1, datum);
  }

  return 0;
}

static int decompress_rle28(unsigned char* dst, unsigned char* end,
                            FILE* in, int sex) {
  int runlength, datum;
  unsigned rl0, rl1, rl2, rl3;
  while (dst != end) {
    runlength = fgetc(in);
    if (runlength == EOF)
      return DRACHEN_PREMATURE_EOF;

    rl0 = runlength & 0x3;
    rl1 = (runlength >> 2) & 0x3;
    rl2 = (runlength >> 4) & 0x3;
    rl3 = (runlength >> 6) & 0x3;
    if (rl0 == 0) rl0 = 4;
    if (rl1 == 0) rl1 = 4;
    if (rl2 == 0) rl2 = 4;
    if (rl3 == 0) rl3 = 4;

    //Handle each datum
    GETDATUM(datum);
    RLE(rl0, datum);
    //Second quarter may be extra
    if (dst == end) break;
    GETDATUM(datum);
    RLE(rl1, datum);
    if (dst == end) break;

    GETDATUM(datum);
    RLE(rl2, datum);
    //Second half may be extra
    if (dst == end) break;

    GETDATUM(datum);
    RLE(rl3, datum);
  }

  return 0;
}

static int decompress_rle44(unsigned char* dst, unsigned char* end,
                            FILE* in, int sex) {
  int value;
  unsigned rl, datum;
  while (dst != end) {
    value = fgetc(in);
    if (value == EOF)
      return DRACHEN_PREMATURE_EOF;

    rl = (value & 0xF);
    if (rl == 0) rl = 16;
    datum = (value >> 4) & 0xF;
    if (sex && (datum & 0x8))
      datum |= 0xF0;
    RLE(rl, datum);
  }

  return 0;
}

static int decompress_rle26(unsigned char* dst, unsigned char* end,
                            FILE* in, int sex) {
  int value;
  unsigned rl, datum;
  while (dst != end) {
    value = fgetc(in);
    if (value == EOF)
      return DRACHEN_PREMATURE_EOF;

    rl = (value & 0x3);
    if (rl == 0) rl = 4;
    datum = (rl >> 2) & 0x3F;
    if (sex && (datum & 0x20))
      datum |= 0xC0;
    RLE(rl, datum);
  }

  return 0;
}

static int decompress_half(unsigned char* dst, unsigned char* end,
                           FILE* in, int sex) {
  int value;
  unsigned d0, d1;
  while (dst != end) {
    value = fgetc(in);
    if (value == EOF)
      return DRACHEN_PREMATURE_EOF;

    d0 = (value >> 0) & 0xF;
    d1 = (value >> 4) & 0xF;

    if (sex && (d0 & 0x8)) d0 |= 0xF0;
    if (sex && (d1 & 0x8)) d1 |= 0xF0;

    *dst++ = d0;
    if (dst != end)
      *dst++ = d1;
  }

  return 0;
}

static int (* const decompressors[8])(unsigned char*, unsigned char*,
                                      FILE*, int) = {
  decompress_noop,
  decompress_rle88,
  decompress_rle48,
  decompress_rle28,
  decompress_rle44,
  decompress_rle26,
  decompress_half,
  decompress_zero,
};

static int decode_one_element(uint32_t* offset, drachen_encoder* enc) {
  int head = fgetc(enc->file), lenenc, cmptyp, rlesex, inincr, prvadd, ch;
  unsigned char incrval;
  unsigned i;
  uint16_t len16;
  uint32_t len32;
  if (head == EOF)
    return DRACHEN_PREMATURE_EOF;

  lenenc = head & EE_LENENC;
  cmptyp = (head & EE_CMPTYP) >> EE_CMP_SHIFT;
  rlesex = !!(head & EE_RLESEX);
  inincr = !!(head & EE_ININCR);
  prvadd = !!(head & EE_PRVADD);

  /* Determine length */
  switch (lenenc) {
  case EE_LENONE:
    len32 = 1;
    break;

  case EE_LENBYT:
    ch = fgetc(enc->file);
    if (ch == EOF)
      return DRACHEN_PREMATURE_EOF;

    len32 = ch + 2;
    break;

  case EE_LENSRT:
    if (!fread(&len16, 2, 1, enc->file))
      return errno;

    len32 = swab16(len16, enc) + 259;
    break;

  case EE_LENINT:
    if (!fread(&len32, 4, 1, enc->file))
      return errno;

    len32 = swab32(len32, enc);
    break;

#ifndef NDEBUG
  default:
    assert(0);
#endif
  }

  /* Read the incr value if present */
  if (inincr) {
    if (!fread(&incrval, 1, 1, enc->file))
      return errno;
  }

  /* Ensure that the length is sane */
  if (*offset + len32 >= enc->frame_size)
    return DRACHEN_OVERRUN;

  /* Decompress */
  enc->error = (*decompressors[cmptyp])(enc->curr_frame+*offset,
                                        enc->curr_frame+*offset+len32,
                                        enc->file, rlesex);
  if (enc->error)
    return enc->error;

  /* Add inincr if set */
  if (inincr)
    for (i = 0; i < len32; ++i)
      enc->curr_frame[*offset+i] += incrval;

  /* Add prev_frame values if set */
  if (prvadd)
    for (i = 0; i < len32; ++i)
      enc->curr_frame[*offset+i] = enc->prev_frame[*offset+i];

  *offset += len32;

  return 0;
}

int drachen_decode(unsigned char* out, char* name, uint32_t namelen,
                   drachen_encoder* enc) {
  int ch, is_first = 1;
  uint32_t offset;

  /* Stop now if there is an error */
  if (enc->error) return enc->error;

  /* Read the name */
  while (1) {
    ch = fgetc(enc->file);
    if (ch == EOF) {
      if (is_first)
        return DRACHEN_END_OF_STREAM;

      enc->error = DRACHEN_PREMATURE_EOF;
      return enc->error;
    }

    if (name && namelen) {
      if (--namelen)
        *name++ = ch;
      else
        /* Final byte, use a NUL instead */
        *name++ = 0;
    }

    if (!ch) break;
  }

  /* Read until failure or end of frame */
  for (offset = 0; offset < enc->frame_size && !enc->error; )
    enc->error = decode_one_element(&offset, enc);

  /* If no error, reverse the transformation into out, then update the
   * "previous frame".
   */
  if (!enc->error) {
    for (offset = 0; offset < enc->frame_size; ++offset)
      out[offset] = enc->curr_frame[enc->xform[offset]];
    memcpy(enc->prev_frame, enc->curr_frame, enc->frame_size);
  }

  return enc->error;
}
