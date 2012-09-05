#ifndef DRACHEN_H_
#define DRACHEN_H_

#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>

#ifdef __cplusplus
extern "C" {
#endif
#if 0
  /* Make Emacs indenter happy */
}
#endif

struct drachen_encoder;
typedef struct drachen_encoder drachen_encoder;

typedef struct {
  uint32_t segment_end, block_size;
} drachen_block_spec;

drachen_encoder* drachen_create_encoder(FILE*, uint32_t, const uint32_t*);
drachen_encoder* drachen_create_decoder(FILE*, uint32_t);

int drachen_free(drachen_encoder*);

void drachen_set_block_size(drachen_encoder*, const drachen_block_spec*);
int drachen_encode(drachen_encoder*, const unsigned char* buffer,
                   const char* name);
int drachen_decode(unsigned char* buffer, char* name, uint32_t namelen,
                   drachen_encoder*);

int drachen_error(const drachen_encoder*);
const char* drachen_get_error(const drachen_encoder*);
uint32_t drachen_frame_size(const drachen_encoder*);

void drachen_make_image_xform_matrix(uint32_t*,
                                     uint32_t offset,
                                     uint32_t rows,
                                     uint32_t columns,
                                     unsigned numComponents,
                                     unsigned blockWidth,
                                     unsigned blockHeight);

#if 0
{
#endif
#ifdef __cplusplus
}
#endif

#endif /* DRACHEN_H_ */
