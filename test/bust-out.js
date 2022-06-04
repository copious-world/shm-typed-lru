const cluster = require('cluster');
const shm = require('../index.js');
const assert = require('assert');



function psleep(amount) {
	let p = new Promise((resolve,reject) => {
		setTimeout(() => {
			resolve(true)
		}, amount);
	})
	return p
}




function rest_of_test() {
	let arr2D2;

		//
	arr2D2 =  shm.create(1000000); //1M bytes
	console.log("arr2D2.key: " + arr2D2.key)
	let cache_count = shm.initLRU(arr2D2.key,250,100000)
	console.log("CACHE count: " + cache_count)
	console.log("Segment Size: " + shm.getSegmentSize(arr2D2.key))

	// // cache_count
	let hh_table_2D2 =  shm.create(cache_count*24*10); //1M bytes
	shm.initHopScotch(hh_table_2D2.key,arr2D2.key,true,cache_count*2)  // bigger than # els in LRU

	console.log(" BEGIN HASH TEST ")

	let el_diff = 0

	let hash = 134
	let el_data = "this is at test"
	let el_id = shm.set(arr2D2.key,el_data,hash,el_diff++)
	console.log("arr2D2 >> el_id: " + el_id)

	console.log(" AFTER SET HASH TEST \n")

	let value = shm.get_el(arr2D2.key,el_id)
	console.log("arr2D2 >> value: " + value)

	console.log(" AFTER GET INDEX TEST \n")

	let first_tst_value = shm.get_el_hash(arr2D2.key,hash,el_diff-1)
	console.log(`FIRST fetching after adding arr2D2 >> hash[${hash}]: value[${first_tst_value}]`)


	console.log(" AFTER GET HASH TEST \n")
	//process.exit(0);
	el_data = "we test different this time"
	el_id = shm.set(arr2D2.key,el_data,hash,el_diff++)
	console.log("arr2D2 >> el_id: " + el_id)

	value = shm.get_el(arr2D2.key,el_id)
	console.log("arr2D2 >> value: " + value)
	//
	//
	let hash2 = 136

	el_data = "we wolly gollies abren"
	el_id = shm.set(arr2D2.key,el_data,hash2,el_diff++)
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
	let i = 0;
	while ( i++ < cache_count ) {
		hash2++;
		el_diff++
		hashes.push(`${hash2}-${el_diff}`)
		el_data = `GEEE ${i} wolly gollies oglum`
		let status = shm.set(arr2D2.key,el_data,hash2,el_diff)
		console.log(shm.free_count(arr2D2.key))
		if ( status === false ) {
			console.log("breaking -- full")
			console.log(shm.get_last_reason(arr2D2.key))
			console.log(shm.current_count(arr2D2.key),shm.free_count(arr2D2.key))
			break;
		}
	}

	// dump the eviceted map
	let evicted_count = 0;
	if ( i < cache_count ) {
		console.log("i < cache_count")
	} else {
		let delta_time_cutoff = 5
		let evicted = shm.run_lru_eviction_get_values(arr2D2.key,delta_time_cutoff,20)
		console.log("evicting something: ")
		if ( typeof evicted === "object" ) {
			console.log("this got evicted: ")
			console.dir(evicted)
			evicted_count = Object.keys(evicted).length
		}
	}


	console.log(shm.current_count(arr2D2.key),shm.free_count(arr2D2.key))

	hashes.forEach(hsh => {
		let pair = hsh.split('-')
		let hash = parseInt(pair[0])
		let index = parseInt(pair[1])
		let tst_value = shm.get_el_hash(arr2D2.key,hash,index)   // some of these hashes should be gone
		console.log(`fetching after adding arr2D2 >> hash[${hash}]: value[${tst_value}]`)
	})

	i = 0;
	evicted_count--
	while ( i++ < evicted_count ) {
		hash2++;
		el_diff++
		hashes.push(`${hash2}-${el_diff}`)
		el_data = `GEEE ${i} wolly gollies oglum`
		let status = shm.set(arr2D2.key,el_data,hash2,el_diff)
		console.log(shm.free_count(arr2D2.key))
		if ( status === false ) {
			console.log("breaking -- full")
			console.log(shm.get_last_reason(arr2D2.key))
			console.log(shm.current_count(arr2D2.key),shm.free_count(arr2D2.key))
			break;
		}
	}

	hashes.forEach(hsh => {
		let pair = hsh.split('-')
		let hash = parseInt(pair[0])
		let index = parseInt(pair[1])
		let tst_value = shm.get_el_hash(arr2D2.key,hash,index)   // some of these hashes should be gone
		console.log(`fetching after adding arr2D2 >> hash[${hash}]: value[${tst_value}]`)
	})


}

if (cluster.isMaster) {
	//
	(async () => {
		for( let i = 0; i < 4; i++ ) {
			let et = shm.epoch_time()
			console.log(et)
			await psleep(10)
			console.log(Date.now())
			await psleep(90)
		}
		rest_of_test()
	})()

	//


} else {  // child process....
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
