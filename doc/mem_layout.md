## LRU and Hash Table Memory Layout

There are two tables in use when both the LRU and the associated hash table are in use. 

Initialization of the two sections utilizes two different methods. 

* **initLRU**
* **initHopScotch**

**Note:**
> It is possible to use the LRU without initializing a hash table.
***initHopScotch*** makes a call to an ***LRU\_cache*** method, *set\_hash\_impl*. This call tells the LRU to put indexes to be looked up later into the hash table. Otherwise, indexes are placed in a C++ STL map. In fact, it is `unordered_map<uint64_t,uint32_t>`. This map will reside in the application memory, hence inside the node.js application that calls the methods. As such, the map will not be shared between processes. It may be preferable to use it for small applications residing in one processes. But, for such applications, it may be better to just use JavaScript data structure.

For the most common operations both segments will be requested and then passed on to the C++ code for initialization. 

Recall this example form **shm-lru-cache**:

```
        if ( this.initializer ) {
            let sz = ((this.count*this.record_size) + LRU_HEADER)
            this.lru_buffer =  shm.create(sz);
            this.lru_key = this.lru_buffer.key
            this.count = shm.initLRU(this.lru_key,this.record_size,sz,true)
            //
            sz = (2*this.count*(WORD_SIZE + LONG_WORD_SIZE) + HH_HEADER_SIZE)
            this.hh_bufer = shm.create(sz); 
            this.hh_key = this.hh_bufer.key
            shm.initHopScotch(this.hh_key,this.lru_key,true,this.count)
            
            ...
            
       }

```

Take note that the keys for the LRU buffer and the hopscotch buffer are passed to the initialization methods. 

In the ***initHopScotch*** method, both keys are passed. The C++ code makes the LRU the commander of the hash table. Call to get\_el\_hash pass throught the LRU, which can choose which map to look for an index to its element. Once the element is found, the LRU can use the offset into its memory segment to pull out the data that is being sought.

### Header Files

If you look in the C++ header files, you will find two classes:

* LRU\_cache&nbsp;&nbsp;-- inside node\_shm\_LRU.h
* HH\_map&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;&nbsp;-- inside node\_shm\_HH.h


These classes prepare their memory region from within their constructors. Each makes a call to *setup\_region*.

Notice that in the LRU_cache class the class implementation refers to an interface to a map, *hmap\_interface*. As such, the LRU\_cache implementation does not know that it uses a Hopscotch hash map.

### LRU\_cache memory structure

***setup\_region*** initializes two doubly linked lists. One lists is emtpy, except for a header and a tail. This is the list of allocated elements. The header and tale are at the foot of memory. A second list header is place directly after the first two headers. This third element, and second header, is the start of free memory. The rest of the memory is threaded from the start of the free memory to the end of the section, with the last position given to the tale of free memory. The tale of free memory will point its *next* pointer to (usigned)(-1).

New element allocations always are at the top of free memory. When an element is used, it is put at the top of the allocated list, after it is unlinked from the free list. When an element is deleted, it is unlinked from the free list and pushed onto its end.

All addresses to elements are kept as offsets from the start of the shared segment. Any process requesting an element will be work with offsets which may be obtained from the hash table.

 
### HH\_map memory structure

***setup\_region*** initializes to areas within its shared structure, prefixed by a header.

The header includes three fields:

* count --- current count of elements
* max\_n --- the maximum number elements that can be stored
* neighbor --- the size of the hopscotch neighborhood. (Kept here for sharing)

Immediately after the header is the value regions. For this application of the hopscotch hash, values are 64 bit unsigned numbers, or just bits which may be anything, but here used for offsets into the LRU. The region size is max\_n*(element size) or `sizeof(uint64_t)*max_n`.

The next section of memory is the regions of buckets (\_region\_H from the hopscotch documentation). This is 32 bits per bucket, the 32 bits being neighborhood maps. The initializer sets aside max\_n of these bit maps for future use. The two regions, excluding the header are cleared to zero.

### Diagrams

#### LRU

<>

#### Hopscotch


<>




