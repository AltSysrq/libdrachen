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

/* Error codes specific to drachen */
/**
 * Indicates that the input file's first 8 bytes do not match the magic
 * sequence expected by Drachen.
 */
#define DRACHEN_BAD_MAGIC -1
/**
 * Indicates that the caller creating a decoder expected a frame size different
 * from the frame size used in the given input file.
 */
#define DRACHEN_WRONG_FRAME_SIZE -2
/**
 * Indicates that the reverse transformation matrix in the input file contained
 * integers which pointed outside the framespace.
 */
#define DRACHEN_BAD_XFORM -3
/**
 * Indicates that, while decoding, a frame segment or RLE run indicated a size
 * which exceeded the boundaries imposed by its container.
 */
#define DRACHEN_OVERRUN -4
/**
 * Indicates that the end of the input file was encountered at a position other
 * than where a new frame would be expected.
 */
#define DRACHEN_PREMATURE_EOF -5
/**
 * Indicates that decoding of the input file has completed.
 *
 * This is not an error, and will never be returned by drachen_error().
 */
#define DRACHEN_END_OF_STREAM -6

/**
 * Opaque type which stores Drachen encoding/decoding information.
 */
struct drachen_encoder;
typedef struct drachen_encoder drachen_encoder;

/**
 * Indicates that the data between segment_end (exclusive) and some unknown
 * starting point (which is implicit) should be encoded using a block size of
 * block_size.
 *
 * @see drachen_set_block_size().
 */
typedef struct {
  uint32_t segment_end, block_size;
} drachen_block_spec;

/**
 * Creates an encoder to write a new stream to the given FILE, which has frames
 * of the size specified in the second argument. If the third argument is
 * non-NULL, the given alternate transformation matrix specified there is
 * used. The length of the third argument must be at least the frame size; the
 * contents are copied, so the array pointed to is not owned by the resulting
 * encoder.
 *
 * On success, returns the constructed encoder. Headers have already been
 * written to the file. On failure, may either return an encoder in a failure
 * state (see drachen_error) or NULL if it could not allocate memory.
 *
 * A transformation matrix is an array which maps data as such:
 *   for (i = 0; i < frame_size; ++i)
 *     destination[transform[i]] = source[i];
 * The default transformation matrix is the identity matrix; that is, it results
 * in a simple copy of the source data.
 */
drachen_encoder* drachen_create_encoder(FILE*, uint32_t, const uint32_t*);

/**
 * Creates an encoder which is ready to decode from the given file. If the
 * second argument is non-zero, this call fails if the input file does not use
 * exactly that frame size.
 *
 * On success, returns an encoder. On failure, returns NULL if memory was
 * exhausted, or an encoder in an error state (see drachen_error).
 */
drachen_encoder* drachen_create_decoder(FILE*, uint32_t);

/**
 * Frees all memory used by the given encoder, closes its file, and frees the
 * encoder itself.
 */
int drachen_free(drachen_encoder*);

/**
 * Changes the block size specification for the given encoder. The second
 * argument is an array of drachen_block_specs, where the segment_end field of
 * the last one is greater than or equal to the frame size. The block size in
 * each element is used for bytes between the segment_end (or 0 if this is the
 * first) of the previous spec, and its own segment_end (exclusive). The array
 * belongs to the caller; it is not copied, and will not be freed when the
 * encoder is. It is the responsibility of the caller to ensure that the
 * lifetime of the array extends at least to the destruction of the encoder, or
 * to a time where drachen_set_block_size() is called with a different array.
 *
 * The drachen_block_specs' segment_end fields must be in ascending order, and
 * there may be no duplicates. block_size must be greater than zero for all
 * specs.
 *
 * By default, Drachen uses a small, consistent block size.
 *
 * Block size generally does not affect speed, but rather compression
 * ratio. Too small a block size will cause the encoder to switch encoding
 * methods too frequently; too large a block size will reduce the encoder's
 * ability to notice localised coincidences, thus reducing compressibility.
 */
void drachen_set_block_size(drachen_encoder*, const drachen_block_spec*);

/**
 * Encodes a new frame via the given encoder. buffer is an array of bytes whose
 * length must be at least the frame size of the encoder. name is a
 * NUL-terminated string which names this frame. It does not need to be
 * unique (though it should be).
 *
 * Returns 0 on success; returns non-zero and sets the encoder's error field on
 * failure.
 */
int drachen_encode(drachen_encoder*, const unsigned char* buffer,
                   const char* name);
/**
 * Decodes the next frame from the given decoder, storing it in buffer, which
 * must have a length greater than or equal to the frame size. If name is
 * non-NULL, it must point to a buffer at least namelen bytes long. The name of
 * the next frame will be copied into that buffer. If the frame's name is
 * greater than (namelen-1), it is truncated, so that name will always be a
 * valid NUL-terminated string.
 *
 * On success, returns 0. If the end of the stream has been reached, returns
 * DRACHEN_END_OF_STREAM. On failure, sets the error field in the encoder and
 * returns non-zero.
 *
 * If DRACHEN_END_OF_STREAM is returned, and the input file was opened in
 * read-write mode, it is then possible to use drachen_encode() to append more
 * frames to the stream.
 */
int drachen_decode(unsigned char* buffer, char* name, uint32_t namelen,
                   drachen_encoder*);

/**
 * Returns the error status of the given encoder.
 *
 * 0 means that no error has occurred.
 * A positive value is the value of errno when a system or library call failed.
 * A negative value is a drachen-specific error code.
 *
 * Once this function returns non-zero, it is no longer valid to use the
 * encoder for any purpose, other than to free it and query its status.
 */
int drachen_error(const drachen_encoder*);
/**
 * Returns a human-readable error description of the error status in the given
 * encoder, or NULL if no error has occurred.
 */
const char* drachen_get_error(const drachen_encoder*);
/**
 * Returns the frame size being used by the given encoder.
 *
 * There is no way to change the frame size of an encoder.
 */
uint32_t drachen_frame_size(const drachen_encoder*);

/**
 * Creates a transformation matrix optimal for encoding an uncompressed image,
 * storing it into the first argument, which must be at least
 * (offset+num_components*cols*rows) long (elements beyond that length will not
 * be touched).
 *
 * offset specifies the byte offset of the first part of image data. No byte
 * reordering happens before the offset.
 *
 * cols and rows indicate the number of columns and rows in the image data,
 * respectively.
 *
 * num_components indicates how many bytes wide each pixel is. Bytes in the
 * image data are reordered so that each component is in a single, contiguous
 * block.
 *
 * block_width and block_height specify dimensions for "blocks" of pixels
 * within the image data, which are rectangles of those dimensions. Bytes are
 * reordered so that sub-pixels belonging to the same block are contiguous. If
 * block_width is not evenly divisible into cols, or block_height into rows, it
 * is resized so that it is.
 */
void drachen_make_image_xform_matrix(uint32_t*,
                                     uint32_t offset,
                                     uint32_t cols,
                                     uint32_t rows,
                                     unsigned num_components,
                                     unsigned block_width,
                                     unsigned block_height);

/**
 * Zeroes out the "previous frame" for the given encoder.
 *
 * This is only "useful" for producing interesting effects on decoding.
 */
void drachen_zero_prev(drachen_encoder*, uint32_t off);

#if 0
{
#endif
#ifdef __cplusplus
}
#endif

#endif /* DRACHEN_H_ */
