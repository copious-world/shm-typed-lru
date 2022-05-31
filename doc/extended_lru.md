## Extending LRU with Slabs

It may help to think of each LRU and Hash Tables as a pair and call the pair a **slab**.

At initialization, the code for this module shm-typed-lru will make it possible for an application to set up one slab. The same module can set up more slabs.

As of this writing the module does not itself create more slabs automatically if the LRU becomes full. And, it searches just one slab. 

It is debatable that more needs to be done with this module given that an application (also meant as a data service layer) can add more slabs itself. The only requirement would be that the application needs to retain superfluous data and move it between sabs or to other processors as it sees fit. 

Currently, the application decides when it will run evictions from the LRU. The evictions will pull out those elements that are within a cuttoff time. The application makes a call to ***run\_lru\_eviction***.

The method, ***run\_lru\_eviction***, will return a list of hashes that have been removed. These are the hashes of values stored in the LRU. With this call the application would need a second storage or value storage management process to retieve values.

## Retrieving Hash-value pairs

Using another call (TBD), the process running evictions can get hash-value pairs. The application process can then insert the values into a seconday slab. The processes using the slabs can then decide if they want to search the second slab for a value given search fails on the first slab. 

It may also be possible to set up more than one thread to search for a hash on primary and seconday slabs at the same time.



## Telling other processes about the new slab

First, take note that each slab will have its own mutex. The process running evictions should lock the resource. After the eviction runs, the resource should be unlocked. Then, a second slab may be locked and values may be inserted in a batch.

If the processes that is moving things to a second slab makes a new slab for the first time that it is needed, or if it is removing the slab when the average activity for searches subsides, then other processes will have to be aprised of the existence of the slab. The other processes have a choice of using the second slab. But, they need to know about it and the new mutex that goes with it. 

How the processes communicate with each other is part of the application. For example, the processes may use node.js IPC for sending messages. Alternatively, a single shared memory segment may be used to set up a communication channel where each processes keeps a region with some maximal space for mutexes, some space for indicating shared memory segemnt ids, etc. Processes can check on the state of things there. However, it may still be best to signal the processes to look in the table.




