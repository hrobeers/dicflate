dicflate
========

DEFLATE tool with support for preset dictionaries using zlib

dicflate is a stream filter: it reads from stdin and writes to stdout, so it
can be used in pipelines. It can produce both standard zlib-wrapped and raw
DEFLATE streams, with optional preset dictionaries and a configurable
compression level.

Usage
-----

```
Usage: dicflate [-x] [-r] [-l level] [-d dict] < source > dest

Compress (deflate) stdin to stdout, or extract (inflate) with -x.

Options:
  -x         extract (decompress) instead of compress
  -r         raw DEFLATE stream, without zlib header and checksum
  -l level   compression level, 0 (none) to 9 (best); default 6
  -d dict    preset dictionary, read from file dict (max 32 KiB);
             required for extraction if used during compression
```

Notes:
- Compressed data goes to stdout, diagnostics (`Dict size`, `Dict adler`,
  errors) go to stderr. Exit status is non-zero on failure.
- At most the first 32 KiB (the DEFLATE window size) of the dictionary file
  is used.
- A non-numeric `-l` value is silently ignored and the default level is used.

Quickstart
----------
```bash
# Compress
dicflate -d my.dict < uncompressed > compressed.data

# Extract
dicflate -x -d my.dict < compressed.data > uncompressed
```
For more examples see [tests](test/).

Use cases
---------

### Plain zlib stream
The default output is a standard zlib stream (RFC 1950): a 2-byte header,
the DEFLATE data and an adler32 checksum. It is compatible with any zlib
implementation, e.g. `python3 -c 'import sys,zlib; sys.stdout.buffer.write(zlib.decompress(sys.stdin.buffer.read()))'`.

```bash
dicflate < input > output.zlib
dicflate -x < output.zlib > input
```

### Raw DEFLATE stream
`-r` emits a raw DEFLATE stream (RFC 1951) without the zlib header and
checksum, saving 6 bytes per stream. Use it when many small streams are
stored together (e.g. one per JSON document).

```bash
dicflate -r < input > output.deflate
dicflate -rx < output.deflate > input
```

### Compression level
`-l` selects the zlib compression level, from 0 (stored, no compression)
to 9 (maximum). The default is 6.

```bash
dicflate -l 9 < input > output.zlib
dicflate -l 0 < input > output.zlib   # store, no compression
```

### Preset dictionary
`-d` primes the compression window with the contents of a dictionary file,
so strings that already occur in the dictionary are encoded as
back-references. This makes small inputs compress far better than they
otherwise could. Extraction needs the same dictionary.

```bash
# Compress a small document using a large document as dictionary
dicflate -d schema.json < record.json > record.dfl

# Extract
dicflate -x -d schema.json < record.dfl > record.json

# Works with raw streams too
dicflate -r -d schema.json < record.json > record.dfl
dicflate -rx -d schema.json < record.dfl > record.json
```

With a zlib-wrapped stream (`-d` without `-r`) the stream stores the
dictionary's adler32 checksum, so a mismatched dictionary is detected.
A raw stream has no such check: a mismatched dictionary yields garbage
or a generic "invalid or incomplete deflate data" error instead of a
clear diagnostic.

### Building a dictionary
A dictionary is most effective when it contains the recurring text of
your documents. For JSON, [json-skeleton.sh](json-skeleton.sh) strips
all leaf values from a sample document and keeps the keys and structure,
which makes an ideal dictionary:

```bash
# Skeleton of one document (keys + structure, values removed)
./json-skeleton.sh < sample.json > skeleton.json

# Use it as dictionary for all similar documents
./json-skeleton.sh '["uuid","timestamp"]' < sample.json > skeleton.json  # blacklist keys
dicflate -r -d skeleton.json < record.json > record.dfl
```

The dictionary can also simply be one or more representative documents,
as in the [tests](test/).

Interoperability
----------------
Streams produced by dicflate are standard zlib/DEFLATE and can be
processed by any zlib implementation. For raw streams with a preset
dictionary in Python (3.3+):

```python
import zlib
dict_ = open("schema.json", "rb").read()
# decompress a `dicflate -r -d schema.json` stream:
d = zlib.decompressobj(-15, zdict=dict_)
data = d.decompress(open("record.dfl", "rb").read())
```
and likewise for compression via `zlib.compressobj(9, zlib.DEFLATED, -15, 8,
zlib.Z_DEFAULT_STRATEGY, zdict=dict_)`.

WebAssembly
-----------
A WebAssembly build is available in [wasm/](wasm/). Build it with
[Emscripten](https://emscripten.org/) and its zlib port:

```bash
cd wasm && make
```

This produces `dicflate.js` and `dicflate.wasm` for use in Node.js and
browser environments.

#### Dependencies
- C compiler (gcc) and zlib
- [Emscripten](https://emscripten.org/) (only for the WebAssembly build)
- [Bats: Bash Automated Testing System](https://github.com/sstephenson/bats) (for tests)

Build and test
--------------
```bash
make        # build ./dicflate
make check  # build and run the test suite
```

Rationale
---------

zlib's DEFLATE algorithm supports the use of preset dictionaries for compression of short strings (sequence of bytes).
Unfortunately this functionality is not exposed by any of the mainstream compression tools.
dicflate aims to fill this void.

Preset dictionaries can be usefull to reduce the size of self-describing serialization formats (e.g. JSON), without the need to shorten property names or other verbosities.
