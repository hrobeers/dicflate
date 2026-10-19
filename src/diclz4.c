/*
diclz4.c: LZ4 tool using preset dictionaries

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
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "lz4.h"
#include "lz4hc.h"
#include "lz4frame.h"

#if defined(MSDOS) || defined(OS2) || defined(WIN32) || defined(__CYGWIN__)
#  include <fcntl.h>
#  include <io.h>
#  define SET_BINARY_MODE(file) setmode(fileno(file), O_BINARY)
#else
#  define SET_BINARY_MODE(file)
#endif

/* LZ4 match offsets are limited to 64 KiB, so only the tail of a larger
   dictionary is ever referenced; matches are relative offsets, therefore
   the dictionary can grow over time by prepending new content. */
#define MAX_DICT_SIZE (64 * 1024)

/* chunk size of both the frame and the raw container;
   also the maximum raw block size */
#define BLOCK_IN (1024 * 1024)

/* print a diagnostic, prefixed, and report failure */
static int fail(const char *fmt, ...)
{
    va_list ap;
    fputs("diclz4: ", stderr);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    return EXIT_FAILURE;
}

/* ================= frame mode: standard, interoperable lz4 frames ================= */

/* Compress from file source to file dest until EOF on source. */
static int def_frame(FILE *source, FILE *dest, int level,
                     const char *dict, size_t dict_size)
{
    int ret = EXIT_SUCCESS;
    LZ4F_cctx *cctx = NULL;
    void *in = NULL;
    void *out = NULL;
    size_t const in_size = BLOCK_IN;
    size_t out_size;
    size_t r;
    LZ4F_preferences_t prefs;

    r = LZ4F_createCompressionContext(&cctx, LZ4F_VERSION);
    if (LZ4F_isError(r))
        return fail("out of memory");

    memset(&prefs, 0, sizeof(prefs));
    prefs.frameInfo.blockMode = LZ4F_blockLinked;
    prefs.frameInfo.blockSizeID = LZ4F_max4MB; /* keep typical inputs in one block */
    prefs.frameInfo.contentChecksumFlag = LZ4F_noContentChecksum;
    prefs.compressionLevel = level;
    prefs.autoFlush = 0;

    out_size = LZ4F_compressBound(in_size, &prefs);
    in = malloc(in_size);
    out = malloc(out_size);
    if (!in || !out) {
        ret = fail("out of memory");
        goto cleanup;
    }

    /* set Dictionary if provided; frames carry no dictionary id or checksum,
       matching the raw dictionary behaviour of the other dicflate tools */
    r = LZ4F_compressBegin_usingDict(cctx, out, out_size,
                                     dict_size > 0 ? dict : NULL, dict_size,
                                     &prefs);
    if (LZ4F_isError(r)) {
        ret = fail("cannot load dictionary: %s", LZ4F_getErrorName(r));
        goto cleanup;
    }
    if (fwrite(out, 1, r, dest) != r) {
        ret = fail("error writing stdout");
        goto cleanup;
    }

    /* compress until end of file */
    for (;;) {
        size_t n = fread(in, 1, in_size, source);
        if (ferror(source)) {
            ret = fail("error reading stdin");
            goto cleanup;
        }
        if (n > 0) {
            r = LZ4F_compressUpdate(cctx, out, out_size, in, n, NULL);
            if (LZ4F_isError(r)) {
                ret = fail("%s", LZ4F_getErrorName(r));
                goto cleanup;
            }
            if (fwrite(out, 1, r, dest) != r) {
                ret = fail("error writing stdout");
                goto cleanup;
            }
        }
        if (n == 0 || feof(source))
            break;
    }

    r = LZ4F_compressEnd(cctx, out, out_size, NULL);
    if (LZ4F_isError(r)) {
        ret = fail("%s", LZ4F_getErrorName(r));
        goto cleanup;
    }
    if (fwrite(out, 1, r, dest) != r)
        ret = fail("error writing stdout");

cleanup:
    free(in);
    free(out);
    LZ4F_freeCompressionContext(cctx);
    return ret;
}

/* Decompress from file source to file dest until stream ends or EOF. */
static int inf_frame(FILE *source, FILE *dest,
                     const char *dict, size_t dict_size)
{
    int ret = EXIT_SUCCESS;
    LZ4F_dctx *dctx = NULL;
    void *in = NULL;
    void *out = NULL;
    size_t const in_size = 64 * 1024;
    size_t const out_size = 4 * 1024 * 1024; /* one block */
    size_t r;
    size_t last_hint = 0; /* hint of the last call: 0 when the frame was complete */

    r = LZ4F_createDecompressionContext(&dctx, LZ4F_VERSION);
    if (LZ4F_isError(r))
        return fail("out of memory");

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

        size_t consumed = 0;
        while (consumed < n) {
            size_t srcSize = n - consumed;
            size_t dstSize = out_size;
            r = (dict && dict_size > 0)
                ? LZ4F_decompress_usingDict(dctx, out, &dstSize,
                                            (const char *)in + consumed, &srcSize,
                                            dict, dict_size, NULL)
                : LZ4F_decompress(dctx, out, &dstSize,
                                 (const char *)in + consumed, &srcSize, NULL);
            if (LZ4F_isError(r)) {
                ret = fail("%s", LZ4F_getErrorName(r));
                goto cleanup;
            }
            if (srcSize == 0 && dstSize == 0) {
                ret = fail("lz4 decompression made no progress");
                goto cleanup;
            }
            if (fwrite(out, 1, dstSize, dest) != dstSize) {
                ret = fail("error writing stdout");
                goto cleanup;
            }
            consumed += srcSize;
            last_hint = r;
        }
    }

    /* a non-zero hint means the last frame needs more bytes */
    if (last_hint != 0)
        ret = fail("incomplete lz4 stream");

cleanup:
    free(in);
    free(out);
    LZ4F_freeDecompressionContext(dctx);
    return ret;
}

/* ============ raw mode: custom magicless container, no interoperability ============ */
/* Layout: repeated blocks of
       u32 le  origSize   (> 0)
       u32 le  compSize   (0 = origSize bytes follow uncompressed)
       compSize bytes
   terminated by a single u32 le 0. */

static int put_u32(FILE *dest, uint32_t v)
{
    unsigned char b[4] = { (unsigned char)(v & 0xff), (unsigned char)((v >> 8) & 0xff),
                           (unsigned char)((v >> 16) & 0xff), (unsigned char)((v >> 24) & 0xff) };
    return fwrite(b, 1, 4, dest) == 4 ? 0 : -1;
}

static int get_u32(FILE *source, uint32_t *v)
{
    unsigned char b[4];
    if (fread(b, 1, 4, source) != 4)
        return -1;
    *v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
         ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 0;
}

/* Compress from file source to file dest until EOF on source,
   with linked blocks and the dictionary as window prefix. */
static int def_raw(FILE *source, FILE *dest, int level,
                   const char *dict, size_t dict_size)
{
    int ret = EXIT_SUCCESS;
    LZ4_stream_t *s = NULL;
    LZ4_streamHC_t *shc = NULL;
    void *in = NULL;
    void *comp = NULL;
    int const comp_cap = LZ4_compressBound(BLOCK_IN);

    in = malloc(BLOCK_IN);
    comp = malloc((size_t)comp_cap);
    if (!in || !comp) {
        ret = fail("out of memory");
        goto cleanup;
    }

    if (level >= 1) {
        shc = LZ4_createStreamHC();
        if (!shc) {
            ret = fail("out of memory");
            goto cleanup;
        }
        LZ4_resetStreamHC(shc, level);
        if (dict_size > 0 && LZ4_loadDictHC(shc, dict, (int)dict_size) < 0) {
            ret = fail("cannot load dictionary");
            goto cleanup;
        }
    } else {
        s = LZ4_createStream();
        if (!s) {
            ret = fail("out of memory");
            goto cleanup;
        }
        if (dict_size > 0 && LZ4_loadDict(s, dict, (int)dict_size) < 0) {
            ret = fail("cannot load dictionary");
            goto cleanup;
        }
    }

    /* compress until end of file */
    for (;;) {
        size_t n = fread(in, 1, BLOCK_IN, source);
        if (ferror(source)) {
            ret = fail("error reading stdin");
            goto cleanup;
        }
        if (n == 0)
            break;

        int c = level >= 1
            ? LZ4_compress_HC_continue(shc, in, comp, (int)n, comp_cap)
            : LZ4_compress_fast_continue(s, in, comp, (int)n, comp_cap, 1);

        if (c <= 0 || (size_t)c >= n) {
            /* incompressible chunk: store it raw */
            if (put_u32(dest, (uint32_t)n) || put_u32(dest, 0) ||
                fwrite(in, 1, n, dest) != n) {
                ret = fail("error writing stdout");
                goto cleanup;
            }
        } else {
            if (put_u32(dest, (uint32_t)n) || put_u32(dest, (uint32_t)c) ||
                fwrite(comp, 1, (size_t)c, dest) != (size_t)c) {
                ret = fail("error writing stdout");
                goto cleanup;
            }
        }
    }

    /* end-of-stream marker */
    if (put_u32(dest, 0))
        ret = fail("error writing stdout");

cleanup:
    free(in);
    free(comp);
    LZ4_freeStream(s);
    LZ4_freeStreamHC(shc);
    return ret;
}

/* keep the last MAX_DICT_SIZE bytes of all output in win */
static void window_append(char *win, size_t *win_size, const char *data, size_t n)
{
    size_t keep = *win_size < MAX_DICT_SIZE ? *win_size : MAX_DICT_SIZE;
    if (n >= MAX_DICT_SIZE) {
        memcpy(win, data + (n - MAX_DICT_SIZE), MAX_DICT_SIZE);
        *win_size = MAX_DICT_SIZE;
    } else if (keep + n <= MAX_DICT_SIZE) {
        memcpy(win + keep, data, n);
        *win_size = keep + n;
    } else {
        size_t drop = keep + n - MAX_DICT_SIZE;
        memmove(win, win + drop, keep - drop);
        memcpy(win + (keep - drop), data, n);
        *win_size = MAX_DICT_SIZE;
    }
}

/* Decompress from file source to file dest until the marker or EOF.
   Each block is decoded against the last 64 KiB of already-written
   output, which equals the compressor's window. */
static int inf_raw(FILE *source, FILE *dest,
                   const char *dict, size_t dict_size)
{
    int ret = EXIT_SUCCESS;
    void *comp = NULL;
    void *out = NULL;
    char *win = NULL;
    size_t win_size = 0;

    comp = malloc((size_t)LZ4_compressBound(BLOCK_IN));
    out = malloc(BLOCK_IN);
    win = malloc(MAX_DICT_SIZE);
    if (!comp || !out || !win) {
        ret = fail("out of memory");
        goto cleanup;
    }

    /* the dictionary is the initial window */
    if (dict_size > 0) {
        memcpy(win, dict, dict_size);
        win_size = dict_size;
    }

    for (;;) {
        uint32_t orig, csize;
        if (get_u32(source, &orig)) {
            ret = fail("incomplete raw lz4 stream");
            goto cleanup;
        }
        if (orig == 0)
            break; /* end-of-stream marker */
        if (orig > BLOCK_IN) {
            ret = fail("invalid raw lz4 block");
            goto cleanup;
        }
        if (get_u32(source, &csize)) {
            ret = fail("incomplete raw lz4 stream");
            goto cleanup;
        }

        if (csize == 0) {
            /* uncompressed chunk */
            if (fread(out, 1, orig, source) != orig) {
                ret = fail("incomplete raw lz4 stream");
                goto cleanup;
            }
        } else {
            int d;
            if (csize >= orig || csize > (uint32_t)LZ4_compressBound(BLOCK_IN)) {
                ret = fail("invalid raw lz4 block");
                goto cleanup;
            }
            if (fread(comp, 1, csize, source) != csize) {
                ret = fail("incomplete raw lz4 stream");
                goto cleanup;
            }
            d = LZ4_decompress_safe_usingDict(comp, out, (int)csize, (int)orig,
                                              win, (int)win_size);
            if (d < 0 || (uint32_t)d != orig) {
                ret = fail("invalid or incomplete raw lz4 data");
                goto cleanup;
            }
        }

        if (fwrite(out, 1, orig, dest) != orig) {
            ret = fail("error writing stdout");
            goto cleanup;
        }
        window_append(win, &win_size, out, orig);
    }

cleanup:
    free(comp);
    free(out);
    free(win);
    return ret;
}

static int printUsage(int argc, char **argv)
{
    fprintf(stderr,
        "Usage: %s [-x] [-r] [-l level] [-d dict] < source > dest\n"
        "\n"
        "Compress (lz4) stdin to stdout, or extract (decompress) with -x.\n"
        "\n"
        "Options:\n"
        "  -x         extract (decompress) instead of compress\n"
        "  -r         raw block stream, without magic number and frame header;\n"
        "             only decodable by diclz4 itself, not interoperable\n"
        "  -l level   compression level, 0 (fast, default) to 12 (best);\n"
        "             levels 1 and up use LZ4_HC\n"
        "  -d dict    preset dictionary, read from file dict; when larger than\n"
        "             64 KiB, only the last 64 KiB is used; required for\n"
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
    int raw = 0;
    int level = 0;
    char *dict = NULL;
    size_t dict_size = 0;

    while ((opt = getopt(argc, argv, "rxl:d:")) != -1) {
        switch (opt) {
        case 'x': extract = 1; break;
        case 'r': raw = 1; break;
        case 'l':
            level = (int)parseOrDefault(optarg, level);
            if (level < 0) level = 0;
            if (level > LZ4HC_CLEVEL_MAX) level = LZ4HC_CLEVEL_MAX;
            break;
        case 'd':
            dict = load_dict(optarg, &dict_size);
            break;
        default:
            return printUsage(argc, argv);
        }
    }

    if (extract)
        return raw ? inf_raw(stdin, stdout, dict, dict_size)
                   : inf_frame(stdin, stdout, dict, dict_size);
    return raw ? def_raw(stdin, stdout, level, dict, dict_size)
               : def_frame(stdin, stdout, level, dict, dict_size);
}
