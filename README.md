dicflate
========

DEFLATE tool using preset dictionaries


Quickstart
----------
```bash
dicflate -d my.dict < uncompressed > compressed.data
dicflate -x -d my.dict < compressed.data > uncompressed
```

#### Dependencies
- C compiler (gcc)


Rationale
---------

zlib's DEFLATE algorithm supports the use of preset dictionaries for compression of short strings (sequence of bytes).
Unfortunately this functionality is not exposed by any of the mainstream compression tools.
dicflate aims to fill this void.
