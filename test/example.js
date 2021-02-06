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

/**
 * Output
 *

[Master] Typeof buf: Buffer Typeof arr: Float32Array
[Worker] Typeof buf: Buffer Typeof arr: Float32Array
0 [Master] Set buf[0]= 2  arr[0]= 5
0 [Worker] Get buf[0]= 2  arr[0]= 5
1 [Master] Set buf[0]= 3  arr[0]= 2.5
1 [Worker] Get buf[0]= 3  arr[0]= 2.5
2 [Master] Set buf[0]= 4  arr[0]= 1.25
2 [Worker] Get buf[0]= 4  arr[0]= null
3 [Master] Set buf[0]= 5  arr[0]= 0.625
3 [Worker] Get buf[0]= 5  arr[0]= null
4 [Master] Set buf[0]= 6  arr[0]= 0.3125
shm segments destroyed: 3


*/
