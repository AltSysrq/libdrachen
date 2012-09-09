#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>
#include <inttypes.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef HAVE_GETOPT_H
#include <getopt.h>
#endif

#include "drachen.h"

/* Command-line options (initialised to 0) */
static int co_is_encoding, co_is_decoding, co_dryrun;
static int co_zero_frames;
static const char* co_primary_filename;
static const char*const* co_encoding_input_files;
static unsigned co_num_encoding_input_files;
static unsigned co_image_off, co_image_comps,
  co_image_nr, co_image_nc, co_image_bw, co_image_bh;
static unsigned co_block_size;
static int co_force;
static const char* co_sequential_output_name;

static int co_verbosity, co_timing_statistics, co_nowarn;

static const char* co_this;
/* End command-line options */

static inline void l_syserr(const char* message) {
  fprintf(stderr, "%s: error: %s: %s\n",
          co_this, message, strerror(errno));
}
static inline void l_sysferr(const char* message, const char* fname) {
  if (!fname) fname = "<default>";
  fprintf(stderr, "%s: error: %s: %s: %s\n",
          co_this, message, fname, strerror(errno));
}

static inline void l_error(const char* message) {
  fprintf(stderr, "%s: error: %s\n", co_this, message);
}

static inline void l_errore(const char* fname,
                            const drachen_encoder* enc) {
  fprintf(stderr, "%s: error: %s: %s\n",
          co_this, fname, drachen_get_error(enc));
}

static inline void l_warn (const char* message) {
  if (!co_nowarn)
    fprintf(stderr, "%s: warning: %s\n", co_this, message);
}

static inline void l_warns(const char* message, const char* parm) {
  if (!co_nowarn)
    fprintf(stderr, "%s: warning: %s: %s\n",
            co_this, parm, message);
}

static inline void l_info(const char* message) {
  if (co_verbosity >= 1)
    fprintf(stderr, "%s: note: %s\n", co_this, message);
}

static inline void l_report(const char* message) {
  if (co_verbosity >= 1)
    fprintf(stderr, "%s\n", message);
}

static inline void l_reportf(const char* fmt, ...) {
  va_list args;
  if (co_verbosity >= 1) {
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
  }
}

static inline void l_report_extra(const char* message) {
  if (co_verbosity >= 2)
    fprintf(stderr, "%s\n", message);
}

static inline void l_report_extraf(const char* fmt, ...) {
  va_list args;
  if (co_verbosity >= 2) {
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
  }
}

static inline void l_debug(const char* message) {
  if (co_verbosity >= 3)
    fprintf(stderr, "%s: DEBUG: %s\n", co_this, message);
}

static int do_encode(void), do_decode(void);

static const char short_options[] = "hVfo:O:X:R:C:W:H:b:n:vtwedDz";
#ifdef HAVE_GETOPT_LONG
static const struct option long_options[] = {
  { "block-size",          1, NULL, 'b' },
  { "decode",              0, NULL, 'd' },
  { "dry-run",             0, NULL, 'D' },
  { "encode",              0, NULL, 'e' },
  { "force",               0, NULL, 'f' },
  { "help",                0, NULL, 'h' },
  { "img-block-height",    1, NULL, 'H' },
  { "img-block-width",     1, NULL, 'W' },
  { "img-body-offset",     1, NULL, 'O' },
  { "img-num-cols",        1, NULL, 'C' },
  { "img-num-components",  1, NULL, 'X' },
  { "img-num-rows",        1, NULL, 'R' },
  { "no-warnings",         0, NULL, 'w' },
  { "numeric-output-fmt",  1, NULL, 'n' },
  { "output",              0, NULL, 'o' },
  { "show-timing",         0, NULL, 't' },
  { "verbose",             0, NULL, 'v' },
  { "version",             0, NULL, 'V' },
  { "zero-frames",         0, NULL, 'z' },
  {0},
};
#endif

static void uint_arg_or_die(unsigned* dst,
                            const char* name) {
  char* endptr;
  long int val;
  val = strtol(optarg, &endptr, 0);
  if (*endptr || val < (long int)0) {
    fprintf(stderr, "%s: invalid integer for %s: %s\n",
            co_this, name, optarg);
    exit(255);
  }

  *dst = (unsigned)val;
}

static const char*const usage_statement =
"Usage: drachencode -e [-fvtwD] [image-opts] [-b blk-sz] -o outfile infiles...\n"
"       drachencode -d [-fvtwD] [-n format] [infile]\n"
"Encodes or decodes libdrachen files from or into individual named files.\n"
"\n"
"All options are listed below.\n"
#ifndef HAVE_GETOPT_LONG
  "WARNING: Long options are not supported on your system.\n"
#endif
  "-b, --block-size=size\n"
  "    Sets the block size for encoding, in bytes.\n"
  "    Block size does not significantly affect encoding speed (except for\n"
  "    extreme values). Adjusting the block size from the default may give\n"
  "    better compression ratios.\n"
  "-d, --decode\n"
  "    Perform decoding. This option is mutually exclusive with --encode;\n"
  "    exactly one of the two must be specified.\n"
  "-D, --dry-run\n"
  "    Do everything but file writing.\n"
  "-e, --encode\n"
  "    Perform encoding. This option is mutually exclusive with --decode;\n"
  "    exactly one of the two must be specified.\n"
  "-f, --force\n"
  "    On decoding, allow overwriting of files. On encoding, allow implicitly\n"
  "    writing to standard output (this makes --output optional).\n"
  "-h, -?, --help\n"
  "    Print this help message and exit.\n"
  "-H, --img-block-height=height\n"
  "-W, --img-block-width=width\n"
  "-O, --img-offset=offset\n"
  "-C, --img-num-cols=ncols\n"
  "-X, --img-num-components=ncomps\n"
  "-R, --img-num-rows=nrows\n"
  "    On encoding, reorder the input bytes to be optimal for an image which\n"
  "    is ncols pixels wide and nrows pixels high, whose pixels are ncomps\n"
  "    bytes wide, and whose first pixel is located at byte offset offset.\n"
  "    Bytes are rearranged so that each colour component is in a run by\n"
  "    itself, and pixels within blocks of size width by height are in\n"
  "    contiguous runs. Width and height should (but do not have to be)\n"
  "    evenly divisible into ncols and nrows, respectively. The overall\n"
  "    frame size must be greater than (offset+ncomps*nrows*ncols) bytes.\n"
  "    If no --block-size is given, block size is implicitly set to\n"
  "    (width/4) bytes.\n"
  "    If no --img-offset is given, zero is assumed. If --img-num-components\n"
  "    is not given, one is assumed.\n"
  "    Either all or none of these options must be given, except for\n"
  "    --img-offset and --img-num-components, which are always optional.\n"
  "-w, --no-warnings\n"
  "    Suppress any warnings that may be issued.\n"
  "-n, --numeric-output-fmt=format\n"
  "    Instead of using files embedded in the archive on decoding, instead\n"
  "    use format (a printf-compatible string which will receive exactly one\n"
  "    integer argument) to derive filenames from a zero-based frame index.\n"
  "-o, --output=outfile\n"
  "    On encoding, write to outfile instead of standard output. The name\n"
  "    \"-\" means to use standard output, even if --force was not given.\n"
  "-t, --show-timing\n"
  "    Show timing and speed statistics.\n"
  "-v, --verbose\n"
  "    Print more messages. Each use of this option increases the verbosity.\n"
  "-V, --version\n"
  "    Print version number and exit.\n"
  "-z, --zero-frames\n"
  "    On decoding, pretend the previous frame, starting at img-offset, is\n"
  "    entirely zero. This has interesting effects for video.\n"
  ;

int main(int argc, char*const* argv) {
  int opt, has_consumed_fmt_input;
  unsigned i;
  co_this = argv[0];

  while (-1 != (opt =
#ifdef HAVE_GETOPT_LONG
         getopt_long(argc, argv, short_options, long_options, NULL)
#else
         getopt(argc, argv, short_options)
#endif
  )) {
    switch (opt) {
    case '?':
    case ':':
    case 'h':
      fputs(usage_statement, opt == 'h'? stdout : stderr);
      return (opt == 'h'? 0 : 255);

    case 'V':
      puts(PACKAGE_STRING);
      return 0;

    case 'b':
      uint_arg_or_die(&co_block_size, "block-size");
      break;

    case 'd':
      co_is_decoding = 1;
      break;

    case 'D':
      co_dryrun = 1;
      break;

    case 'e':
      co_is_encoding = 1;
      break;

    case 'f':
      co_force = 1;
      break;

    case 'H':
      uint_arg_or_die(&co_image_bh, "img-block-height");
      break;

    case 'W':
      uint_arg_or_die(&co_image_bw, "img-block-width");
      break;

    case 'O':
      uint_arg_or_die(&co_image_off, "img-offset");
      break;

    case 'C':
      uint_arg_or_die(&co_image_nc, "img-num-cols");
      break;

    case 'X':
      uint_arg_or_die(&co_image_comps, "img-num-components");
      break;

    case 'R':
      uint_arg_or_die(&co_image_nr, "img-num-rows");
      break;

    case 'w':
      co_nowarn = 1;
      break;

    case 'n':
      co_sequential_output_name = strdup(optarg);
      break;

    case 'o':
      co_primary_filename = strdup(optarg);
      break;

    case 't':
      co_timing_statistics = 1;
      break;

    case 'v':
      ++co_verbosity;
      break;

    case 'z':
      co_zero_frames = 1;
      break;

    default:
      fprintf(stderr, "%s: FATAL: Unknown \"known\" option: %c\n",
              co_this, (char)opt);
      exit(255);
    }
  }

  /* Validate options */
  if (!(co_is_decoding ^ co_is_encoding)) {
    l_error("Exactly one of --encode or --decode must be specified.");
    return 255;
  }

  if (co_is_encoding &&
      (co_image_nc || co_image_nr || co_image_bw || co_image_bh ||
       co_image_off || co_image_comps) &&
      !(co_image_nc && co_image_nr && co_image_bw && co_image_bh)) {
    l_error("Either no image options, or at least --img-num-cols,\n"
            "--img-num-rows, --img-block-width, and --img-block-height\n"
            "must be specified.");
    return 255;
  }

  if (co_image_nc && !co_image_comps)
    co_image_comps = 1;

  /* Validate the format string, if given */
  if (co_sequential_output_name) {
    has_consumed_fmt_input = 0;
    for (i = 0; co_sequential_output_name[i]; ++i) {
      if (co_sequential_output_name[i] == '%') {
        ++i;
        if (co_sequential_output_name[i] == '%')
          /* Simple escaped percent sign */
          continue;

        if (has_consumed_fmt_input) {
          l_error("Format string has more than one consuming formatting.");
          return 255;
        }

        has_consumed_fmt_input = 1;

        /* Flags */
        while (co_sequential_output_name[i] == '#' ||
               co_sequential_output_name[i] == '0' ||
               co_sequential_output_name[i] == '-' ||
               co_sequential_output_name[i] == ' ' ||
               co_sequential_output_name[i] == '+')
          ++i;
        /* Width */
        while (co_sequential_output_name[i] >= '0' &&
               co_sequential_output_name[i] <= '9')
          ++i;

        /* Type */
        if (co_sequential_output_name[i] != 'd' &&
            co_sequential_output_name[i] != 'i' &&
            co_sequential_output_name[i] != 'o' &&
            co_sequential_output_name[i] != 'u' &&
            co_sequential_output_name[i] != 'x' &&
            co_sequential_output_name[i] != 'X') {
          l_error("Invalid format string type.");
          return 255;
        }
      }
    }

    if (!has_consumed_fmt_input) {
      l_error("Format string is not variant.");
      return 255;
    }
  }

  if (co_is_encoding && !co_force && !co_primary_filename) {
    l_error("Not implicitly encoding to standard output without --force");
    return 255;
  }

  if (co_image_bh && co_image_nr && co_image_nr % co_image_bh) {
    l_warn("img-block-height does not divide evenly into img-num-rows.");
    l_warn("Actual block height will differ from what you specfied.");
  }
  if (co_image_bw && co_image_nc && co_image_nc % co_image_bw) {
    l_warn("img-block-width does not divide evenly into img-num-cols.");
    l_warn("Actual block height will differ from what you specified.");
  }

  if (co_dryrun && co_is_encoding) {
    l_report("Changing output file to /dev/null to perform dry-run.");
    co_primary_filename = "/dev/null";
  }

  if (co_is_encoding) {
    co_num_encoding_input_files = optind <= argc? argc - optind : 0;
    /* Cast is safe, we won't be modifying anything.
     * (char*const* -> const char*const* is technically "incompatible")
     */
    co_encoding_input_files = (const char*const*)argv+optind;
    if (!co_num_encoding_input_files) {
      l_error("No encoding input files given.");
      return 255;
    }

    return do_encode();
  } else {
    if (optind >= argc) {
      co_primary_filename = "-";
      if (!co_force)
        l_warn("Decoding from standard input.");
    } else if (argc - optind == 1) {
      co_primary_filename = argv[optind];
    } else {
      l_error("Too many input files.");
      return 255;
    }

    return do_decode();
  }
}

static const char*const data_suffices[] = {
  "B",
  "kB", //Karl
  "MB", //Marx
  "GB", //Gave
  "TB", //The
  "PB", //Proles
  "EB", //Eleven
  "ZB", //Zeppelins
  "YB",
};

static int do_encode(void) {
  FILE* file = 0, *infile = 0;
  drachen_encoder* enc = NULL;
  struct stat statbuf;
  uint32_t frame_size, amt_read;
  uint32_t* custom_xform = NULL;
  unsigned char* buffer = NULL;
  int status = 0;
  unsigned i;
  clock_t enc_start, enc_end, total_time = 0;
  unsigned long long total_data;
  unsigned data_suffix = 0;
  drachen_block_spec custom_blocks[2];

  if (!co_primary_filename || !strcmp(co_primary_filename, "-"))
    file = stdin;
  else
    file = fopen(co_primary_filename,
                 /* Only allow overwriting if forced, or if writing to the
                  * bitbucket.
                  */
                 co_force || !strcmp(co_primary_filename, "/dev/null")?
                 "wb" : "wbx");

  if (!file) {
    l_sysferr("Could not open output file", co_primary_filename);
    if (errno == EEXIST)
      fprintf(stderr, "Use --force to overwrite it anyway.\n");
    status = 254;
    goto finish;
  }

  /* Get the frame size from the first file */
  if (stat(co_encoding_input_files[0], &statbuf)) {
    l_sysferr("Could not stat first input file", co_encoding_input_files[0]);
    status = 254;
    goto finish;
  }

  if (!statbuf.st_size) {
    l_error("First file appears to be empty (size==0), giving up.");
    status = 254;
    goto finish;
  }

  frame_size = statbuf.st_size;

  l_reportf("Using frame size of %u bytes.\n", (unsigned)frame_size);

  if (co_image_bw) {
    if (frame_size < co_image_off + co_image_comps*co_image_nr*co_image_nc) {
      l_error("Frames are too small for the image parameters you specified.");
      status = 255;
      goto finish;
    }

    if (frame_size > co_image_nc + co_image_comps*co_image_nr*co_image_nc)
      l_warn("Frame size is larger than the space used by the image parms.");

    custom_xform = malloc(sizeof(uint32_t)*frame_size);
    if (!custom_xform) {
      l_syserr("Could not allocate image transform buffer");
      status = 254;
      goto finish;
    }

    /* Initialise the whole thing first in case the image doesn't cover the
     * whole thing.
     */
    for (i = 0; i < frame_size; ++i)
      custom_xform[i] = i;

    drachen_make_image_xform_matrix(custom_xform,
                                    co_image_off,
                                    co_image_nc,
                                    co_image_nr,
                                    co_image_comps,
                                    co_image_bw,
                                    co_image_bh);
  }

  buffer = malloc(frame_size);
  if (!buffer) {
    l_syserr("Could not allocate input buffer");
    status = 254;
    goto finish;
  }

  enc = drachen_create_encoder(file, frame_size, custom_xform);
  if (!enc) {
    l_syserr("Could not allocate encoder");
    status = 254;
    goto finish;
  }

  if (co_block_size) {
    custom_blocks[0].segment_end = 0xFFFFFFFFu;
    custom_blocks[0].block_size = co_block_size;
    drachen_set_block_size(enc, custom_blocks);
  } else if (co_image_bh) {
    if (co_image_off) {
      custom_blocks[0].segment_end = co_image_off;
      custom_blocks[0].block_size = co_image_off;
      custom_blocks[1].segment_end = 0xFFFFFFFFu;
      custom_blocks[1].block_size = co_image_bw/4;
      if (custom_blocks[1].block_size < 16)
        custom_blocks[1].block_size = co_image_bw;
    } else {
      custom_blocks[0].segment_end = 0xFFFFFFFFu;
      custom_blocks[0].block_size = co_image_bw/4;
      if (custom_blocks[0].block_size < 16)
        custom_blocks[0].block_size = co_image_bw;
    }

    drachen_set_block_size(enc, custom_blocks);
  }

  for (i = 0; i < co_num_encoding_input_files; ++i) {
    l_report(co_encoding_input_files[i]);

    infile = fopen(co_encoding_input_files[i], "rb");
    if (!infile) {
      l_sysferr("Could not open input file", co_encoding_input_files[i]);
      status = 254;
      goto finish;
    }

    amt_read = fread(buffer, 1, frame_size, infile);
    if (ferror(infile)) {
      l_sysferr("Could not read from input file", co_encoding_input_files[i]);
      status = 254;
      goto finish;
    }

    if (EOF != fgetc(infile)) {
      l_warns("File is longer than frame size; it will be truncated",
              co_encoding_input_files[i]);
    }

    fclose(infile);
    infile = NULL;

    if (amt_read < frame_size) {
      l_warns("File is shorter than frame size; other bytes assumed zero.",
              co_encoding_input_files[i]);
      memset(buffer+amt_read, 0, frame_size-amt_read);
    }

    enc_start = clock();
    status = drachen_encode(enc, buffer, co_encoding_input_files[i]);
    enc_end = clock();

    if (status) {
      l_errore(co_encoding_input_files[i], enc);
      goto finish;
    }

    total_time += enc_end - enc_start;

    if (co_timing_statistics)
      l_report_extraf("File %s encoded in %u ms\n",
                      co_encoding_input_files[i],
                      (unsigned)((enc_end-enc_start)*1000/CLOCKS_PER_SEC));
  }

  /* If timing statistics were requested, print them */
  if (co_timing_statistics) {
    if (total_time == 0)
      total_time = 1;

    fprintf(stderr, "Encoding rate:\n");
    fprintf(stderr, "  Frames/ms:  %u\n",
            (unsigned)(((unsigned long long)co_num_encoding_input_files) *
                       CLOCKS_PER_SEC / 1000 / total_time));
    fprintf(stderr, "  Frames/sec: %u\n",
            (unsigned)(((unsigned long long)co_num_encoding_input_files) *
                       CLOCKS_PER_SEC / total_time));
    fprintf(stderr, "  Frames/min: %u\n",
            (unsigned)(((unsigned long long)co_num_encoding_input_files) *
                       60 * CLOCKS_PER_SEC / total_time));

    total_data = co_num_encoding_input_files;
    total_data *= frame_size;
    total_data *= CLOCKS_PER_SEC;
    total_data /= total_time;
    while (total_data > 1024*1024) {
      total_data /= 1024;
      ++data_suffix;
    }

    fprintf(stderr, "  Data rate:  %u %s/sec\n",
            (unsigned)total_data, data_suffices[data_suffix]);
  }

  finish:
  if (infile) fclose(infile);
  if (buffer) free(buffer);
  if (custom_xform) free(custom_xform);
  if (enc) {
    drachen_free(enc);
    file = NULL;
  }
  if (file) fclose(file);

  return status;
}

int do_decode(void) {
  FILE* infile = NULL, * outfile = NULL;
  drachen_encoder* enc = NULL;
  unsigned char* buffer = NULL;
  uint32_t frame_size;
  unsigned current_frame, data_suffix = 0;
  char filename[256];
  clock_t dec_start, dec_end, total_time = 0;
  unsigned long long total_data;
  int status = 0;

  if (!co_primary_filename || !strcmp(co_primary_filename, "-"))
    infile = stdin;
  else
    infile = fopen(co_primary_filename, "rb");

  if (!infile) {
    l_sysferr("Could not open input file", co_primary_filename);
    status = 254;
    goto finish;
  }

  enc = drachen_create_decoder(infile, 0);
  if (!enc) {
    l_syserr("Could not allocate decoder");
    status = 254;
    goto finish;
  }
  if (drachen_error(enc)) {
    l_errore(co_primary_filename? co_primary_filename : "<default>", enc);
    status = 254;
    goto finish;
  }

  frame_size = drachen_frame_size(enc);
  l_reportf("Decoding with frame size %u\n", (unsigned)frame_size);
  buffer = malloc(frame_size);
  if (!buffer) {
    l_syserr("Could not allocate output buffer");
    status = 254;
    goto finish;
  }

  for (current_frame = 0; ; ++current_frame) {
    dec_start = clock();
    status = drachen_decode(buffer, filename, sizeof(filename), enc);
    dec_end = clock();

    if (status == DRACHEN_END_OF_STREAM) {
      status = 0;
      break;
    }

    total_time += dec_end-dec_start;

    if (status) {
      l_errore("<unknown filename>", enc);
      goto finish;
    }

    if (co_zero_frames)
      drachen_zero_prev(enc, co_image_off);

    l_reportf("%5d %s", current_frame, filename);
    if (co_sequential_output_name) {
      snprintf(filename, sizeof(filename),
               co_sequential_output_name, current_frame);
      l_reportf(" -> %s", filename);
    }
    l_reportf("\n");

    if (co_timing_statistics)
      l_report_extraf("File %s decoded in %u ms\n", filename,
                      (unsigned)((dec_end-dec_start)*1000/CLOCKS_PER_SEC));

    if (!co_dryrun) {
      outfile = fopen(filename, co_force? "wb" : "wbx");
      if (!outfile) {
        l_sysferr("Could not open output file", filename);
        if (errno == EEXIST)
          fprintf(stderr, "To overwrite the file anyway, use --force.\n");
        status = 254;
        goto finish;
      }

      if (!fwrite(buffer, frame_size, 1, outfile)) {
        l_sysferr("Could not write to output file", filename);
        status = 254;
        goto finish;
      }

      fclose(outfile);
      outfile = NULL;
    }
  }

  l_reportf("%u frames decoded.\n", current_frame);

  if (co_timing_statistics) {
    if (total_time == 0)
      total_time = 1;

    fprintf(stderr, "Decoding rate:\n");
    fprintf(stderr, "  Frames/ms:  %u\n",
            (unsigned)(((unsigned long long)current_frame) *
                       CLOCKS_PER_SEC / 1000 / total_time));
    fprintf(stderr, "  Frames/sec: %u\n",
            (unsigned)(((unsigned long long)current_frame) *
                       CLOCKS_PER_SEC / total_time));
    fprintf(stderr, "  Frames/min: %u\n",
            (unsigned)(((unsigned long long)current_frame) *
                       60 * CLOCKS_PER_SEC / total_time));

    total_data = current_frame;
    total_data *= frame_size;
    total_data *= CLOCKS_PER_SEC;
    total_data /= total_time;
    while (total_data > 1024*1024) {
      total_data /= 1024;
      ++data_suffix;
    }

    fprintf(stderr, "  Data rate:  %u %s/sec\n",
            (unsigned)total_data, data_suffices[data_suffix]);
  }

  finish:
  if (buffer) free(buffer);
  if (outfile) fclose(outfile);
  if (enc) {
    drachen_free(enc);
    infile = NULL;
  }
  if (infile) fclose(infile);
  return status;
}
