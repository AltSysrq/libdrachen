#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <assert.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "drachen.h"
#include "common.h"

static drachen_block_spec default_block_spec[1] = {
  { 0xFFFFFFFFu, 32 },
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
  encoder->block_size = default_block_spec;
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
  encoder->tmp_data = NULL;

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

  if (magic[sizeof(magic)-1] || strcmp(magic, "Drachen")) {
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
  if (enc->file && (err = fclose(enc->file)))
    return err;

  free(enc->prev_frame);
  free(enc->curr_frame);
  free(enc->xform);
  if (enc->tmp_data) free(enc->tmp_data);
  free(enc);
  return 0;
}

int drachen_error(const drachen_encoder* enc) {
  return enc->error;
}

const char* drachen_get_error(const drachen_encoder* enc) {
  if (enc->error == 0)
    return NULL;
  else if (enc->error > 0)
    return strerror(errno);
  else switch (enc->error) {
    case DRACHEN_BAD_MAGIC:
      return "Invalid magic at start of file.";
    case DRACHEN_WRONG_FRAME_SIZE:
      return "File's frame size did not match expectation.";
    case DRACHEN_BAD_XFORM:
      return "File's reverse transform is invalid.";
    case DRACHEN_OVERRUN:
      return "Input stream overran stated bounds.";
    case DRACHEN_PREMATURE_EOF:
      return "Unexpected end of file.";
    default:
      return "An unknown error occurred.";
  }
}

uint32_t drachen_frame_size(const drachen_encoder* enc) {
  return enc->frame_size;
}

void drachen_set_block_size(drachen_encoder* enc,
                            const drachen_block_spec* spec) {
  enc->block_size = spec;
}

void drachen_make_image_xform_matrix(uint32_t* xform,
                                     uint32_t offset,
                                     uint32_t cols,
                                     uint32_t rows,
                                     unsigned num_components,
                                     unsigned block_width,
                                     unsigned block_height) {
  uint32_t component_offsets[num_components];
  uint32_t i, c, bx, by, px, py, nbx, nby, ix;

  /* Reduce block sizes until they are evenly divisible into the full size. */
  while (cols % block_width) --block_width;
  while (rows % block_height)--block_height;
  nbx = cols / block_width;
  nby = rows / block_height;

  for (i = 0; i < offset; ++i)
    xform[i] = i;

  for (i = 0; i < num_components; ++i)
    component_offsets[i] = offset + i*rows*cols;

  i = 0;
  for (by = 0; by < nby; ++by) {
    for (bx = 0; bx < nbx; ++bx) {
      for (py = 0; py < block_height; ++py) {
        for (px = 0; px < block_width; ++px) {
          for (c = 0; c < num_components; ++c) {
            ix = i++ + offset;
            xform[ix] =
              component_offsets[c] +
              bx*block_width + px +
              (by*block_height + py)*cols;
          }
        }
      }
    }
  }
}

void drachen_zero_prev(drachen_encoder* enc, uint32_t off) {
  memset(enc->prev_frame+off, 0, enc->frame_size - off);
}
