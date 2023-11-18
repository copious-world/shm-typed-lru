#ifndef _H_HOPSCOTCH_HASH_SHM_
#define _H_HOPSCOTCH_HASH_SHM_

#include "node.h"
#include "node_buffer.h"
#include "v8.h"
#include "nan.h"
#include "errno.h"

#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/shm.h>

#include <iostream>
#include <sstream>

#include "hmap_interface.h"


using namespace node;
using namespace v8;
using namespace std;


#include <map>
#include <unordered_map>
#include <list>


// Bringing in code from libhhash  // until further changes...


#define WORD  (8*sizeof(uint32_t))		// 32 bits
#define MOD(x, n) ((x) < (n) ? (x) : (x) - (n))
//
template<typename T>
inline T CLZ(T x) {		// count leading zeros -- make sure it is not bigger than the type size
	static uint8_t W = sizeof(T)*8;
	return(__builtin_clzl(x) % W);
}

#define FFS(x) (__builtin_ctzl(x))				// count trailing zeros (First Free Space in neighborhood)
#define FLS(x) WORD // (WORD - CLZ(x))			// number bits possible less leading zeros (limits the space of the neigborhood)
#define GET(hh, i) ((hh) & (1L << (i)))			// ith bit returned   (hh for hash home)
#define SET(hh, i) (hh = (hh) | (1L << (i)))	// or in ith bit (ith bit set - rest 0)
#define UNSET(hh, i) (hh = (hh) & ~(1L << (i)))	// and with ith bit 0 - rest 1 (think of as mask)
//
const uint64_t HASH_MASK = (((uint64_t)0) | ~(uint32_t)(0));  // 32 bits
//
#define BitsPerByte 8
#define HALF (sizeof(uint32_t)*BitsPerByte)  // should be 32
//
typedef unsigned long ulong;

typedef struct HHASH {
  uint32_t _neighbor;		// Number of elements in a neighborhood
  uint32_t _count;			// count of elements contained an any time
  uint32_t _max_n;			// max elements that can be in a container
} HHash;



class HH_map : public HMap_interface {
	//
	public:

		// LRU_cache -- constructor
		HH_map(void *region,uint32_t max_element_count,bool am_initializer = false) {
			_reason = "OK";
			_region = (uint8_t *)region;
			_status = true;
			_initializer = am_initializer;
			_max_count = max_element_count;
			uint8_t sz = sizeof(HHash);
			uint8_t header_size = (sz  + (sz % sizeof(uint32_t)));
			// initialize from constructor
			this->setup_region(am_initializer,header_size,max_element_count);
		}

		// setup_region -- part of initialization if the process is the intiator..
		void setup_region(bool am_initializer,uint8_t header_size,uint32_t max_count) {
			//
			uint8_t *start = _region;
			HHash *T = (HHash *)start;
			//
			if ( am_initializer ) {
				T->_count = 0;
				T->_max_n = max_count;
				T->_neighbor = FLS(max_count - 1);
			} else {
				max_count = T->_max_n;	// just in case
			}
			//
			_region_V = (uint64_t *)(start + header_size);  // start on word boundary
			uint32_t v_regions_size = (sizeof(uint64_t)*max_count);
			//
			_region_H = (uint32_t *)(start + header_size + v_regions_size);
			uint32_t h_regions_size = (sizeof(uint32_t)*max_count);
			//
			if ( am_initializer ) {
				// storing at most 4GB hashes as 32 bit with 4GB values as 64 bit
				memset((void *)(start + header_size),0,(h_regions_size + v_regions_size));
			}
		}

		bool ok(void) {
			return(this->_status);
		}

		//  store
		uint64_t store(uint64_t loaded_hash, uint32_t v_value) {
			uint32_t element_diff = (uint32_t)((loaded_hash >> HALF) & HASH_MASK);  // JS usage -- this is the index
			uint32_t hash = (uint32_t)(loaded_hash & HASH_MASK);				// JS usage -- this is the base hash
			//
//cout << "store>> element_diff: " << element_diff << " hash: " << hash << endl;
			//
			HHash *T = (HHash *)_region;
			return put_hh_map(T,hash,element_diff,v_value);
		}

		// get
		uint32_t get(uint64_t key) {
//cout << "get>> key: " << key  << " ";
			HHash *T = (HHash *)_region;
//cout << "T->_count: " << T->_count << " ";
//cout << "T->_max_n: " << T->_max_n << " ";
//cout << "T->_neighbor: " << T->_neighbor << " ";

//uint32_t out = get_hh_map(T, key);
//cout << "get>> out: " << out << endl;
			return get_hh_map(T, key);
		}

		// bucket probing
		// 
		uint8_t get_bucket(uint32_t h, uint32_t xs[32]) {
			HHash *T = (HHash *)_region;
			uint8_t count = 0;
			uint32_t i = _succ_hh_hash(T, h, 0);
			while ( i != UINT32_MAX ) {
				uint64_t x = get_val_at_hh_hash(T, h, i);  // get ith value matching this hash (collision)
				xs[count++] = (uint32_t)((x >> HALF) & HASH_MASK);
				i = _succ_hh_hash(T, h, i + 1);  // increment i in some sense (skip unallocated holes)
			}
			return count;	// no value  (values will always be positive, perhaps a hash or'ed onto a 0 value)
		}


		// del
		uint32_t del(uint64_t key) {
			HHash *T = (HHash *)_region;
			return del_hh_map(T, key);
		}

		void clear(void) {
			if ( _initializer ) {
				uint8_t sz = sizeof(HHash);
				uint8_t header_size = (sz  + (sz % sizeof(uint32_t)));
				this->setup_region(_initializer,header_size,_max_count);
			}
		}

	private:
 
		uint32_t _succ_hh_hash(HHash *T, uint32_t h, uint32_t i) {
			if ( i == 32 ) return(UINT32_MAX);
			uint32_t N = T->_max_n;
			h = (h % N);
			return _succ(h, i);
		}


/*		// these are for testing purposes only
		uint32_t _next_T(uint32_t _H, uint32_t i) {
  			uint32_t H = _H & (~0 << i);
  			if ( H == 0 ) return UINT32_MAX;  // like -1
			uint32_t ffs = FFS(H);
cout << " ffs: " << ffs;
  			return ffs;	// return the count of trailing zeros
		}

		uint32_t _succ_T(uint32_t h, uint32_t i) {
			uint32_t *buffer = _region_H;
			uint32_t H = buffer[h];
cout << " _succ_T: GET(H, i)  " << GET(H, i);
  			if ( GET(H, i) ) return i;		// look at the control bits of the test position... see if the position is set.
			uint32_t b = _next_T(H, i);

cout << " _succ_T-> _next_T: b  " << b;

  			return b;			// otherwise, what's next...
		}


		uint32_t _succ_hh_hash_T(HHash *T, uint32_t h, uint32_t i) {
			if ( i == 32 ) return(UINT32_MAX);
			uint32_t N = T->_max_n;
			h = (h % N);
cout << "h: " << h << " of " << N << " i: " << i ;
			uint32_t jk = _succ_T(h, i);
cout << endl;
			return jk;
		}
*/

		void del_hh_hash(HHash *T, uint32_t h, uint32_t i) {
			uint32_t *buffer = _region_H;
			uint64_t *buffer_v = _region_V;

			uint32_t N = T->_max_n;
			h = (h % N);
			uint32_t j = MOD(h + i, N);  // the offset relative to the original hash bucket + bucket position = absolute address
			//
			uint32_t V = buffer_v[j];
			uint32_t H = buffer[h];		// the control bit in the original hash bucket
			//
			if ( (V == 0) || !GET(H, i)) return;
			//
			// reset the hash machine
			buffer_v[j] = 0;	// clear the value slot
			UNSET(H,i);			// remove relative position from the hash bucket
			buffer[h] = H;		// store it
			// lower the count
			T->_count--;
		}

		// put_hh_hash
		// Given a hash and a value, find a place for storing this pair
		// (If the buffer is nearly full, this can take considerable time)
		// Attempt to keep things organized in buckets, indexed by the hash module the number of elements
		//
		bool put_hh_hash(HHash *T, uint32_t h, uint64_t v) {
			uint32_t N = T->_max_n;
			if ( (T->_count == N) || (v == 0) ) return(false);  // FULL
			//
			h = h % N;  // scale the hash .. make sure it indexes the array...
			uint32_t d = _probe(T, h);  // a distance starting from h (if wrapped, then past N)
			if ( d == UINT32_MAX ) return(false); // the positions in the entire buffer are full.
	//cout << "put_hh_hash: d> " << d;
			//
			uint32_t K =  T->_neighbor;
			while ( d >= K ) {						// the number may be bigger than K. if wrapping, then bigger than N. 2N < UINT32_MAX.
				uint32_t hd = MOD( (h + d), N );	// d is allowed to wrap around.
				uint32_t z = _hop_scotch(T, hd);	// hop scotch back to a moveable positions
	//cout << " put_hh_hash: z> " << z;
				if ( z == 0 ) return(false);			// could not find anything that could move. (Frozen at this point..)
				// found a position that can be moved... (offset from h <= d closer to the neighborhood)
				uint32_t j = z;
				z = MOD((N + hd - z), N);		// hd - z is an (offset from h) < h + d or (h + z) < (h + d)  ... see hopscotch 
				uint32_t i = _succ(z, 0);		// either this is moveable or there's another one. (checking the bitmap ...)
				_swap(T, z, i, j);				// swap bits and values between i and j offsets within the bucket h
				d = MOD( (N + z + i - h), N );  // N + z - (h - i) ... a new distance, should be less than before
			}
			//
			uint32_t *buffer = _region_H;
			uint64_t *buffer_v = _region_V;
			//
			uint32_t hd = MOD( (h + d), N );  // store the value
			buffer_v[hd] = v;
	//cout << " put_hh_hash: hd> " << hd  << " val: "  << v;

			//
			uint32_t H = buffer[h]; // update the hash machine
			SET(H,d);
			buffer[h] = H;
	//cout << " put_hh_hash: h> " << h  << " H: "  << H << endl;

			// up the count 
			T->_count++;
			return(true);
		}

		uint32_t _next(uint32_t _H, uint32_t i) {
  			uint32_t H = _H & (~0 << i);
  			if ( H == 0 ) return UINT32_MAX;  // like -1
  			return FFS(H);	// return the count of trailing zeros
		}

		uint32_t _succ(uint32_t h, uint32_t i) {
			uint32_t *buffer = _region_H;		// the binary pattern
			uint32_t H = buffer[h];				// the one for the bucket
   			if ( GET(H, i) ) return i;			// look at the control bits of the test position... see if the position is set.
  			return _next(H, i);					// otherwise, what's next...
		}

		/**
		 * Swap bits and values 
		 * The bitmap is for the h'th bucket. And, i and j are bits within bitmap i'th and j'th.
		 * 
		 * i and j are used later as offsets from h when doing the value swap. 
		*/
		void _swap(HHash *T, uint32_t h, uint32_t i, uint32_t j) {
			uint32_t *buffer = _region_H;
			uint64_t *v_buffer = _region_V;
			//
			uint32_t H = buffer[h];
			UNSET(H, i);
			SET(H, j);
			buffer[h] = H;
			//
			uint32_t N = T->_max_n;
			i = MOD((h + i), N);		// offsets from the moveable position (i will often be 0)
			j = MOD((h + j), N);
			//
			uint64_t v = v_buffer[i];	// swap
			v_buffer[i] = 0;
			v_buffer[j] = v;
		}

		/**
		 *  _probe -- search for a free space within a bucket
		 * 		h : the bucket starts at h (an offset in _region_V)
		 * 
		 *  zero in the value buffer means no entry, because values do not start at zero for the offsets 
		 *  (free list header is at zero if not allocated list)
		 * 
		 * `_probe` wraps around search before h (bucket index) returns the larger value N + j if the wrap returns a position
		 * 
		 * @returns {uint32_t} distance of the bucket from h
		*/
		uint32_t _probe(HHash *T, uint32_t h) {   // value probe ... looking for zero
			uint64_t *v_buffer = _region_V;
			// // 
			uint32_t N = T->_max_n;		// upper bound (count of elements in buffer)
			//
			// search in the bucket
			v_buffer += h;
			for ( uint32_t i = h; i < N; ++i ) {			// search forward to the end of the array (all the way even it its millions.)
				uint64_t V = *v_buffer++;	// is this an empty slot? Usually, when the table is not very full.
				if ( V == 0 ) return (i-h);			// look no further
			}
			//
			// look for anything starting at the beginning of the segment
			// wrap... start searching from the start of all data...
			v_buffer = _region_V;
			for ( uint32_t j = 0; j < h ; ++j ) {
				uint64_t V = *v_buffer++;	// is this an empty slot? Usually, when the table is not very full.
				if ( V == 0 ) return (N + j - h);	// look no further (notice quasi modular addition)
			}
			return UINT32_MAX;  // this will be taken care of by a modulus in the caller
		}

		/**
		 * Look at one bit pattern after another from distance `d` shifted 'down' to h by K (as close as possible).
		 * Loosen the restriction on the distance of the new buffer until K (the max) away from h is reached.
		 * If something within K (for swapping) can be found return it, otherwise 0 (indicates frozen)
		*/
		uint32_t _hop_scotch(HHash *T, uint32_t hd) {  // return an index
			uint32_t *buffer = _region_H;
			uint32_t N = T->_max_n;
			uint32_t K =  T->_neighbor;
			for ( uint32_t i = (K - 1); i > 0; --i ) {
				uint32_t hi = MOD(N + hd - i, N);			// hop backwards towards the original hash position (h)...
				uint32_t H = buffer[hi];
				if ( (H != 0) && (((uint32_t)FFS(H)) < i) ) return i;	// count of trailing zeros less than offset from h
			}
			return 0;
		}

		uint64_t get_val_at_hh_hash(HHash *T, uint32_t h, uint32_t i) {
			uint32_t offset = (h + i);		// offset from the hash position...
			uint32_t N = T->_max_n;
			uint32_t j = (offset % N);		// if wrapping around
			return(_region_V[j]);			// return value
		}


		// SET OPERATION
		// originailly called hunt for a set type...
		uint64_t hunt_hash_set(HHash *T, uint32_t h, uint64_t k, bool kill) {
			uint32_t i = _succ_hh_hash(T, h, 0);
			while ( i != UINT32_MAX ) {
				uint64_t x = get_val_at_hh_hash(T, h, i);  // get ith value matching this hash (collision)
				if ( _cmp(k, x) ) {		// compare the discerning hash part of the values (in the case of map, hash of the stored value)
					if (kill) del_hh_hash(T, h, i);
					return x;
				}
				i = _succ_hh_hash(T, h, i + 1);  // increment i in some sense (skip unallocated holes)
			}
			return 0;		// no value  (values will always be positive, perhaps a hash or'ed onto a 0 value)
		}


		// ---- ---- ---- ---- ---- ---- ----
		bool _cmp(uint64_t k, uint64_t x) {		// compares the bottom part of the workds
			bool eq = ((HASH_MASK & k) == (HASH_MASK & x));
			return(eq); //
		}

		bool put_hh_set(HHash *T, uint32_t h, uint64_t key_val) {
			if ( key_val == 0 ) return 0;		// cannot store zero values
			if ( get_hh_set(T, h, (uint32_t)key_val) != 0 ) return (true);  // found, do not duplicate ... _cmp has been called
			if ( put_hh_hash(T, h, key_val)) return (true); // success
			// not implementing resize
			return (false);
		}


		uint64_t get_hh_set(HHash *T, uint32_t hash, uint32_t key) {
			uint64_t zero = 0;
			uint64_t key_null = (zero | (uint64_t)key); // hopefully this explains it... 
//cout << "get_hh_set: key_null: " << key_null << " hash: " << hash <<  endl;
			bool flag_delete = false;
			return hunt_hash_set(T, hash, key_null, flag_delete);
		}

		uint64_t del_hh_set(HHash *T, uint32_t hash, uint32_t key) { 
			uint64_t zero = 0;
			uint64_t key_null = (zero | (uint64_t)key); // hopefully this explains it... 
			bool flag_delete = true;
			return hunt_hash_set(T, hash, key_null, flag_delete); 
		}

		// note: not implementing resize since the size of the share segment is controlled by the application..

		// MAP OPERATION

		// loaded value -- value is on top (high word) and the index (top of loaded hash) is on the

		uint64_t put_hh_map(HHash *T, uint32_t hash_of_loaded, uint32_t index, uint32_t value) {
			if ( value == 0 ) return false;
//cout <<  " put_hh_map: loaded_value [value] " << value << " loaded_value [index] " << index;
			uint64_t loaded_value = (((uint64_t)value) << HALF) | index;
//cout << " loaded_value: " << loaded_value << endl;
			bool put_ok = put_hh_set(T, hash_of_loaded, loaded_value);
			if ( put_ok ) {
				uint64_t loaded_key = (((uint64_t)index) << HALF) | hash_of_loaded; // LOADED
				return(loaded_key);
			} else {
				return(UINT64_MAX);
			}
		}

		uint32_t get_hh_map(HHash *T, uint64_t key) { 
			 // UNLOADED
			uint32_t element_diff = (uint32_t)((key >> HALF) & HASH_MASK);  // just unloads it (was index)
			uint32_t hash = (uint32_t)(key & HASH_MASK);
//cout << "get_hh_map>> element_diff: " << element_diff << " hash: " << hash << " ";
//cout << " _region_H[hash] " << _region_H[hash] << " _region_V[hash]  "  << _region_V[hash]  << endl;

			return (uint32_t)(get_hh_set(T, hash, element_diff) >> HALF); 
		}

		uint32_t del_hh_map(HHash *T, uint64_t key) {
			 // UNLOADED
			uint32_t element_diff = (uint32_t)((key >> HALF) & HASH_MASK);
			uint32_t hash = (uint32_t)(key & HASH_MASK);
			return (uint32_t)(del_hh_set(T, hash, element_diff) >> HALF);
		}

		// ---- ---- ---- ---- ---- ---- ----
		//
		bool							_status;
		bool							_initializer;
		uint32_t						_max_count;
		const char 						*_reason;
		uint8_t		 					*_region;
		uint32_t		 				*_region_H;
		uint64_t		 				*_region_V;

};


namespace node {
namespace node_shm {

	/**
	 * Setup a segment as a container of a hop scotch hash table
	 * Params:
	 *  key_t key
	 */
	NAN_METHOD(get_HH);

}
}


#endif // _H_HOPSCOTCH_HASH_SHM_