##IPC shared memory LRU cache for Node.js  

##### Based upon shm-typed-array with the following state of testing.

![travis](https://travis-ci.org/ukrbublik/shm-typed-array.svg?branch=master)

``` bash
For shm-type-array, please refer to:
"author": {
    "name": "Oblogin Denis",
    "email": "ukrbublik@gmail.com",
    "url": "https://github.com/ukrbublik"
  }
```

##### shm-typed-lru tests and npm (coming soon)


# Purpose

This node.js module provides a simple LRU for **fixed sized elements** residing in shared memory. It makes the LRU available to more tha one process. This module does not provide all of the communication that might take place between processes sharing an LRU. It makes the communication possible and manages the object that they share. Please see references to other modules for more features.

Optionally, this module provides Hopscotch hashing for associating data with LRU indecies. If the Hopscotch hashing is used, it is shared between attached processes. Modules do not have to communicate about the hashes. There is no locking however, so the application needs to provide any necessary locks and signals.

(The author chose not to clone the original repository since there many changes to C++, and changing the C++ was more expedient than trying to manage separate stacks. In the future, this problem, not necessarily apparent to all the upstream repositories will be resolved.)

>>The module, shared-typed-array was written to manage a collection of shared memory regions. Applications of shared-typed-lru may make use of that to enhance communication. Or, more than one LRU may be managed for different record sizes.
  
# Install

*This module is using cmake-js for its C++ build.*

So, the following modules should be installed in order to build this module:

```
npm install nan -g
npm install cmake-js -g
```


After those modules are installed, this installation should go smoothly:

``` bash
$ npm install shm-typed-lru
$ npm test
```

Manual build:

``` bash
cmake-js compile
node test/example.js
```
Windows is not supported.

## General Usage

For any operations to take place on shared memory, the application program must first create a shared memory segment by calling the **create** method. Only one process should do this. In otherwords appoint a process as the manager (master of cerimonies).

Here is an example from shm-lru-cache:

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

Notice that in this example, the calling application is allowing the **shm-typed-lry** module to create a key for finding the segment in the OS shared memory tables. 

A way to look at shared memory in the OS (e.g. Linux) is to use the *ipc* based commands.

```
ipcls -m # lists shared memory sections
ipcrm -m id # removes the shared memory section (if something crashes say)
```

In the example both an LRU section and a hash table section have been created.


## API

> In the following, the **key** parameter is the identifier of the shared memory section. Please avoid confusing this with a key for a value, which will usually be a hash key or perhaps an element offset.
> 
> The **key** is a reference to a shared memory structure obtained by **shm.create**
> 

### - generic return values

* If a segment exists in the OS table, but the module has lost track of it. Most of the APIs, except those related to the mutex will return a Boolean value of **false**.  Mutex methods return a number -3 instead of **false**. The Boolean values are used for testing the lock. 

* If the segment identified by the **key** is not known to the OS, a number -1 will be returned.

* Other values relate to the success or failure of the operation. A generic success value is the Boolean value **true**, while failure will be the number -2. Some methods return a positive number, e.g. a hash value, an index, or a string.


## Inherited Methods
For the following API's please read the description at this repository: [shm-typed-array](https://github.com/ukrbublik/shm-typed-array)

See [example.js](https://github.com/ukrbublik/shm-typed-array/)

### shm.create (count, typeKey [, key])
### shm.get (key, typeKey)
### shm.detach (key)
### shm.detachAll ()
#### Types
### shm.getTotalSize()
### shm.LengthMax

## Here are the API's added in this repository:

### initLRU(key,record_size,region\_size,i\_am\_initializer)

**key** is the shared memory region key

> This will create an LRU data structure in the shared memory region. The record size is new information not give to shm.create. The region size should be the same (see *count*). The basic communication use is for one process to be the master of the region. Call that process the initializer. Initialization creates data structurs. When *i\_am\_initializer* is set to false, the process using this module will read the existing data structures. 

> Within the library, each process will have a hash map (C++ unorderd_map) that maps hashes to the offsets (from the start of the shared block). 

**Returns**: the number of elements that may be stored in the region. Failure values as above.

### getSegmentSize(key)

**key** is the shared memory region key

> This returns the length of just that region.

**Returns**: shared memory region length as a number. Failure values as above.

### lru\_max\_count(key)

**key** is the shared memory region key

> This fetches the maximum number of elements of size *record\_size*. *(good to use in initHopScotch)*.

**Returns**: the maximum number of elements that may be stored. Failure values as above.


### set(key,value,hash,[index])

**key** is the shared memory region key

> The application creates a hash key fitting UInt32. The hash key becomes the identifier for the stored value. The value should fit within the *record\_size* provided to initLRU. An index, used to distinguish the element in case of a hash collision may be optionally passed. *index* defaults to zero.
> 
> > Internal to the hash table implementation, the index is stored in the high word of the table's hash key, and the low word stores the hash. The hash and the index are passed in as 32 bit words. The internal hash key is a 64 bit word. (Because of the word size difference, almost an artifact of using node api ia Nan, the application may decide to split 64bit hashes that it creates into these two parts.)

**Returns**: UInt32 offset to the element record. (Care should be taken when using the index versions of get, set, del.) Failure values as above.

### get\_el\_hash(key,hash,[index])

**key** is the shared memory region key

> Retuns the value previously stored in conjunction with the provided hash. If the element was stored with an index, the same index should be passed. 

**Returns**: the stored value as a string if successful. Failure values as above.

### del\_key(key,hash,[index])

**key** is the shared memory region key

> Deletes the record (moves it to the free list), and removes the hash from  any hash map data structures governed by the module. If the element was stored with an index, the same index should be passed.

**Returns**: Boolean value true if it succeeds. Failure values as above.

### get\_el(key,index)

**key** is the shared memory region key

**Returns**: the value previously stored in conjunction with the index returned from *set*. (Note: if the element has been deleted, the value, prepended with the string "DELETED" will be accessible until it is overwritten. The element will not be accesible by the hash version of this method.) Failure values as above.

### del\_el(key,index)

**key** is the shared memory region key

> Deletes the record (moves it to the free list), and removes the internally stored hash from any hash map data structures governed by the module.

**Returns:** **true**. Failure values as above.

### get\_last\_reason(key)

**key** is the shared memory region key

> If an error occurs, as may be indicated by negative return values from methods, this method reports the reason published by the module. Accessing the reason will cause it to be reset to "OK", its initial state.

**Returns:** A string assigned to the last error. Failure values as above.

### reload\_hash_map(key)

**key** is the shared memory region key

> Synchronizes the internal hash map for the calling process with the existing allocated data.

**Returns:** **true**. Failure values as above.

### reload\_hash\_map\_update(key,share\_key)

**key** is the shared memory region key

> Does the same as *reload\_hash_map*, but only inspects elements with a share_key matching the one passed to this method.

**Returns:** **true**. Failure values as above.

### run\_lru\_eviction(key,cutoff\_time,max\_evictions)

**key** is the shared memory region key

> The application process sets the policy as to when to run the eviction method. This method deletes all elements that are prior to the time cutoff up to as many as max\_evictions.

**Returns:** A list of hashes that were removed. Failure values as above.

### run\_lru\_eviction\_get\_values(key,cutoff\_time,max\_evictions)

**key** is the shared memory region key

> This is the same as run\_lru\_eviction except that instead of returning a list of hashes, it returns a map object with hashes as keys and values stored under the key as the map object values.  The values will be strings.

**Returns:** A map table hash to value. Failure values as above.


### set\_share\_key(key,index,share\_key)

**key** is the shared memory region key

> Access an element by index and sets its share_key for use in reloading hash maps. In some case the tables may be filtered on reloading, so that only those with the share key are put back in the hash map. 

> If the shared hash map is not used. A process may set the share key and create an map table in the memory of the process where the table only includes those hashes that have the same share key. (This has been useful for debugging. But, some essoteric programs may beed this feature.)

**Returns:** **true**. Failure values as above.

### debug\_dump\_list(key,backwards)

**key** is the shared memory region key

> This method dumps a JSON format of all the elements currently allocated in the LRU. Formats its output for the terminal. This has no options for formatting.

**Returns:** **true**. Failure values as above.


### initHopScotch(key,lru\_key,am\_initializer,max\_element\_count)

**key** is the shared memory region key containing the hash map. **lru\_key** is the key of the LRU region.

> This method, **initHopSchotch**, initializes Hopscotch hashing in a shared memory regions, identified by *key*. It associates the hash table with a previously allocated region intialied by **initLRU**. Use *lru\_key*, the shared memory key for the LRU list so that the module may find the region. The parameter, *am\_initializer*, tells the module if the regions should be initialized for the process or if it should be picked up (copying the header). There should be just one process that sets *am\_initializer* to true. The max_element_count should be the same or bigger than the element count returned from the LRU methods, *lru\_max\_count* or *initLRU*. Having more is likey a good idea. Depending on how good your hash is, up to twice as much might be OK.

**Throws**: If there is no shared memory segment and some other condition than ENOENT or EIDRM was found.

**Throws**: "Bad parametes for initLRU" indicating that the initialization failed. (Specifically, these are allocations to objects tracking the tables in the current process)

**Returns:** **true** on success. Otherwise: the key that was passed if the module has lost track of the segment; -1 if the segment address can't be determined; Failure values as above in relation to the LRU segment.


# Cleanup
This library does cleanup of created SHM segments only on normal exit of process, see [`exit` event](https://nodejs.org/api/process.html#process_event_exit).

If you want to do cleanup on terminate signals like `SIGINT`, `SIGTERM`, please use [node-cleanup](https://github.com/jtlapp/node-cleanup) / [node-death](https://github.com/jprichardson/node-death) and add code to exit handlers:
```
shm.detachAll();
```


# Usage
See [example.js](https://github.com/copious-world/shm-typed-lru/blob/master/test/example.js)

``` js
const cluster = require('cluster');
const shm = require('../index.js');
const assert = require('assert');


var buf, arr, arr2D2;
if (cluster.isMaster) {
	// Assert that creating shm with same key twice will fail
	var key = 1234567890;
	var a = shm.create(10, 'Float32Array', key);
	var b = shm.create(10, 'Float32Array', key);
	assert(a instanceof Float32Array);
	assert(a.key == key);
	assert(b === null);
	//
	// Assert that getting shm by unexisting key will fail
	var unexisting_key = 1234567891;
	var c = shm.get(unexisting_key, 'Buffer');
	assert(c === null);

	// Test using shm between 2 node processes
	buf = shm.create(4096); //4KB
	arr = shm.create(1000000, 'Float32Array'); //1M floats
	//bigarr = shm.create(1000*1000*1000*1.5, 'Float32Array'); //6Gb
	assert.equal(arr.length, 1000000);
	assert.equal(arr.byteLength, 4*1000000);
	buf[0] = 1;
	arr[0] = 10.0;
	//bigarr[bigarr.length-1] = 6.66;
	console.log('[Master] Typeof buf:', buf.constructor.name,
			'Typeof arr:', arr.constructor.name);


	arr2D2 =  shm.create(1000000); //1M bytes

	let cache = shm.initLRU(arr2D2.key,250,10000)
	console.log("CACHE: " + cache)
	let cache2 = shm.initLRU(arr2D2.key,250,10000)
	console.log("CACHE: " + cache2)
	console.log("Segment Size: " + shm.getSegmentSize(arr2D2.key))

	let hash = 134
	let el_data = "this is at test"
	let el_id = shm.set(arr2D2.key,hash,el_data)
	console.log("arr2D2 >> el_id: " + el_id)

	let value = shm.get_el(arr2D2.key,el_id)
	console.log("arr2D2 >> value: " + value)

	el_data = "we test different this time"
	el_id = shm.set(arr2D2.key,hash,el_data)
	console.log("arr2D2 >> el_id: " + el_id)

	value = shm.get_el(arr2D2.key,el_id)
	console.log("arr2D2 >> value: " + value)


	let hash2 = 136

	el_data = "we wolly gollies abren"
	el_id = shm.set(arr2D2.key,hash2,el_data)
	console.log("arr2D2 >> el_id: " + el_id)

	value = shm.get_el(arr2D2.key,el_id)
	console.log(`arr2D2 >> value[${el_id}]: ` + value)
	
	if ( shm.del_el(arr2D2.key,el_id) === true ) {
		console.log("deleted")
		value = shm.get_el(arr2D2.key,el_id)
		console.log(`post del arr2D2 >> value[${el_id}]: ` + value)
	}
	/*
	*/

	let hashes = []
	for ( let i = 0; i < 10; i++ ) {
		hash2++;
		hashes.push(hash2)
		el_data = `GEEE ${i} wolly gollies abren`
		shm.set(arr2D2.key,hash2,el_data)
	}

	hashes.forEach(hsh => {
		value = shm.get_el_hash(arr2D2.key,hsh)
		console.log(`fetching after adding arr2D2 >> hash[${hsh}]: value[${value}]`)
	})

	shm.debug_dump_list(arr2D2.key)

	for ( let k = 0; k < 3; k++ ) {
		let n = hashes.length
		let j = Math.floor(Math.random()*n);
		//
		let tst_hash = hashes[j]
		console.log(tst_hash)
		let tst_value = shm.get_el_hash(arr2D2.key,tst_hash)
		console.log(`fetching after adding arr2D2 >> hash[${tst_hash}]: value[${tst_value}]`)
	}

	shm.debug_dump_list(arr2D2.key)
	console.log("-----------------------------------------------")
	shm.debug_dump_list(arr2D2.key,true)

	//shm.detach(arr2D2.key)
	//let cache3 = shm.initLRU(arr2D2.key,250,10000)
	//console.log("CACHE: " + cache3)

	var worker = cluster.fork();
	worker.on('online', () => {
		worker.send({
			msg: 'shm',
			bufKey: buf.key,
			arrKey: arr.key,
			//bigarrKey: bigarr.key,
		});
		//
		let hash_str = JSON.stringify(hashes)
		setTimeout(() => {
			let message = {
				'msg'		: 'lru',
				'bufKey'	: arr2D2.key,
				'hashList'	: hash_str
			}
			worker.send(message)
			setTimeout(() => {
				let n = hashes.length
				let j = Math.floor(Math.random()*n);
				//
				let el_hash = hashes[j]
				console.log(el_hash)
				if ( shm.del_key(arr2D2.key,el_hash) === true ) {
					console.log(`deleted ${el_hash}`)
				}
				j = Math.floor(Math.random()*n);
				el_hash = hashes[j]
				console.log(el_hash)
				if ( shm.del_key(arr2D2.key,el_hash) === true ) {
					console.log(`deleted ${el_hash}`)
				}
				j = Math.floor(Math.random()*n);
				el_hash = hashes[j]
				console.log(el_hash)
				if ( shm.del_key(arr2D2.key,el_hash) === true ) {
					console.log(`deleted ${el_hash}`)
				}
				//
				let message = {
					'msg'		: 'lru-reload',
					'bufKey'	: arr2D2.key,
					'hashList'	: hash_str
				}
				worker.send(message)
				setTimeout(groupSuicide,3000)
			}, 1000)
		}, 1000)
		//
		var i = 0;
		let test_intr = setInterval(() => {
			buf[0] += 1;
			arr[0] /= 2;
			console.log(i + ' [Master] Set buf[0]=', buf[0],
				' arr[0]=', arr ? arr[0] : null);
			i++;
			if (i == 5) {
				clearInterval(test_intr)
			}
		}, 1500);
	});
} else {  // child process....
	process.on('message', function(data) {
		var msg = data.msg;
		if (msg == 'shm') {
			buf = shm.get(data.bufKey);
			arr = shm.get(data.arrKey, 'Float32Array');
			//bigarr = shm.get(data.bigarrKey, 'Float32Array');
			console.log('[Worker] Typeof buf:', buf.constructor.name,
					'Typeof arr:', arr.constructor.name);
			//console.log('[Worker] Test bigarr: ', bigarr[bigarr.length-1]);
			var i = 0;
			setInterval(function() {
				console.log(i + ' [Worker] Get buf[0]=', buf[0],
					' arr[0]=', arr ? arr[0] : null);
				i++;
				if (i == 2) {
					shm.detach(data.arrKey);
					arr = null; //otherwise process will drop
				}
			}, 500);
		} else if ( msg == 'lru' ) {

			let key = data.bufKey;
			let hashes = JSON.parse(data.hashList)
			shm.get(key);
			shm.initLRU(key,250,10000,false)

			hashes.forEach( hsh => {
				value = shm.get_el_hash(key,hsh)
				if ( value == -2 ) {
					let raison = shm.get_last_reason(key)
					console.log(raison)
				}
				console.log(`CHILD arr2D2 >> hash[${hsh}]: value[${value}]`)		
			})	
		} else if ( msg == 'lru-reload' ) {
			let key = data.bufKey;
			shm.reload_hash_map(key)
			let hashes = JSON.parse(data.hashList)
			hashes.forEach( hsh => {
				value = shm.get_el_hash(key,hsh)
				if ( value == -2 ) {
					let raison = shm.get_last_reason(key)
					console.log(raison)
				}
				console.log(`CHILD AFTER RELOAD >> hash[${hsh}]: value[${value}]`)		
			})
		} else if (msg == 'exit') {
			process.exit();
		}
	});
}

function groupSuicide() {
	if (cluster.isMaster) {
		for (var id in cluster.workers) {
		    cluster.workers[id].send({ msg: 'exit'});
		    cluster.workers[id].destroy();
		}
		process.exit();
	}
}

```

**Output:**

An example is provided in the repository: [test output](https://github.com/copious-world/shm-typed-lru/blob/master/test/exampe-output.txt)

##Internal Documentation

In this section the reader will find some information about the internal operations, the layout of memory, etc. While the API has been given, nothing has been said about the algorithms for hashing or other mechanisms such as provisions for interprocess mutex operation. This information is provided via the links to markdown documents stored in this repository.

### <u>Hash table memory layout</u>

The basic shared memory module provided by **shm-typed-array** allows for the management of more than one section of memory. **Shm-typed-lru** takes advantage of this in order to store larger data elements in a fixed managed region while smaller references are stored in a hash table for reverse lookup.

The larger data elements, fixded in size, are kept with in a doubly linked list with fixed element offsets. The nodes of the doubly linked list are taken from a free element list embedded in data section of memory, a single shared memory segment.

The Hopscotch hash table is kept in another shared memory segment. Lookup is done by searching the Hopscotch table in order to obtain an offset to an allocated node in the managed memory section.

**A continuation of this description can be found here:** [memory layout](./doc/mem_layout.md)

### <u>Hopscotch Hash Alogrithm</u>

One of the better hashing algorithms is the hopscotch hash. Hash algorithms often assume that their hashes will address buckets imperfectly. As a result more than one element will fall into the same bucket. In some hash schemes, a linked list is the data structure chosen for bucket storage. Some hashes and data may result in short lists. But, in more extreme situations, many data elements may map into a sinlge bucket. In such situations, the storage becomes a linked list with ever growing time for search, insertion, and deletion. 

The Hopscotch hash allows for some amount of collision and provides binary pattern data structures for managing insertion in such a way that bucket search can be optimized.

Hopscotch hashing also easily resides in a single slab of shared memory. In this way, more than one process can find an element stored there.

**Find implementation details here:** [code for Hopscotch](./doc/code_for_hopscotch.md)

### <u>Mutex Sharing and Operations</u>

More than one process may operate on the shared memory regions containing the managed memory and the Hopscotch hash tables. These operations are best protected by a locking mechanism. The locking mechanism is exposed to the application. An application could call the library in an unprotected way or opt for some its own guarding of the data structures.

However, shm-typed-lru provides multi process mutex locking. The use of the POSIX mutex in a shared memory section is a fairly well known trick. The POSIX mutex operates easily enough within a process between threads. The single process has access to the memory for the lock data structures. With just a little modification, the shared memory can be used since the lock communication is via parameters in shared memory. POSIX mutex routines provide ways to specify the use of the share memory section.

The shm-type-lru module provides methods for locking and locking the mutex. Applications may call these to create critical sections within separate processes.

**Find implementation details here:** [Mutex Operations](./doc/mutex_ops.md)

### <u>Data storage with timestamps</u>

The shm-typed-lru module is an LRU. Elements that reside to long in the shared memory may be discarded. The module leaves it up to the application to decide how and when to remove elements. One method, **run\_lru\_eviction**, can be called to throw out elements that are too old. The method returns a list of elements being discarded. The application may decide to store these elsewhere or to simply use the aged out data to notify clients about the ends of processes.

**A continuation of this discussion, with information about how to use evicted data, can be found here:** [LRU Eviction](./doc/lru_eviction.md)

### <u>Growing by slabs</u>

In a future version of this library, an optional listing of secondary slabs may be provided depending upon configuration.

When the LRU becomes full, the module might move aging data into a second LRU, another managed memory and hash table pair. The older day may be searched when searches in the primary section fail.

The seconday slabs may be removed when they become empty and the preasure on the primay slabs goes away.

Access to these sorts of operation will be provded in methods (to be implementedd).

**A continuation of this discussion can be found here:** [Extending the LRU](./doc/extended_lru.md)


