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

struct drachen_encoder {
  uint32_t frame_size;
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
    ((value >> 24) << shifts[0]*8) |
    ((value >> 16) << shifts[1]*8) |
    ((value >>  8) << shifts[2]*8) |
    ((value >>  0) << shifts[3]*8);
}

static inline uint32_t swab32(uint32_t value, const drachen_encoder* enc) {
  return swab32a(value, enc->endian32);
}

static inline uint16_t swab16a(uint32_t value, const unsigned char* shifts) {
  return
    ((value >> 8) << shifts[0]*8) |
    ((value >> 0) << shifts[1]*8);
}

static inline uint16_t swab16(uint32_t value, const drachen_encoder* enc) {
  return swab16a(value, enc->endian32);
}


#endif /* COMMON_H_ */
