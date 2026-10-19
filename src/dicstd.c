/*
dicstd.c: Zstandard tool using preset dictionaries

Based on dicflate.c, which is based on Mark Adler's zpipe.c.

The MIT License

Copyright (c) hrobeers, 2019

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"

#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(__CYGWIN__)
#  include <fcntl.h>
#  include <io.h>
#  define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#else
#  define SET_BINARY_MODE(file)
#endif

/* Only the tail of the dictionary is ever referenced: matches are relative
   offsets, so at most the decoder window (level dependent) is used. */
#define MAX_DICT_SIZE (16 * 1024 * 1024)

/* print a diagnostic, prefixed, and report failure */
static int fail(const char *fmt, ...)
{
    va_list ap;
    fputs("dicstd: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    return EXIT_FAILURE;
}

/* Compress from file source to file dest until EOF on source.
   Returns EXIT_SUCCESS on success, EXIT_FAILURE on error. */
static int def(FILE *source, FILE *dest, int level,
               const char *dict, size_t dict_size, int magicless)
{
    int ret = EXIT_SUCCESS;
    size_t zret;
    ZSTD_CCtx *cctx = NULL;
    void *in = NULL;
    void *out = NULL;
    size_t const in_size = ZSTD_CStreamInSize();
    size_t const out_size = ZSTD_CStreamOutSize();
    ZSTD_inBuffer input;
    ZSTD_outBuffer output;

    cctx = ZSTD_createCStream();
    if (!cctx)
        return fail("out of memory");

    if (ZSTD_isError(ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, level)) ||
        ZSTD_isError(ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 0)) ||
        ZSTD_isError(ZSTD_CCtx_setParameter(cctx, ZSTD_c_format,
                    magicless ? ZSTD_f_zstd1_magicless : ZSTD_f_zstd1))) {
        ret = fail("invalid compression level or parameter");
        goto cleanup;
    }

    /* set Dictionary if provided; a raw (non-zstd) dictionary is treated as
       plain content, matching dicflate's behaviour */
    if (dict && dict_size > 0) {
        zret = ZSTD_CCtx_loadDictionary(cctx, dict, dict_size);
        if (ZSTD_isError(zret)) {
            ret = fail("cannot load dictionary: %s", ZSTD_getErrorName(zret));
            goto cleanup;
        }
    }

    in = malloc(in_size);
    out = malloc(out_size);
    if (!in || !out) {
        ret = fail("out of memory");
        goto cleanup;
    }

    /* compress until end of file */
    for (;;) {
        size_t n = fread(in, 1, in_size, source);
        if (ferror(source)) {
            ret = fail("error reading stdin");
            goto cleanup;
        }
        input.src = in;
        input.size = n;
        input.pos = 0;
        ZSTD_EndDirective const mode =
            (n == 0 || feof(source)) ? ZSTD_e_end : ZSTD_e_continue;

        /* run ZSTD_compressStream2() until the whole input chunk is consumed,
           or the frame is finished when this is the last chunk */
        do {
            output.dst = out;
            output.size = out_size;
            output.pos = 0;
            zret = ZSTD_compressStream2(cctx, &output, &input, mode);
            if (ZSTD_isError(zret)) {
                ret = fail("%s", ZSTD_getErrorName(zret));
                goto cleanup;
            }
            if (fwrite(out, 1, output.pos, dest) != output.pos) {
                ret = fail("error writing stdout");
                goto cleanup;
            }
        } while (mode == ZSTD_e_end ? zret != 0 : input.pos < input.size);

        if (mode == ZSTD_e_end)
            break;
    }

cleanup:
    free(in);
    free(out);
    ZSTD_freeCStream(cctx);
    return ret;
}

/* Decompress from file source to file dest until stream ends or EOF.
   Returns EXIT_SUCCESS on success, EXIT_FAILURE on error. */
static int inf(FILE *source, FILE *dest,
               const char *dict, size_t dict_size, int magicless)
{
    int ret = EXIT_SUCCESS;
    size_t zret;
    ZSTD_DCtx *dctx = NULL;
    void *in = NULL;
    void *out = NULL;
    size_t const in_size = ZSTD_DStreamInSize();
    size_t const out_size = ZSTD_DStreamOutSize();
    ZSTD_inBuffer input;
    ZSTD_outBuffer output;
    size_t last = 0; /* hint of the last call: 0 when the frame was complete */

    dctx = ZSTD_createDStream();
    if (!dctx)
        return fail("out of memory");

    if (ZSTD_isError(ZSTD_DCtx_setParameter(dctx, ZSTD_d_format,
                    magicless ? ZSTD_f_zstd1_magicless : ZSTD_f_zstd1))) {
        ret = fail("cannot set the frame format");
        goto cleanup;
    }

    /* the dictionary must be loaded before any frame data is decompressed;
       frames compressed with a raw dictionary carry no dictionary id, so a
       dictionary that grew by prepending still decodes older frames */
    if (dict && dict_size > 0) {
        zret = ZSTD_DCtx_loadDictionary(dctx, dict, dict_size);
        if (ZSTD_isError(zret)) {
            ret = fail("cannot load dictionary: %s", ZSTD_getErrorName(zret));
            goto cleanup;
        }
    }

    in = malloc(in_size);
    out = malloc(out_size);
    if (!in || !out) {
        ret = fail("out of memory");
        goto cleanup;
    }

    /* decompress until the stream ends or EOF on source */
    for (;;) {
        size_t n = fread(in, 1, in_size, source);
        if (ferror(source)) {
            ret = fail("error reading stdin");
            goto cleanup;
        }
        if (n == 0)
            break;
        input.src = in;
        input.size = n;
        input.pos = 0;

        /* run ZSTD_decompressStream() until the input chunk is consumed */
        while (input.pos < input.size) {
            output.dst = out;
            output.size = out_size;
            output.pos = 0;
            zret = ZSTD_decompressStream(dctx, &output, &input);
            if (ZSTD_isError(zret)) {
                ret = fail("%s", ZSTD_getErrorName(zret));
                goto cleanup;
            }
            if (fwrite(out, 1, output.pos, dest) != output.pos) {
                ret = fail("error writing stdout");
                goto cleanup;
            }
            last = zret;
        }
    }

    /* a non-zero hint means the last frame needs more bytes */
    if (last != 0)
        ret = fail("incomplete zstd stream");

cleanup:
    free(in);
    free(out);
    ZSTD_freeDStream(dctx);
    return ret;
}

static int printUsage(int argc, char **argv)
{
    fprintf(stderr,
        "Usage: %s [-x] [-r] [-l level] [-d dict] < source > dest\n"
        "\n"
        "Compress (zstd) stdin to stdout, or extract (decompress) with -x.\n"
        "\n"
        "Options:\n"
        "  -x         extract (decompress) instead of compress\n"
        "  -r         raw zstd frame, without the 4-byte magic number\n"
        "  -l level   compression level, 1 (fastest) to 22 (best); default 3\n"
        "  -d dict    preset dictionary, read from file dict; when larger than\n"
        "             16 MiB, only the last 16 MiB is used; required for\n"
        "             extraction if used during compression\n",
        argv[0]);
    return EXIT_FAILURE;
}

static long int parseOrDefault(char *str, long int def)
{
    char *endptr;
    long int parsed = strtol(str, &endptr, 10);
    if (endptr == str) return def;
    return parsed;
}

/* read the dictionary, keeping the tail of files larger than MAX_DICT_SIZE */
static char *load_dict(const char *path, size_t *dict_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fail("cannot open dictionary file %s", path);
        exit(EXIT_FAILURE);
    }

    long int size = 0;
    size_t to_read = MAX_DICT_SIZE;
    int tail = 0;
    if (fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) >= 0) {
        if (size > MAX_DICT_SIZE) {
            fseek(f, size - (long int)MAX_DICT_SIZE, SEEK_SET);
            tail = 1;
        } else {
            rewind(f);
            to_read = (size_t)size;
        }
    } else {
        rewind(f); /* non-seekable: read from the start, up to the cap */
    }

    char *buf = malloc(to_read ? to_read : 1);
    if (!buf) {
        fail("out of memory");
        exit(EXIT_FAILURE);
    }
    size_t n = fread(buf, 1, to_read, f);
    if (ferror(f)) {
        fail("error reading dictionary file %s", path);
        exit(EXIT_FAILURE);
    }
    fclose(f);

    if (tail)
        fprintf(stderr, "Dict size: %zu (tail of a %ld byte dictionary)\n", n, size);
    else
        fprintf(stderr, "Dict size: %zu\n", n);

    *dict_size = n;
    return buf;
}

/* compress or decompress from stdin to stdout */
int main(int argc, char **argv)
{
    /* avoid end-of-line conversions */
    SET_BINARY_MODE(stdin);
    SET_BINARY_MODE(stdout);

    /* parse commandline options */
    int opt;
    int extract = 0;
    int magicless = 0;
    int level = ZSTD_CLEVEL_DEFAULT;
    char *dict = NULL;
    size_t dict_size = 0;

    while ((opt = getopt(argc, argv, "rxl:d:")) != -1) {
        switch (opt) {
        case 'x': extract = 1; break;
        case 'r': magicless = 1; break;
        case 'l':
            level = (int)parseOrDefault(optarg, level);
            if (level < 1) level = 1;
            if (level > ZSTD_maxCLevel()) level = ZSTD_maxCLevel();
            break;
        case 'd':
            dict = load_dict(optarg, &dict_size);
            break;
        default:
            return printUsage(argc, argv);
        }
    }

    if (extract)
        return inf(stdin, stdout, dict, dict_size, magicless);
    return def(stdin, stdout, level, dict, dict_size, magicless);
}
