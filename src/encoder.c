#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>

#include "drachen.h"
#include "common.h"

static unsigned unsigned_stats(unsigned* minout,
                               unsigned* median,
                               unsigned* maxout,
                               const unsigned char* data,
                               const unsigned char* prev,
                               unsigned len) {
  unsigned char max = data[0] - prev[0], min = max;
  unsigned i;
  for (i = 1; i < len; ++i) {
    unsigned char ch = data[i] - prev[i];
    if (ch > max)
      max = ch;
    else if (ch < min)
      min = ch;
  }

  *minout = min;
  *median = (((unsigned int)max)+((unsigned int)min))/2;
  *maxout = max;

  return ((unsigned int)max) - ((unsigned int)min) + 1;
}

static unsigned signed_stats(signed* minout,
                             signed* median,
                             signed* maxout,
                             const unsigned char* udata,
                             const unsigned char* uprev,
                             unsigned len) {
  const signed char* data = (signed char*)udata,
    * prev = (signed char*)uprev;
  signed char max = data[0] - prev[0], min = max;
  unsigned i;
  for (i = 1; i < len; ++i) {
    signed char ch = data[i] - prev[i];
    if (ch > max)
      max = ch;
    else if (ch < min)
      min = ch;
  }

  *minout = min;
  *median = (((signed int)max)+((signed int)min))/2;
  *maxout = max;

  return (unsigned)(((signed int)max)-((signed int)min)) + 1;
}

static inline unsigned ceildiv(unsigned dividend, unsigned divisor) {
  return (dividend+1)/divisor;
}

typedef struct encoding_method {
  unsigned compression;
  int is_signed, sub_prev, sub_fixed;
  unsigned char fixed_sub;
} encoding_method;

static unsigned rle_count_runs(const unsigned char* data, unsigned len,
                               unsigned maxrun) {
  unsigned cnt = 1, runlen = 1;
  unsigned char run = data[0];
  unsigned i;
  for (i = 1; i < len; ++i) {
    if (runlen == maxrun || data[i] != run) {
      ++cnt;
      run = data[i];
      runlen = 1;
    }
  }

  return cnt;
}

static encoding_method optimal_encoding_method(const unsigned char* data,
                                               const unsigned char* prev,
                                               unsigned len) {
  unsigned char zero[len], test[len];
  const unsigned char* test_data;
  /* Stats for min/med/max with zero and prev subtracted, unsigned and
   * signed.
   */
  unsigned uminz, uminp, umedz, umedp, umaxz, umaxp, uranz, uranp;
  signed   sminz, sminp, smedz, smedp, smaxz, smaxp;
  unsigned sranz, sranp;
  unsigned expected_len, other_len, runs, i;
  encoding_method meth;
  memset(zero, 0, len);
  memset(&meth, 0, sizeof(meth));

  uranz = unsigned_stats(&uminz, &umedz, &umaxz, data, zero, len);
  uranp = unsigned_stats(&uminp, &umedp, &umaxp, data, prev, len);
  sranz =   signed_stats(&sminz, &smedz, &smaxz, data, zero, len);
  sranp =   signed_stats(&sminp, &smedp, &smaxp, data, prev, len);

  /* First check for the best case, where range is one (which means we can use
   * zero compression).
   *
   * (Right now, some of these cases are redundant, but that could change
   * later).
   */
  if (uranz == 1 || uranp == 1 || sranz == 1 || sranp == 1) {
    /* Subtract one from each to make the below code more compact */
    --uranz, --uranp, --sranz, --sranp;
    meth.compression = EE_CMPZER;
    meth.is_signed = (!sranz || !sranp);
    meth.sub_prev = (!uranp || !sranp);
    meth.sub_fixed = (uminz && uminp && sminz && sminp);
    if (!uranz)
      meth.fixed_sub = uminz;
    else if (!uranp)
      meth.fixed_sub = uminp;
    else if (!sranz)
      meth.fixed_sub = sminz;
    else
      meth.fixed_sub = sminp;

    return meth;
  }

  /* If all ranges are above 6-bit range, we must use an 8-bit encoding
   * (uncompressed, RLE8-8, RLE4-8, or RLE2-8). sub_fixed will never make a
   * difference, so never use it (1-byte penalty). Similarly, is_signed has no
   * effect on anything, since no sign extension occurs.
   */
  if (uranz > 64 && uranp > 64 && sranz > 64 && sranp > 64) {
    /* Simplest case of no compression */
    meth.compression = EE_CMPNON;
    meth.is_signed = 0;
    meth.sub_fixed = 0;
    /* Only subtract from previous if needed */
    meth.sub_prev = !(uranz > 64 || sranz > 64);
    expected_len = len;

    if (meth.sub_prev) {
      for (i = 0; i < len; ++i)
        test[i] = data[i] - prev[i];
      test_data = test;
    } else
      test_data = data;

    /* See if RLE8-8 uses fewer bytes. */
    other_len = 2*rle_count_runs(test_data, len, 256);
    if (other_len < expected_len) {
      meth.compression = EE_CMPR88;
      expected_len = other_len;
    }

    /* RLE 4-8 */
    runs = rle_count_runs(test_data, len, 16);
    other_len = runs + ceildiv(runs,2);
    if (other_len < expected_len) {
      meth.compression = EE_CMPR48;
      expected_len = other_len;
    }

    /* RLE 2-8 */
    runs = rle_count_runs(test_data, len, 4);
    other_len = runs + ceildiv(runs,4);
    if (other_len < expected_len) {
      meth.compression = EE_CMPR28;
      expected_len = other_len;
    }

    return meth;
  }

  /* If we get here, we know that the ranges allow for six-bit encoding. If
   * they cannot be encoded 4-bit, try RLE2-6, RLE8-8, RLE4-8 (RLE2-6 will
   * never be worse than uncompressed, and RLE2-8 is strictly worse than
   * RLE2-6).
   */
  if (uranz > 16 && uranp > 16 && sranz > 16 && sranp > 16) {
    if (uranz <= 64 && (uminp != 0 || uranp > 64)) {
      meth.is_signed = 0;
      meth.sub_prev = 0;
      meth.sub_fixed = !!uminz;
      meth.fixed_sub = uminz;
    } else if (uranp <= 64) {
      meth.is_signed = 0;
      meth.sub_prev = 1;
      meth.sub_fixed = !!uminp;
      meth.fixed_sub = uminp;
    } else if (sranz <= 64 && (sminp != 0 || sranp > 64)) {
      meth.is_signed = 1;
      meth.sub_prev = 0;
      meth.sub_fixed = !!sminz;
      meth.fixed_sub = sminz;
    } else {
      meth.is_signed = 1;
      meth.sub_prev = 1;
      meth.sub_fixed = !!sminp;
      meth.fixed_sub = sminp;
    }

    /* We don't need to handle sub_fixed for test data, since it doesn't affect
     * the RLE counter.
     */
    if (meth.sub_prev) {
      for (i = 0; i < len; ++i)
        test[i] = data[i] - prev[i];
      test_data = test;
    } else
      test_data = data;

    meth.compression = EE_CMPR26;
    expected_len = rle_count_runs(test_data, len, 4);

    /* Try RLE4-8 */
    runs = rle_count_runs(test_data, len, 16);
    other_len = runs + ceildiv(runs, 2);
    if (other_len < expected_len) {
      meth.compression = EE_CMPR48;
      expected_len = other_len;
    }

    /* And RLE8-8 */
    runs = rle_count_runs(test_data, len, 256);
    other_len = 2*runs;
    if (other_len < expected_len) {
      meth.compression = EE_CMPR88;
      expected_len = other_len;
    }

    /* If using an 8-bit encoding, we won't ever need sub_fixed. */
    if (meth.compression == EE_CMPR48 || meth.compression == EE_CMPR88)
      meth.sub_fixed = 0;

    return meth;
  }

  /* Using a 4-bit encoding. Consider HALF, RLE8-8, RLE4-4. RLE4-8 is strictly
   * worse than RLE4-4; RLE4-4 will always match or better RLE2-6, which is
   * already considered better than RLE2-8.
   */
  if (uranz <= 16 && (uminp != 0 || uranp > 16)) {
    meth.is_signed = 0;
    meth.sub_prev = 0;
    meth.sub_fixed = !!uminz;
    meth.fixed_sub = uminz;
  } else if (uranp <= 16) {
    meth.is_signed = 0;
    meth.sub_prev = 1;
    meth.sub_fixed = !!uminp;
    meth.fixed_sub = uminp;
  } else if (sranz <= 16 && (sminp != 0 || sranp > 16)) {
    meth.is_signed = 1;
    meth.sub_prev = 0;
    meth.sub_fixed = !!sminz;
    meth.fixed_sub = sminz;
  } else {
    meth.is_signed = 1;
    meth.sub_prev = 1;
    meth.sub_fixed = !!sminp;
    meth.fixed_sub = sminp;
  }

  /* We don't need to handle sub_fixed for test data, since it doesn't affect
   * the RLE counter.
   */
  if (meth.sub_prev) {
    for (i = 0; i < len; ++i)
      test[i] = data[i] - prev[i];
    test_data = test;
  } else
    test_data = data;

  meth.compression = EE_CMPHLF;
  expected_len = ceildiv(len,2);

  /* Try RLE8-8 */
  other_len = 2*rle_count_runs(data, len, 256);
  if (other_len < expected_len) {
    meth.compression = EE_CMPR88;
    expected_len = other_len;
  }

  /* And RLE4-4 */
  other_len = rle_count_runs(data, len, 16);
  if (other_len < expected_len) {
    meth.compression = EE_CMPR44;
    expected_len = other_len;
  }

  /* If we switched to an 8-bit encoding, drop any sub_fixed. */
  if (meth.compression == EE_CMPR88)
    meth.sub_fixed = 0;

  return meth;
}

#define PUTC(out,ch) if (EOF == fputc(ch,out)) return errno

static int compressor_none(FILE* out, const unsigned char* data, uint32_t len) {
  if (fwrite(data, len, 1, out))
    return 0;
  else
    return errno;
}

static int compressor_rle88(FILE* out,
                            const unsigned char* data, uint32_t len) {
  unsigned char curr = data[0];
  unsigned runlen = 1, i;
  for (i = 1; i < len; ++i) {
    if (runlen == 256 || data[i] != curr) {
      /* End of this run */
      PUTC(out, runlen & 0xFF);
      PUTC(out, curr);
      curr = data[i];
      runlen = 1;
    } else {
      ++runlen;
    }
  }

  /* Finish final run */
  PUTC(out, runlen & 0xFF);
  PUTC(out, curr);
  return 0;
}

static int compressor_rle48(FILE* out,
                            const unsigned char* data, uint32_t len) {
  unsigned char c0 = data[0], c1;
  unsigned rl0 = 1, rl1, i = 0;

  run0:
  for (++i; i < len; ++i) {
    if (rl0 == 16 || data[i] != c0) {
      rl1 = 1;
      c1 = data[i];
      goto run1;
    } else {
      ++rl0;
    }
  }

  /* Only half a run pair */
  PUTC(out, rl0 /* Upper bits don't matter */);
  PUTC(out, c0);
  return 0;

  run1:
  for (++i; i < len; ++i) {
    if (rl1 == 16 || data[i] != c1) {
      /* Finished with this run pair */
      PUTC(out, (rl0 & 0xF) | ((rl1 & 0xF) << 4));
      PUTC(out, c0);
      PUTC(out, c1);
      rl0 = 1;
      c0 = data[i];
      goto run0;
    } else {
      ++rl1;
    }
  }

  /* Finish both runs */
  PUTC(out, (rl0 & 0xF) | ((rl1 & 0xF) << 4));
  PUTC(out, c0);
  PUTC(out, c1);
  return 0;
}

static int compressor_rle28(FILE* out,
                            const unsigned char* data, uint32_t len) {
  unsigned char c0 = data[0], c1, c2, c3;
  unsigned rl0 = 1, rl1, rl2, rl3, i = 0;

  run0:
  for (++i; i < len; ++i) {
    if (rl0 == 4 || data[i] != c0) {
      rl1 = 1;
      c1 = data[i];
      goto run1;
    } else {
      ++rl0;
    }
  }

  /* Only one of four runs */
  PUTC(out, rl0 /* Upper bits don't matter */);
  PUTC(out, c0);
  return 0;

  run1:
  for (++i; i < len; ++i) {
    if (rl1 == 4 || data[i] != c1) {
      rl2 = 1;
      c2 = data[i];
      goto run2;
    } else {
      ++rl1;
    }
  }

  /* Only two of four runs */
  PUTC(out, (rl1 << 2) | (rl0 & 0x3));
  PUTC(out, c0);
  PUTC(out, c1);
  return 0;

  run2:
  for (++i; i < len; ++i) {
    if (rl2 == 4 || data[i] != c2) {
      rl3 = 1;
      c3 = data[i];
      goto run3;
    } else {
      ++rl2;
    }
  }

  /* Only three of four runs */
  PUTC(out, (rl2 << 4) | ((rl1 & 0x3) << 2) | (rl0 & 0x3));
  PUTC(out, c0);
  PUTC(out, c1);
  PUTC(out, c2);
  return 0;

  run3:
  for (++i; i < len; ++i) {
    if (rl3 ==4 || data[i] != c3) {
      PUTC(out,
           (rl3 << 6) |
           ((rl2 & 0x3) << 4) |
           ((rl1 & 0x3) << 2) |
           (rl0 & 0x3));
      PUTC(out, c0);
      PUTC(out, c1);
      PUTC(out, c2);
      PUTC(out, c3);

      rl0 = 1;
      c0 = data[i];
      goto run0;
    } else {
      ++rl3;
    }
  }

  PUTC(out,
       (rl3 << 6) |
       ((rl2 & 0x3) << 4) |
       ((rl1 & 0x3) << 2) |
       (rl0 & 0x3));
  PUTC(out, c0);
  PUTC(out, c1);
  PUTC(out, c2);
  PUTC(out, c3);

  return 0;
}

static int compressor_rle44(FILE* out,
                            const unsigned char* data, uint32_t len) {
  unsigned runlength = 1, i = 1;
  unsigned char curr = data[0];

  for (; i < len; ++i) {
    if (runlength == 16 || data[i] != curr) {
      /* End of this run */
      PUTC(out, ((runlength & 0x0F) | ((curr & 0x0F) << 4)));
      runlength = 1;
      curr = data[i];
    } else {
      ++runlength;
    }
  }

  /* Finish last run */
  PUTC(out, ((runlength & 0x0F) | ((curr & 0x0F) << 4)));
  return 0;
}

static int compressor_rle26(FILE* out,
                            const unsigned char* data, uint32_t len) {
  unsigned runlength = 1, i = 1;
  unsigned char curr = data[0];

  for (; i < len; ++i) {
    if (runlength == 4 || data[i] != curr) {
      /* End of this run */
      PUTC(out, ((runlength & 0x3) | ((curr & 0x3F) << 2)));
      runlength = 1;
      curr = data[i];
    } else {
      ++runlength;
    }
  }

  PUTC(out, ((runlength & 0x3) | ((curr & 0x3F) << 2)));

  return 0;
}

static int compressor_half(FILE* out,
                           const unsigned char* data, uint32_t len) {
  unsigned i;
  for (i = 0; i + 1 < len; i += 2)
    PUTC(out, (data[i] & 0x0F) | ((data[i+1] & 0x0F) << 4));

  /* Check for odd trailing byte */
  if (i < len)
    PUTC(out, data[i] /* The upper four bits are ignored */);

  return 0;
}

static int (*const compressors[8])(FILE*, const unsigned char*, uint32_t) = {
  compressor_none,
  compressor_rle88,
  compressor_rle48,
  compressor_rle28,
  compressor_rle44,
  compressor_rle26,
  compressor_half,
  /* Should never be called */
  NULL,
};

static int encode_one_element(FILE* out,
                              encoding_method meth,
                              const unsigned char* data,
                              const unsigned char* prev,
                              uint32_t len) {
  unsigned char len8;
  uint16_t len16;
  const unsigned char* data_to_encode;
  unsigned char* tmp_data = NULL;
  unsigned i;
  int status;
  unsigned char head =
    (len == 1? EE_LENONE :
     len <= 258? EE_LENBYT :
     len <= (65535+259)? EE_LENSRT :
     EE_LENINT) |
    meth.compression |
    (meth.is_signed? EE_RLESEX : 0) |
    (meth.sub_fixed? EE_ININCR : 0) |
    (meth.sub_prev ? EE_PRVADD : 0);

  /* Write header */
  if (EOF == fputc(head, out))
    return errno;

  /* Write length, if needed */
  if (len > 1) {
    if (len <= 258) {
      len8 = (unsigned char)(len-2);
      if (!fwrite(&len8, 1, 1, out))
        return errno;
    } else if (len <= (65535+259)) {
      len16 = (uint16_t)(len-259);
      if (!fwrite(&len16, 2, 1, out))
        return errno;
    } else {
      if (!fwrite(&len, 4, 1, out))
        return errno;
    }
  }

  /* Write offset byte, if used */
  if (meth.sub_fixed && !fwrite(&meth.fixed_sub, 1, 1, out))
    return errno;

  /* Write the body, if any.
   * (EE_CMPZER never has a body).
   */
  if (meth.compression != EE_CMPZER) {
    /* If the input data is modified, create a temporary buffer */
    if (meth.sub_fixed || meth.sub_prev) {
      data_to_encode = tmp_data = malloc(len);
      if (!tmp_data)
        return ENOMEM;

      memcpy(tmp_data, data, len);
      if (meth.sub_fixed)
        for (i = 0; i < len; ++i)
          tmp_data[i] -= meth.fixed_sub;

      if (meth.sub_prev)
        for (i = 0; i < len; ++i)
          tmp_data[i] -= prev[i];
    } else {
      /* Use the raw input data */
      data_to_encode = data;
    }

    /* Compress the body */
    status = (*compressors[meth.compression >> EE_CMP_SHIFT])(out,
                                                              data_to_encode,
                                                              len);
    /* Free any scratch space used */
    if (tmp_data)
      free(tmp_data);

    /* Fail if the compressor failed. */
    if (status)
      return status;
  }

  return 0;
}

int drachen_encode(drachen_encoder* enc,
                   const unsigned char* buffer,
                   const char* name) {
  uint32_t start_of_curr, offset, i, bs;
  const drachen_block_spec* block_size = enc->block_size;
  encoding_method currmeth, nextmeth;
  unsigned char* swap;

  if (!fwrite(name, strlen(name)+1, 1, enc->file))
    return enc->error = errno;

  /* Transform the input frame according to the transformation matrix. */
  for (i = 0; i < enc->frame_size; ++i)
    enc->curr_frame[i] = buffer[enc->xform[i]];

  start_of_curr = 0;
  for (offset = 0; offset < enc->frame_size; offset += bs) {
    /* Determine the current block size.
     * This is either the block size for the current segment, or whatever's
     * left in that segment.
     */
    bs = block_size->block_size;
    if (offset + bs >= block_size->segment_end) {
      bs = block_size->segment_end - offset;
      ++block_size;
    }
    /* Check for going off the end of the frame */
    if (offset + bs > enc->frame_size)
      bs = enc->frame_size - offset;

    nextmeth = optimal_encoding_method(enc->curr_frame+offset,
                                       enc->prev_frame+offset,
                                       bs);
    if (offset == 0)
      /* First segment */
      currmeth = nextmeth;
    else if (memcmp(&currmeth, &nextmeth, sizeof(encoding_method))) {
      /* Changing encoding method, write the previous */
      enc->error = encode_one_element(enc->file,
                                      currmeth,
                                      enc->curr_frame+start_of_curr,
                                      enc->prev_frame+start_of_curr,
                                      offset - start_of_curr);
      if (enc->error)
        return enc->error;

      start_of_curr = offset;
      currmeth = nextmeth;
    }
  }

  /* Finish the last segment */
  enc->error = encode_one_element(enc->file,
                                  currmeth,
                                  enc->curr_frame+start_of_curr,
                                  enc->prev_frame+start_of_curr,
                                  enc->frame_size - start_of_curr);

  /* Update "prev" frame */
  swap = enc->prev_frame;
  enc->prev_frame = enc->curr_frame;
  enc->curr_frame = swap;

  return enc->error;
}
