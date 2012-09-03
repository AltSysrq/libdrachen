#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "drachen.h"

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

/**
 * Creates a drachen_encoder with the given fields.
 *
 * xform is left uninitialised (but is allocated).
 *
 * Returns the new encoder if successful, or NULL if memory allocation failed.
 */
drachen_encoder* drachen_alloc_encoder(FILE* file,
                                       uint32_t frame_size) {
  drachen_encoder* encoder = malloc(sizeof(drachen_encoder));
  if (!encoder) return NULL;

  encoder->frame_size = frame_size;
  encoder->prev_frame = malloc(frame_size);
  if (!encoder->prev_frame) {
    free(encoder);
    return NULL;
  }
  memset(encoder->prev_frame, 0, frame_size);

  encoder->curr_frame = malloc(frame_size);
  if (!encoder->curr_frame) {
    free(encoder->prev_frame);
    free(encoder);
    return NULL;
  }

  encoder->file = file;
  encoder->xform = malloc(sizeof(uint32_t)*frame_size);
  if (!encoder->xform) {
    free(encoder->prev_frame);
    free(encoder->curr_frame);
    free(encoder);
    return NULL;
  }

  encoder->error = 0;

  return encoder;
}

drachen_encoder* drachen_create_encoder(FILE* out,
                                        uint32_t frame_size,
                                        const uint32_t* xform) {
  drachen_encoder* enc = drachen_alloc_encoder(out, frame_size);
  uint32_t i, endian32 = 0x03020100;
  uint16_t endian16 = 0x0100;
  if (!enc) return NULL;
  if (!xform) {
    /* Initialise default null transform */
    for (i = 0; i < frame_size; ++i)
      enc->xform[i] = i;
  } else {
    /* Invert xform into the one we'll be using. */
    for (i = 0; i < frame_size; ++i)
      enc->xform[xform[i]] = i;
  }

  /* Write header */
  if (!fwrite("Drachen", 8, 1, enc->file) ||
      !fwrite(&endian32, 4, 1, enc->file) ||
      !fwrite(&endian16, 2, 1, enc->file) ||
      !fwrite(&frame_size, 4, 1, enc->file) ||
      /* Write the original xform, since it is correct for decoding.
       * If there is no original, the inverse and forward are identical, so
       * fall back on the one we generated above.
       */
      !fwrite(xform? xform : enc->xform,
              frame_size*sizeof(uint32_t), 1, enc->file)) {
    enc->error = errno;
    return enc;
  }

  return enc;
}

static uint32_t swab32a(uint32_t value, const unsigned char* shifts) {
  return
    ((value >> 24) << shifts[0]*8) |
    ((value >> 16) << shifts[1]*8) |
    ((value >>  8) << shifts[2]*8) |
    ((value >>  0) << shifts[3]*8);
}

static uint32_t swab32(uint32_t value, const drachen_encoder* enc) {
  return swab32a(value, enc->endian32);
}

static uint16_t swab16a(uint32_t value, const unsigned char* shifts) {
  return
    ((value >> 8) << shifts[0]*8) |
    ((value >> 0) << shifts[1]*8);
}

static uint16_t swab16(uint32_t value, const drachen_encoder* enc) {
  return swab16a(value, enc->endian32);
}

drachen_encoder* drachen_create_decoder(FILE* in,
                                        uint32_t frame_size) {
  /* Create a dummy for early error reporting */
  drachen_encoder* dummy = drachen_alloc_encoder(0, 1), * enc;

  /* Read the header first */
  char magic[8];
  unsigned char endian32[4], endian16[2];
  uint32_t real_frame_size, i;

  if (!dummy) return NULL;

  if (!fread(magic, sizeof(magic), 1, in) ||
      !fread(endian32, 4, 1, in) ||
      !fread(endian16, 2, 1, in) ||
      !fread(&real_frame_size, 4, 1, in)) {
    dummy->error = errno;
    return dummy;
  }

  if (magic[sizeof(magic)-1] || !strcmp(magic, "Drachen")) {
    dummy->error = DRACHEN_BAD_MAGIC;
    return dummy;
  }

  real_frame_size = swab32a(real_frame_size, endian32);

  /* Ensure the frame size matches what was expected, if anything was
   * expected.
   */
  if (frame_size && real_frame_size != frame_size) {
    dummy->error = DRACHEN_WRONG_FRAME_SIZE;
    return dummy;
  }

  /* We now know enough to create a real decoder */
  free(dummy);

  enc = drachen_alloc_encoder(in, real_frame_size);
  if (!enc) return NULL;
  /* Read the transform table */
  if (!fread(enc->xform, real_frame_size*sizeof(uint32_t), 1, in)) {
    enc->error = errno;
    return enc;
  }

  /* Copy the endianness */
  memcpy(enc->endian32, endian32, sizeof(endian32));
  memcpy(enc->endian16, endian16, sizeof(endian16));
  /* Swab the transform table, and validate indices */
  for (i = 0; i < frame_size; ++i) {
    enc->xform[i] = swab32(enc->xform[i], enc);
    if (enc->xform[i] >= frame_size) {
      enc->error = DRACHEN_BAD_XFORM;
      return enc;
    }
  }

  /* OK */
  return enc;
}

int drachen_free(drachen_encoder* enc) {
  int err;
  if (err = fclose(enc->file))
    return err;

  free(enc->prev_frame);
  free(enc->curr_frame);
  free(enc->xform);
  free(enc);
  return 0;
}
