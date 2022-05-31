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

On the very high level, the Hopscotch algorithm takes a hash in order to index a bucket in the table. Each bucket will have a neighborhood of cells. Each cell is a bucket location, and buckets overlap. 

On searching for a place to put data, the hope is that the very first cell in the bucket will be empty so that data can simply be written into it. But, given some number of possible hashing collisions, the first bucket may be occupied.

If the first cell is occupied, the algorithm may look forward in memory for an empty cell. The algorithm does a linear probe for the empty cell. If it finds an empty cell within a neighborhood, the cell may be used to store data. But, which data?

The Hopscotch hash table algorithm attempts to cluster colliding data near the most definitive bucket. So, it attempts to swap data more loosely associated with the original bucket with the data causing the collision. Metaphorically, the hole that is located in the linear probe is moved closer to the bucket of collision. The swap is not done just once, but as many times as can possibly be done until the hole is as close to the collision bucket as possible. 

By keeping the newly placed data in as short a distance as possible to the collision, the search for it in future lookups will most likley fall within a small constant. Furthermore, the constant can be determined by the configured size of the neighborhood to the collision bucket. In this way, Hopscotch has maintains on average a flat matrix of entries avoiding worst case depths of lists which give rise to long (slow) linear probes.

The references are full of pictures and explanations. And, the original publication has a proof that the probability of finding a full neighborhood is within (1/H!), wich is a 3.8x10^-36 probability. Since this table is being used to index a fixed size memory, the size the table may be set to be some size larger than the number of memory elements (link list nodes) so that the hash table never becomes full. 

Besides never filling the hash table, it is expected that elements will be frequently freed. The linked list is an LRU, made so that elements will age out or so that elements will be pushed out when the newest of entries arrive. If elements being pushed out are to be sent into a growing list of shared LRU memory sections, then a hash table will be assigned to each new sections. So, each hash table will not suffer overruns. For further discussion on this topic, see the section on growing memory in shards [Extending the LRU](./extended_lru.md)


## Hopscotch Code





