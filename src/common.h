#ifndef COMMON_H_
#define COMMON_H_

#include <stdio.h>
#include <inttypes.h>

/* This is an internal header for libdrachen.
 * Don't install it.
 */

/* Error codes specific to drachen */
#define DRACHEN_BAD_MAGIC -1
#define DRACHEN_WRONG_FRAME_SIZE -2
#define DRACHEN_BAD_XFORM -3
#define DRACHEN_OVERRUN -4
#define DRACHEN_PREMATURE_EOF -5
#define DRACHEN_END_OF_STREAM -6

struct drachen_encoder {
  uint32_t frame_size;
  const drachen_block_spec* block_size;
  unsigned char* prev_frame, * curr_frame;
  FILE* file;
  uint32_t* xform;

  /* For reading, the input machine byte order.
   * Each item is a left bitshift count divided by eight.
   */
  unsigned char endian32[4], endian16[2];

  int error;
};

static inline uint32_t swab32a(uint32_t value, const unsigned char* shifts) {
  return
    (((value >>  0) & 0xFF) << shifts[0]*8) |
    (((value >>  8) & 0xFF) << shifts[1]*8) |
    (((value >> 16) & 0xFF) << shifts[2]*8) |
    (((value >> 24) & 0xFF) << shifts[3]*8);
}

static inline uint32_t swab32(uint32_t value, const drachen_encoder* enc) {
  return swab32a(value, enc->endian32);
}

static inline uint16_t swab16a(uint32_t value, const unsigned char* shifts) {
  return
    (((value >> 0) & 0xFF) << shifts[0]*8) |
    (((value >> 8) & 0xFF) << shifts[1]*8);
}

static inline uint16_t swab16(uint32_t value, const drachen_encoder* enc) {
  return swab16a(value, enc->endian16);
}

/* Constants for the encoding element header */
#define EE_LENENC 0x03
#define EE_LENONE 0x00
#define EE_LENBYT 0x01
#define EE_LENSRT 0x02
#define EE_LENINT 0x03
#define EE_CMPTYP 0x1C
#define EE_CMPNON 0x00
#define EE_CMPR88 0x04
#define EE_CMPR48 0x08
#define EE_CMPR28 0x0C
#define EE_CMPR44 0x10
#define EE_CMPR26 0x14
#define EE_CMPHLF 0x18
#define EE_CMPZER 0x1C
#define EE_CMP_SHIFT 2
#define EE_RLESEX 0x20
#define EE_ININCR 0x40
#define EE_PRVADD 0x80

#endif /* COMMON_H_ */
