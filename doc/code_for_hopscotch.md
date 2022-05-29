## Code for Hopscoth


The hopscotch algorithm is well understood. The aim of this documentation is to make the code, C++ header files accessible to those who might use or maintain it.

The code is based on Linux library code for the Hopscotch algorithm. But, the code differs from that since the table is stored in a shared memory structure and does not rely on calls to system memory allocation available in **libc**.

### <u>Code location</u>

The hopscotch implementation can be found in **src/node\_shm\_HH.h**.

## Hopscotch Overview

This is a short overview with references to other explanations. Hopefully, this will acclamate the reader to the problem addressed in coding the tables. 

Here are some references: 

* [Wikipedia](https://en.wikipedia.org/wiki/Hopscotch_hashing)
* [Original Publication](http://mcg.cs.tau.ac.il/papers/disc2008-hopscotch.pdf)

