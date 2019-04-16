dicflate
========

DEFLATE tool using preset dictionaries


Quickstart
----------
```bash
make check
```

#### Dependencies
- C compiler (gcc)
- Bash
- [Bats: Bash Automated Testing System](https://github.com/sstephenson/bats) (for tests)


Rationale
---------

zlib's DEFLATE algorithm supports the use of preset dictionaries for compression of short strings (sequence of bytes).
Unfortunately this functionality is not exposed by any of the mainstream compression tools.
dicflate aims to fill this void.
