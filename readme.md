IPC shared memory LRU cache for Node.js  

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

##### shm-type-lru tests and npm (coming soon)


# Purpose

This repository provides a simple LRU for fixed sized elements residing in shared memory. It make the LRU available to more tha one process. This module does not provide all of the communication that might take place between processes sharing an LRU. It makes the communication possible and manages the object that they share. Please see references to other modules for more features.

(The author chose not to clone the original repository since there many changes to C++, and changing the C++ was more expedient than trying to manage separate stacks. In the future, this problem, not necessarily apparent to all the upstream repositories will be resolved.)

>>The module, shared-typed-array was written to manage a collection of shared memory regions. Application s of shared-typed-lru may make use of that to enhance communication. Or, more than one LRU may be managed for different record sizes.
  
# Install

``` bash
$ npm install shm-typed-lru
$ npm test
```
Manual build:
``` bash
node-gyp configure
node-gyp build
node test/example.js
```
Windows is not supported.


# API

For the following API's please read the description at this repository:
See [example.js](https://github.com/ukrbublik/shm-typed-array/)

### shm.create (count, typeKey [, key])
### shm.get (key, typeKey)
### shm.detach (key)
### shm.detachAll ()
#### Types
### shm.getTotalSize()
### shm.LengthMax

Here are the API's added in this repository:

### initLRU(key,record_size,region\_size,i\_am\_initializer)

Given a key to a shared memory structure obtained by shm.create, this will create an LRU data structure in the shared memory region. The record size is new information not give to shm.create. The region size should be the same (see *count*). The basic communication use is for one process to be the master of the region. Call that process the initializer. Initialization creates data structurs. When *i\_am\_initializer* is set to false, the process using this module will read the existing data structures. 

Within the library, each process will have a red-black tree (C++ map) that maps hashes to the offsets (from the start of the shared block). Initialization sets up the map, which grows and shrinks as elements are added or removed. Prior to future work, these trees will need to be updated by processes that do not add new records, but that read the records. So, communication beyond this module will be required. (Future work: a shared hash map in fixed memory. Communication will still be required, but less.)

### getSegmentSize(key)

Given a key for a shared memory region, this returns the length of just that region.

### set(key,hash,value)

The application creates a hash key fitting UInt32. The key becomes the identifier for the stored value. The value should fit within the *record\_size* provided to initLRU.

Returns: UInt32 offset to the element record. (Care should be taken when using the index versions of get, set, del.

### get_el\_hash(key,hash)

Retuns the value previously stored in conjunction with the provided hash.

### del_key(key,hash)

Deletes the record (moves it to the free list), and removes the hash from  any hash map data structures governed by the module.

### get_el(key,index)

Retuns the value previously stored in conjunction with the index returned from *set*. (Note: if the element has been deleted, the value, prepended with the string "DELETED" will be accessible until it is overwritten. The element will not be accesible by the hash version of this method.)

### del_el(key,index)

Deletes the record (moves it to the free list), and removes the internally stored hash from  any hash map data structures governed by the module.

### get\_last_reason(key)

If an error occurs, as may be indicated by negative return values from methods, this method reports the reason published by the module. Accessing the reason will cause it to be reset to "OK", its initial state. 

### reload\_hash_map(key)

Synchronizes the internal hash map for the calling process with the existing allocated data.

### reload\_hash\_map\_update(key,share_key)

Does the same as *reload\_hash_map*, but only inspects elements with a share_key matching the one passed to this method.

### run\_lru\_eviction(key,cutoff\_time,max_evictions)

The application process sets the policy as to when to run the eviction method. This method deletes all elements that are prior to the time cutoff up to as many as max\_evictions. 

### set\_share\_key(key,index,share_key)

Access an element by index and sets its share_key for use in reloading hash maps. 

### debug\_dump\_list(key,backwards)

This method dumps a JSON format of all the elements currently allocated in the LRU.

# Cleanup
This library does cleanup of created SHM segments only on normal exit of process, see [`exit` event](https://nodejs.org/api/process.html#process_event_exit).  
If you want to do cleanup on terminate signals like `SIGINT`, `SIGTERM`, please use [node-cleanup](https://github.com/jtlapp/node-cleanup) / [node-death](https://github.com/jprichardson/node-death) and add code to exit handlers:
```js
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