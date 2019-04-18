dicflate
========

DEFLATE tool with support for preset dictionaries using zlib


Quickstart
----------
```bash
# Compress
dicflate -d my.dict < uncompressed > compressed.data

# Extract
dicflate -x -d my.dict < compressed.data > uncompressed
```
For more examples see [tests](test/).

#### Dependencies
- C compiler (gcc)
- [Bats: Bash Automated Testing System](https://github.com/sstephenson/bats) (for tests)


Rationale
---------

zlib's DEFLATE algorithm supports the use of preset dictionaries for compression of short strings (sequence of bytes).
Unfortunately this functionality is not exposed by any of the mainstream compression tools.
dicflate aims to fill this void.

Preset dictionaries can be usefull to reduce the size of self-describing serialization formats (e.g. JSON), without the need to shorten property names or other verbosities.
