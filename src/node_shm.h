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

using namespace node;
using namespace v8;
using namespace std;


#include <map>
#include <list>


template<typename T>
inline string joiner(list<T> &jlist) {
	if ( jlist.size() == 0 ) {
		return("");
	}
	stringstream ss;
	for ( auto v : jlist ) {
		ss << v;
		ss << ',';
	}
	string out = ss.str();
	return(out.substr(0,out.size()-1));
}

typedef struct LRU_ELEMENT_HDR {
	uint32_t	_prev;
	uint32_t	_next;
	uint32_t 	_hash;
	time_t		_when;
	uint32_t	_share_key;
} LRU_element;

class LRU_cache {
	//
	public:
		// LRU_cache -- constructor
		LRU_cache(void *region,size_t record_size,size_t region_size,bool am_initializer) {
			_reason = "OK";
			_region = (uint8_t *)region;
			_record_size = record_size;
			_status = true;
			_step = (sizeof(LRU_element) + record_size);
			if ( 4*_step >= region_size ) {
				_reason = "(constructor): regions is too small";
				_status = false;
			} else {
				//
				_region_size = region_size;
				_max_count = (region_size/_step) - 3;
				_count_free = 0;
				_count = 0; 
				//
				if ( am_initializer ) {
					setup_region(record_size);
				} else {
					_count_free = this->_walk_free_list();
					_count = this->_walk_allocated_list(1);
				}
			}
		}

		// setup_region -- part of initialization if the process is the intiator..
		void setup_region(size_t record_size) {
			
			uint8_t *start = _region;
			size_t step = _step;

			LRU_element *ctrl_hdr = (LRU_element *)start;
			ctrl_hdr->_prev = UINT32_MAX;
			ctrl_hdr->_next = step;
			ctrl_hdr->_hash = 0;
			ctrl_hdr->_when = 0;
			
			LRU_element *ctrl_tail = (LRU_element *)(start + step);
			ctrl_tail->_prev = 0;
			ctrl_tail->_next = UINT32_MAX;
			ctrl_tail->_hash = 0;
			ctrl_tail->_when = 0;

			LRU_element *ctrl_free = (LRU_element *)(start + 2*step);
			ctrl_free->_prev = UINT32_MAX;
			ctrl_free->_next = 3*step;
			ctrl_free->_hash = UINT32_MAX;
			ctrl_free->_when = 0;

			size_t region_size = this->_region_size;
			//
			size_t curr = ctrl_free->_next;
			size_t next = 4*step;
			
			while ( curr < region_size ) {
				_count_free++;
				LRU_element *next_free = (LRU_element *)(start + curr);
				next_free->_prev = UINT32_MAX;  // singly linked free list
				next_free->_next = next;
				if ( next >= region_size ) {
					next_free->_next = UINT32_MAX;
				}
				next_free->_hash = UINT32_MAX;
				next_free->_when = 0;
				//
				curr += step;
				next += step;
			}

		}

		// add_el
		 uint32_t add_el(char *data,uint32_t key) {
			//
			uint8_t *start = _region;
			size_t step = _step;

			LRU_element *ctrl_free = (LRU_element *)(start + 2*step);
			if (  ctrl_free->_next == UINT32_MAX ) {
				_status = false;
				_reason = "out of free memory";
				return(UINT32_MAX);
			}
			//
			uint32_t new_el_offset = ctrl_free->_next;
			LRU_element *new_el = (LRU_element *)(start + new_el_offset);
			ctrl_free->_next = new_el->_next;
        	//
			LRU_element *header = (LRU_element *)(start);
			LRU_element *first = (LRU_element *)(start + header->_next);
			new_el->_next = header->_next;
			first->_prev = new_el_offset;
			new_el->_prev = 0; // offset to header
			header->_next = new_el_offset;
			//
			new_el->_when = time(0);
			new_el->_hash = key;
			char *store_el = (char *)(new_el + 1);

			memset(store_el,0,this->_record_size);
			size_t cpsz = min(this->_record_size,strlen(data));
			memcpy(store_el,data,cpsz);

			//
			_local_hash_table[key] = new_el_offset;

			_count++;
			_count_free--;
			return(new_el_offset);
	    }

		// get_el
		uint8_t get_el(uint32_t offset,char *buffer) {
			if ( !this->check_offset(offset) ) return(2);
			//
			uint8_t *start = _region;
			LRU_element *stored = (LRU_element *)(start + offset);
			char *store_data = (char *)(stored + 1);
			memcpy(buffer,store_data,this->_record_size);
			if ( this->touch(stored,offset) ) return(0);
			return(1);
		}

		// update_el
		bool update_el(uint32_t offset,char *buffer) {
			if ( !this->check_offset(offset) ) return(false);
			//
			uint8_t *start = _region;
			LRU_element *stored = (LRU_element *)(start + offset);
			if ( this->touch(stored,offset) ) {
				char *store_data = (char *)(stored + 1);
				memcpy(store_data,buffer,this->_record_size);
				return(true);
			}
			_reason = "deleted";
			return(false);
		}

		// check_for_hash
		uint32_t check_for_hash(uint32_t key) {
			if ( _local_hash_table.find(key) != _local_hash_table.end() ) {
				return(_local_hash_table[key]);
			}
			return(UINT32_MAX);
		}
		//

		// del_el
		bool del_el(uint32_t offset) {
			if ( !this->check_offset(offset) ) return(false);
			uint8_t *start = _region;
			//
			LRU_element *stored = (LRU_element *)(start + offset);
			//
			uint32_t prev_off = stored->_prev;
			uint32_t hash = stored->_hash;
	//cout << "del_el: " << offset << " hash " << hash << " prev_off: " << prev_off << endl;
			//
			if ( (prev_off == UINT32_MAX) && (hash == UINT32_MAX) ) {
				_reason = "already deleted";
				return(false);
			}
			uint32_t next_off = stored->_next;
	//cout << "del_el: " << offset << " next_off: " << next_off << endl;

			LRU_element *prev = (LRU_element *)(start + prev_off);
			LRU_element *next = (LRU_element *)(start + next_off);
			//
	//cout << "del_el: " << offset << " prev->_next: " << prev->_next  << " next->_prev " << next->_prev  << endl;
			prev->_next = next_off;
			next->_prev = prev_off;
			//
			stored->_hash = UINT32_MAX;
			stored->_prev = UINT32_MAX;
			//
			LRU_element *ctrl_free = (LRU_element *)(start + 2*(this->_step));
			stored->_next = ctrl_free->_next;
	//cout << "del_el: " << offset << " ctrl_free->_next: " << ctrl_free->_next << endl;
			ctrl_free->_next = offset;
			//
			_local_hash_table.erase(hash);
			//
			return(true);
		}


		// evict_least_used
		void evict_least_used(time_t cutoff,uint8_t max_evict,list<uint32_t> &ev_list) {
			uint8_t *start = _region;
			size_t step = _step;
			LRU_element *ctrl_tail = (LRU_element *)(start + step);
			time_t test_time = 0;
			uint8_t ev_count = 0;
			do {
				uint32_t prev_off = ctrl_tail->_prev;
				if ( prev_off == 0 ) break; // no elements left... step?? instead of 0
				ev_count++;
				LRU_element *stored = (LRU_element *)(start + prev_off);
				uint32_t hash = stored->_hash;
				ev_list.push_back(hash);
				test_time = stored->_when;
				this->del_el(prev_off);
			} while ( (test_time < cutoff) && (ev_count < max_evict) );
		}

		size_t _walk_allocated_list(uint8_t call_mapper,bool backwards = false) {
			if ( backwards ) {
				return(_walk_allocated_list_backwards(call_mapper));
			} else {
				return(_walk_allocated_list_forwards(call_mapper));
			}
		}


		size_t _walk_allocated_list_forwards(uint8_t call_mapper) {
			uint8_t *start = _region;
			LRU_element *header = (LRU_element *)(start);
			size_t count = 0;
			uint32_t next_off = header->_next;
			LRU_element *next = (LRU_element *)(start + next_off);
			if ( call_mapper > 2 )  _pre_dump();
			while ( next->_next != UINT32_MAX ) {   // this should be the tail
				count++;
				if ( count >= _max_count ) break;
				if ( call_mapper > 0 ) {
					if ( call_mapper == 1 ) {
						this->_add_map(next,next_off);
					} else if ( call_mapper == 2 ) {
						this->_add_map_filtered(next,next_off);
					} else {
						_console_dump(next);
					}
				}
				next_off =  next->_next;
				next = (LRU_element *)(start + next_off);
			}
			if ( call_mapper > 2 )  _post_dump();
			return(count - this->_count);
		}

		size_t _walk_allocated_list_backwards(uint8_t call_mapper) {
			uint8_t *start = _region;
			uint32_t step = _step;
			LRU_element *tail = (LRU_element *)(start + step);  // tail is one elemet after head
			size_t count = 0;
			uint32_t prev_off = tail->_prev;
			LRU_element *prev = (LRU_element *)(start + prev_off);
			if ( call_mapper > 2 )  _pre_dump();
			while ( prev->_prev != UINT32_MAX ) {   // this should be the tail
				count++;
				if ( count >= _max_count ) break;
				if ( call_mapper > 0 ) {
					if ( call_mapper == 1 ) {
						this->_add_map(prev,prev_off);
					} else if ( call_mapper == 2 ) {
						this->_add_map_filtered(prev,prev_off);
					} else {
						_console_dump(prev);
					}
				}
				prev_off =  prev->_prev;
				prev = (LRU_element *)(start + prev_off);
			}
			if ( call_mapper > 2 )  _post_dump();
			return(count - this->_count);
		}

		size_t _walk_free_list(void) {
			uint8_t *start = _region;
			LRU_element *ctrl_free = (LRU_element *)(start + 2*(this->_step));
			size_t count = 0;
			LRU_element *next_free = (LRU_element *)(start + ctrl_free->_next);
			while ( next_free->_next != UINT32_MAX ) {
				count++;
				if ( count >= _max_count ) break;
				next_free = (LRU_element *)(start + next_free->_next);
			}
			return(count - this->_count_free);
		}
		
		bool ok(void) {
			return(this->_status);
		}

		size_t record_size(void) {
			return(_record_size);
		}

		const char *get_last_reason(void) {
			const char *res = _reason;
			_reason = "OK";
			return(res);
		}

		void reload_hash_map(void) {
			_local_hash_table.clear();
			_count = this->_walk_allocated_list(1);
		}

		void reload_hash_map_update(uint32_t share_key) {
			_share_key = share_key;
			_count = this->_walk_allocated_list(2);
		}

		bool set_share_key(uint32_t offset,uint32_t share_key) {
			if ( !this->check_offset(offset) ) return(false);
			uint8_t *start = _region;
			//
			LRU_element *stored = (LRU_element *)(start + offset);
			_share_key = share_key;
			stored->_share_key = share_key;
			return(true);
		}

	private:

		bool touch(LRU_element *stored,uint32_t offset) {
			uint8_t *start = _region;
			//
			uint32_t prev_off = stored->_prev;
			if ( prev_off == UINT32_MAX ) return(false);
			uint32_t next_off = stored->_next;

			//
			LRU_element *prev = (LRU_element *)(start + prev_off);
			LRU_element *next = (LRU_element *)(start + next_off);
			//
			// out of list
			prev->_next = next_off; // relink
			next->_prev = prev_off;
			//
			LRU_element *header = (LRU_element *)(start);
			LRU_element *first = (LRU_element *)(start + header->_next);
			stored->_next = header->_next;
			first->_prev = offset;
			header->_next = offset;
			stored->_prev = 0;
			stored->_when = time(0);
			//
			return(true);
		}

		bool check_offset(uint32_t offset) {
			_reason = "OK";
			if ( offset > this->_region_size ) {
				//cout << "check_offset: " << offset << " rsize: " << this->_region_size << endl;
				_reason = "OUT OF BOUNDS";
				return(false);
			} else {
				uint32_t step = _step;
				if ( (offset % step) != 0 ) {
					_reason = "ELEMENT BOUNDAY VIOLATION";
					return(false);
				}
			}
			return(true);
		}

		void _add_map(LRU_element *el,uint32_t offset) {
			uint32_t hash = el->_hash;
			_local_hash_table[hash] = offset;
		}

		void _add_map_filtered(LRU_element *el,uint32_t offset) {
			if ( _share_key == el->_share_key ) {
				uint32_t hash = el->_hash;
				_local_hash_table[hash] = offset;
			}
		}

		void _console_dump(LRU_element *el) {
			uint8_t *start = _region;
			uint64_t offset = (uint64_t)(el) - (uint64_t)(start);
			cout << "{" << endl;
			cout << "\t\"offset\": " << offset <<  ", \"hash\": " << el->_hash << ',' << endl;
			cout << "\t\"next\": " << el->_next << ", \"prev:\" " << el->_prev << ',' <<endl;
			cout << "\t\"when\": " << el->_when << ',' << endl;

			char *store_data = (char *)(el + 1);
			char buffer[this->_record_size];
			memset(buffer,0,this->_record_size);
			memcpy(buffer,store_data,this->_record_size-1);
			cout << "\t\"value: \"" << '\"' << buffer << '\"' << endl;
			cout << "}," << endl;
		}

		void _pre_dump(void) {
			cout << "[";
		}
		void _post_dump(void) {
			cout << "{\"offset\": -1 }]" << endl;
		}

		bool							_status;
		const char 						*_reason;
		uint8_t		 					*_region;
		size_t		 					_record_size;
		size_t							_region_size;
		uint32_t						_step;
		uint16_t						_count_free;
		uint16_t						_count;
		uint16_t						_max_count;  // max possible number of records
		uint32_t						_share_key;


		map<uint32_t,uint32_t>			_local_hash_table;
};

/*
namespace imp {
	static const size_t kMaxLength = 0x3fffffff;
}

namespace node {
namespace Buffer {
	// 2^31 for 64bit, 2^30 for 32bit
	static const unsigned int kMaxLength = 
		sizeof(int32_t) == sizeof(intptr_t) ? 0x3fffffff : 0x7fffffff;
}
}
*/

#define SAFE_DELETE(a) if( (a) != NULL ) delete (a); (a) = NULL;
#define SAFE_DELETE_ARR(a) if( (a) != NULL ) delete [] (a); (a) = NULL;


enum ShmBufferType {
	SHMBT_BUFFER = 0, //for using Buffer instead of TypedArray
	SHMBT_INT8,
	SHMBT_UINT8,
	SHMBT_UINT8CLAMPED,
	SHMBT_INT16,
	SHMBT_UINT16,
	SHMBT_INT32,
	SHMBT_UINT32,
	SHMBT_FLOAT32,
	SHMBT_FLOAT64
};

inline int getSize1ForShmBufferType(ShmBufferType type) {
	size_t size1 = 0;
	switch(type) {
		case SHMBT_BUFFER:
		case SHMBT_INT8:
		case SHMBT_UINT8:
		case SHMBT_UINT8CLAMPED:
			size1 = 1;
		break;
		case SHMBT_INT16:
		case SHMBT_UINT16:
			size1 = 2;
		break;
		case SHMBT_INT32:
		case SHMBT_UINT32:
		case SHMBT_FLOAT32:
			size1 = 4;
		break;
		default:
		case SHMBT_FLOAT64:
			size1 = 8;
		break;
	}
	return size1;
}


namespace node {
namespace Buffer {

	MaybeLocal<Object> NewTyped(
		Isolate* isolate, 
		char* data, 
		size_t length
	#if NODE_MODULE_VERSION > IOJS_2_0_MODULE_VERSION
	    , node::Buffer::FreeCallback callback
	#else
	    , node::smalloc::FreeCallback callback
	#endif
	    , void *hint
		, ShmBufferType type = SHMBT_FLOAT64
	);

}
}


namespace Nan {

	inline MaybeLocal<Object> NewTypedBuffer(
	      char *data
	    , size_t length
#if NODE_MODULE_VERSION > IOJS_2_0_MODULE_VERSION
	    , node::Buffer::FreeCallback callback
#else
	    , node::smalloc::FreeCallback callback
#endif
	    , void *hint
		, ShmBufferType type = SHMBT_FLOAT64
	);

}


namespace node {
namespace node_shm {

	/**
	 * Create or get shared memory
	 * Params:
	 *  key_t key
	 *  size_t count - count of elements, not bytes
	 *  int shmflg - flags for shmget()
	 *  int at_shmflg - flags for shmat()
	 *  enum ShmBufferType type
	 * Returns buffer or typed array, depends on input param type
	 */
	NAN_METHOD(get);

	/**
	 * Destroy shared memory segment
	 * Params:
	 *  key_t key
	 *  bool force - true to destroy even there are other processed uses this segment
	 * Returns count of left attaches or -1 on error
	 */
	NAN_METHOD(detach);

	/**
	 * Detach all created and getted shared memory segments
	 * Returns count of destroyed segments
	 */
	NAN_METHOD(detachAll);

	/**
	 * Get total size of all shared segments in bytes
	 */
	NAN_METHOD(getTotalSize);

	/**
	 * Constants to be exported:
	 * IPC_PRIVATE
	 * IPC_CREAT
	 * IPC_EXCL
	 * SHM_RDONLY
	 * NODE_BUFFER_MAX_LENGTH (count of elements, not bytes)
	 * enum ShmBufferType: 
	 *  SHMBT_BUFFER, SHMBT_INT8, SHMBT_UINT8, SHMBT_UINT8CLAMPED, 
	 *  SHMBT_INT16, SHMBT_UINT16, SHMBT_INT32, SHMBT_UINT32, 
	 *  SHMBT_FLOAT32, SHMBT_FLOAT64
	 */

	/**
	 * Setup LRU data structure on top of the shared memory
	 */
	NAN_METHOD(initLRU);

	/**
	 * get LRU segment size
	 */
	NAN_METHOD(getSegSize);

	/**
	 * add hash key and value
	 */
	NAN_METHOD(set_el);

	/**
	 * get element at index
	 */
	NAN_METHOD(get_el);

	/**
	 * get element at index
	 */
	NAN_METHOD(get_el_hash);

	/**
	 * delete element at index
	 */
	NAN_METHOD(del_el);

	/**
	 * delete element having a key...
	 */
	NAN_METHOD(del_key);

	/**
	 * get_last_reason and reset to OK...
	 */
	
	NAN_METHOD(get_last_reason);

	/**
	 *  reload_hash_map  -- clear and rebuild...
	 */
	NAN_METHOD(reload_hash_map);
	NAN_METHOD(reload_hash_map_update);
	NAN_METHOD(set_share_key);

	/**
	 *  run_lru_eviction  -- clear and rebuild...
	 */
	NAN_METHOD(run_lru_eviction);


	NAN_METHOD(debug_dump_list);


}
}
