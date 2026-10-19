/* In-process benchmark for the dicflate tools: isolates the compression
   algorithms from process startup, dynamic linking and I/O.

   Each section mirrors the corresponding tool's settings:
   - zlib:  raw deflate with preset dictionary (dicflate -r -d)
   - zstd:  magicless frame with preset dictionary (dicstd -r -d)
   - lz4:   linked blocks with preset dictionary (diclz4 -r -d)

   usage: bench <input> <dict> <iterations> <zlib-levels...> -- <zstd-levels...> -- <lz4-levels...>
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "zlib.h"

#define ZSTD_STATIC_LINKING_ONLY
#include "zstd.h"

#include "lz4.h"
#include "lz4hc.h"

static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static unsigned char *readall(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); exit(1); }
    fseek(f, 0, SEEK_END);
    long s = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(s ? s : 1);
    *n = fread(b, 1, s, f);
    fclose(f);
    return b;
}

int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s input dict iters <zlib-levels...> -- <zstd-levels...> -- <lz4-levels...>\n", argv[0]);
        return 1;
    }
    size_t in_n, dict_n;
    unsigned char *in = readall(argv[1], &in_n);
    unsigned char *dict = readall(argv[2], &dict_n);
    int iters = atoi(argv[3]);

    size_t cap = in_n + in_n / 2 + 1024;
    unsigned char *out = malloc(cap);

    int a = 4;
    int b = a; while (b < argc && strcmp(argv[b], "--")) b++;   /* zlib list end */
    int c = b < argc ? b + 1 : b; while (c < argc && strcmp(argv[c], "--")) c++; /* zstd list end */
    int d = c < argc ? c + 1 : c;                               /* lz4 list start */
    if (d < argc && !strcmp(argv[d], "--")) d++;

    /* zlib: raw deflate with preset dictionary, reusing one z_stream */
    for (int li = a; li < b; li++) {
        int level = atoi(argv[li]);
        z_stream strm;
        memset(&strm, 0, sizeof(strm));
        if (deflateInit2(&strm, level, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
            return 1;
        deflateSetDictionary(&strm, dict, dict_n);

        double t0 = now_ms();
        for (int i = 0; i < iters; i++) {
            deflateReset(&strm);
            strm.next_in = in;
            strm.avail_in = in_n;
            strm.next_out = out;
            strm.avail_out = cap;
            if (deflate(&strm, Z_FINISH) != Z_STREAM_END) return 1;
        }
        double dt = now_ms() - t0;
        size_t produced = cap - strm.avail_out;
        printf("zlib -l %2d  %9.1f us/op  %7.1f MB/s  out %8zu\n",
               level, dt * 1e3 / iters, in_n / 1024.0 / 1024.0 / (dt / iters / 1e3), produced);
        deflateEnd(&strm);
    }

    /* zstd: magicless frame with preset dictionary, reusing one CCtx */
    for (int li = b < argc ? b + 1 : b; li < c; li++) {
        int level = atoi(argv[li]);
        ZSTD_CCtx *cctx = ZSTD_createCStream();
        ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, level);
        ZSTD_CCtx_setParameter(cctx, ZSTD_c_format, ZSTD_f_zstd1_magicless);
        ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 0);
        ZSTD_CCtx_loadDictionary(cctx, dict, dict_n);

        double t0 = now_ms();
        size_t produced = 0;
        for (int i = 0; i < iters; i++) {
            produced = ZSTD_compress2(cctx, out, cap, in, in_n);
            if (ZSTD_isError(produced)) return 1;
        }
        double dt = now_ms() - t0;
        printf("zstd -l %2d  %9.1f us/op  %7.1f MB/s  out %8zu\n",
               level, dt * 1e3 / iters, in_n / 1024.0 / 1024.0 / (dt / iters / 1e3), produced);
        ZSTD_freeCStream(cctx);
    }

    /* lz4: linked blocks with preset dictionary (the diclz4 -r path);
       the dictionary is (re)loaded per iteration, like one CLI invocation */
    for (int li = d; li < argc; li++) {
        int level = atoi(argv[li]);
        size_t produced = 0;
        double t0 = now_ms();
        if (level >= 1) {
            LZ4_streamHC_t *shc = LZ4_createStreamHC();
            for (int i = 0; i < iters; i++) {
                LZ4_resetStreamHC(shc, level);
                if (dict_n) LZ4_loadDictHC(shc, (const char *)dict, (int)dict_n);
                produced = LZ4_compress_HC_continue(shc, (const char *)in, (char *)out,
                                                    (int)in_n, (int)cap);
                if (produced <= 0) return 1;
            }
            double dt = now_ms() - t0;
            printf("lz4  -l %2d  %9.1f us/op  %7.1f MB/s  out %8zu\n",
                   level, dt * 1e3 / iters, in_n / 1024.0 / 1024.0 / (dt / iters / 1e3), produced);
            LZ4_freeStreamHC(shc);
        } else {
            LZ4_stream_t *s = LZ4_createStream();
            for (int i = 0; i < iters; i++) {
                LZ4_resetStream(s);
                if (dict_n) LZ4_loadDict(s, (const char *)dict, (int)dict_n);
                produced = LZ4_compress_fast_continue(s, (const char *)in, (char *)out,
                                                      (int)in_n, (int)cap, 1);
                if (produced <= 0) return 1;
            }
            double dt = now_ms() - t0;
            printf("lz4  -l %2d  %9.1f us/op  %7.1f MB/s  out %8zu\n",
                   level, dt * 1e3 / iters, in_n / 1024.0 / 1024.0 / (dt / iters / 1e3), produced);
            LZ4_freeStream(s);
        }
    }
    return 0;
}
