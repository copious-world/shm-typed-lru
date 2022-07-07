'use strict';
const shm = require('./build/Release/shm-typed-lru.node');

const uint32Max = Math.pow(2,32) - 1;
const keyMin = 1;
const keyMax = uint32Max - keyMin;
const perm = Number.parseInt('660', 8);
const lengthMin = 1;
/**
 * Max length of shared memory segment (count of elements, not bytes)
 */
const lengthMax = shm.NODE_BUFFER_MAX_LENGTH;

const cleanup = () => {
	try {
		var cnt = shm.detachAll();
		if (cnt > 0)
			console.info('shm segments destroyed:', cnt);
	} catch(exc) { console.error(exc); }
};
process.on('exit', cleanup);
let sigint_proc_stop = false
process.on('SIGINT',() => {
	cleanup()
	if ( sigint_proc_stop === true ) process.exit(0)
	if ( typeof sigint_proc_stop === "function" ) sigint_proc_stop()
})

function set_sigint_proc_stop(func) {
	sigint_proc_stop = func
}

/**
 * Types of shared memory object
 */
const BufferType = {
	'Buffer': shm.SHMBT_BUFFER,
	'Int8Array': shm.SHMBT_INT8,
	'Uint8Array': shm.SHMBT_UINT8,
	'Uint8ClampedArray': shm.SHMBT_UINT8CLAMPED,
	'Int16Array': shm.SHMBT_INT16,
	'Uint16Array': shm.SHMBT_UINT16,
	'Int32Array': shm.SHMBT_INT32,
	'Uint32Array': shm.SHMBT_UINT32,
	'Float32Array': shm.SHMBT_FLOAT32, 
	'Float64Array': shm.SHMBT_FLOAT64,
};
const BufferTypeSizeof = {
	'Buffer': 1,
	'Int8Array': 1,
	'Uint8Array': 1,
	'Uint8ClampedArray': 1,
	'Int16Array': 2,
	'Uint16Array': 2,
	'Int32Array': 4,
	'Uint32Array': 4,
	'Float32Array': 4, 
	'Float64Array': 8,
};

/**
 * Create shared memory segment
 * @param {int} count - number of elements
 * @param {string} typeKey - see keys of BufferType
 * @param {int/null} key - integer key of shared memory segment, or null to autogenerate
 * @return {mixed/null} shared memory buffer/array object, or null on error
 *  Class depends on param typeKey: Buffer or descendant of TypedArray
 *  Return object has property 'key' - integer key of created shared memory segment
 */
function create(count, typeKey /*= 'Buffer'*/, key /*= null*/) {
	if (typeKey === undefined)
		typeKey = 'Buffer';
	if (key === undefined)
		key = null;
	if (BufferType[typeKey] === undefined)
		throw new Error("Unknown type key " + typeKey);
	if (key !== null) {
		if (!(Number.isSafeInteger(key) && key >= keyMin && key <= keyMax))
			throw new RangeError('Shm key should be ' + keyMin + ' .. ' + keyMax);
	}
	var type = BufferType[typeKey];
	//var size1 = BufferTypeSizeof[typeKey];
	//var size = size1 * count;
	if (!(Number.isSafeInteger(count) && count >= lengthMin && count <= lengthMax))
		throw new RangeError('Count should be ' + lengthMin + ' .. ' + lengthMax);
	let res;
	if (key) {
		res = shm.get(key, count, shm.IPC_CREAT|shm.IPC_EXCL|perm, 0, type);
	} else {
		do {
			key = _keyGen();
			res = shm.get(key, count, shm.IPC_CREAT|shm.IPC_EXCL|perm, 0, type);
		} while(!res);
	}
	if (res) {
		res.key = key;
	}
	return res;
}

/**
 * Get shared memory segment
 * @param {int} key - integer key of shared memory segment
 * @param {string} typeKey - see keys of BufferType
 * @return {mixed/null} shared memory buffer/array object, see create(), or null on error
 */
function get(key, typeKey /*= 'Buffer'*/) {
	if (typeKey === undefined)
		typeKey = 'Buffer';
	if (BufferType[typeKey] === undefined)
		throw new Error("Unknown type key " + typeKey);
	var type = BufferType[typeKey];
	if (!(Number.isSafeInteger(key) && key >= keyMin && key <= keyMax))
		throw new RangeError('Shm key should be ' + keyMin + ' .. ' + keyMax);
	let res = shm.get(key, 0, 0, 0, type);
	if (res) {
		res.key = key;
	}
	return res;
}

/**
 * Detach shared memory segment
 * If there are no other attaches for this segment, it will be destroyed
 * @param {int} key - integer key of shared memory segment
 * @param {bool} forceDestroy - true to destroy even there are other attaches
 * @return {int} count of left attaches or -1 on error
 */
function detach(key, forceDestroy /*= false*/) {
	if (forceDestroy === undefined)
		forceDestroy = false;
	return shm.detach(key, forceDestroy);
}

/**
 * Detach all created and getted shared memory segments
 * Will be automatically called on process exit/termination
 * @return {int} count of destroyed segments
 */
function detachAll() {
	return shm.detachAll();
}

function _keyGen() {
	return keyMin + Math.floor(Math.random() * keyMax);
}

function initLRU(key,record_size,region_size,i_am_initializer) {
	if ( i_am_initializer === undefined ) {
		i_am_initializer = true
	}
	return shm.initLRU(key,record_size,region_size,i_am_initializer)
}

function getSegmentSize(key) {
	return shm.getSegSize(key)
}

function lru_max_count(key) {
	return shm.max_count(key)
}

function current_count(key) {
	return shm.current_count(key)
}

function free_count(key) {
	return shm.free_count(key)
}

function epoch_time() {
	return shm.epoch_time()
}


function set(key,value,hh_hash,index) {
//console.log("Set: ",hh_hash,index)
	if ( index == undefined ) index = 0
	return shm.set_el(key,hh_hash,index,value)
}



function set_many(key,value_hash_array) {
	if ( !(Array.isArray(value_hash_array)) ) return false
	return shm.set_many(key,value_hash_array)
}
	

function get_el(key,index) {
	return shm.get_el(key,index)
}


function get_el_hash(key,hh_hash,index) {
	if ( index == undefined ) index = 0
	return shm.get_el_hash(key,hh_hash,index)
}


function del_el(key,index) {
	return shm.del_el(key,index)
}

function del_key(key,hh_hash,index) {
	if ( index == undefined ) index = 0
	return shm.del_key(key,hh_hash,index)
}

function remove_key(key,hh_hash,index) {
	if ( index == undefined ) index = 0
	return shm.remove_key(key,hh_hash,index)
}


function get_last_reason(key) {
	return shm.get_last_reason(key)
}

function reload_hash_map(key) {
	return shm.reload_hash_map(key)
}

function reload_hash_map_update(key,share_key) {
	return shm.reload_hash_map_update(key,share_key)
}

function run_lru_eviction(key,cutoff_time,max_evictions) {
	return shm.run_lru_eviction(key,cutoff_time,max_evictions)
}


function run_lru_eviction_get_values(key,cutoff_time,max_evictions) {
	return shm.run_lru_eviction_get_values(key,cutoff_time,max_evictions)
}


function run_lru_targeted_eviction_get_values(key,cutoff_time,max_evictions,hh_hash,index) {
	return shm.run_lru_targeted_eviction_get_values(key,cutoff_time,max_evictions,hh_hash,index)
}



function set_share_key(key,index,share_key) {
	return shm.set_share_key(key,index,share_key)
}


function debug_dump_list(key,backwards) {
	if ( backwards == undefined ) {
		backwards = false;
	}
	return shm.debug_dump_list(key,backwards)
}

// // // // // // // 
function initHopScotch(key,lru_key,am_initializer,max_element_count) {
	return shm.initHopScotch(key,lru_key,am_initializer,max_element_count)
}



// ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ----

function init_mutex(key,initializer) {
	return shm.init_mutex(key,initializer)
}

function try_lock(key) {
	return shm.try_lock(key)
}

function lock(key) {
	return shm.lock(key)
}

function unlock(key) {
	return shm.unlock(key)
}

function get_last_mutex_reason(key) {
	return shm.get_last_mutex_reason(key)
}


// ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ----

//Exports
module.exports.create = create;
module.exports.get = get;
module.exports.detach = detach;
module.exports.detachAll = detachAll;
module.exports.getTotalSize = shm.getTotalSize;
module.exports.BufferType = BufferType;
module.exports.LengthMax = lengthMax;
//

// The following is for the LRU implementation...
//
module.exports.initLRU = initLRU;
module.exports.getSegmentSize = getSegmentSize;
module.exports.lru_max_count = lru_max_count
module.exports.current_count = current_count
module.exports.free_count = free_count
module.exports.epoch_time = epoch_time
module.exports.set = set;
module.exports.set_many = set_many;
module.exports.get_el = get_el;
module.exports.del_el = del_el;
module.exports.del_key = del_key;
module.exports.remove_key = remove_key;
module.exports.get_el_hash = get_el_hash;
//
module.exports.get_last_reason = get_last_reason;
module.exports.reload_hash_map = reload_hash_map;
module.exports.reload_hash_map_update = reload_hash_map_update;
module.exports.run_lru_eviction = run_lru_eviction;
module.exports.run_lru_eviction_get_values = run_lru_eviction_get_values;
module.exports.run_lru_targeted_eviction_get_values = run_lru_targeted_eviction_get_values

module.exports.set_share_key = set_share_key;
//
module.exports.debug_dump_list = debug_dump_list;
//
module.exports.initHopScotch = initHopScotch
//
module.exports.init_mutex = init_mutex
module.exports.try_lock = try_lock
module.exports.lock = lock
module.exports.unlock = unlock
module.exports.get_last_mutex_reason = get_last_mutex_reason
module.exports.set_sigint_proc_stop = set_sigint_proc_stop

