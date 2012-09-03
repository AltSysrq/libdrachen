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
struct drachen_decoder;
typedef struct drachen_decoder drachen_decoder;

drachen_encoder* drachen_create_encoder(FILE*, size_t, const uint32_t*);
drachen_decoder* drachen_create_decoder(FILE*, size_t);

int drachen_free_encoder(drachen_encoder*);
int drachen_free_decoder(drachen_decoder*);

int drachen_encode(drachen_encoder*, unsigned char* buffer, const char* name);
int drachen_decode(unsigned char* buffer, char* name, size_t namelen,
                   drachen_decoder*);
int drachen_error(drachen_decoder*);

void drachen_make_image_xform_matrix(uint32_t*,
                                     size_t offset,
                                     size_t rows,
                                     size_t columns,
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
