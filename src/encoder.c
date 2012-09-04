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
  if (!uranz || !uranp || !sranz || !sranp) {
    meth.compression = EE_CMPZER;
    meth.is_signed = (!sranz || !sranp);
    meth.sub_prev = (!uranp || !sranp);
    meth.sub_fixed = (uminz || uminp || sminz || sminp);
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
