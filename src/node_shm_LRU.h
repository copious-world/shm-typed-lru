#ifndef _H_HOPSCOTCH_HASH_LRU_
#define _H_HOPSCOTCH_HASH_LRU_

// node_shm_LRU.h

#include "errno.h"

#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/shm.h>

#include <iostream>
#include <sstream>

using namespace node;
using namespace v8;
using namespace std;

#include "hmap_interface.h"


#include <map>
#include <unordered_map>
#include <list>
#include <chrono>



using namespace std::chrono;


#define MAX_BUCKET_FLUSH 12
/*
auto ms_since_epoch(std::int64_t m){
  return std::chrono::system_clock::from_time_t(time_t{0})+std::chrono::milliseconds(m);
}

uint64_t timeSinceEpochMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()
).count();


int main()
{
    using namespace std::chrono;
 
    uint64_t ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    std::cout << ms << " milliseconds since the Epoch\n";
 
    uint64_t sec = duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
    std::cout << sec << " seconds since the Epoch\n";
 
    return 0;
}


milliseconds ms = duration_cast< milliseconds >(
    system_clock::now().time_since_epoch()
);

*/


inline uint64_t epoch_ms(void) {
	uint64_t ms;
	ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
	return ms;
}


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

template<typename K,typename V>
inline string map_maker_destruct(map<K,V> &jmap) {
	if ( jmap.size() == 0 ) {
		return "{}";
	}
	stringstream ss;
	char del = 0;
	ss << "{";
	for ( auto p : jmap ) {
		if ( del ) { ss << del; }
		del = ',';
		K h = p.first;
		V v = p.second;
		ss << "\""  << h << "\" : \""  << v << "\"";
		delete p.second;
	}
	ss << "}";
	string out = ss.str();
	return(out.substr(0,out.size()));
}


template<typename K,typename V>
inline void js_map_maker_destruct(map<K,V> &jmap,Local<Object> &jsObject) {
	if ( jmap.size() > 0 ) {
		for ( auto p : jmap ) {
			stringstream ss;
			ss << p.first;
			string key = ss.str();
			//
			Local<String> propName = Nan::New(key).ToLocalChecked();
			Local<String> propValue = Nan::New(p.second).ToLocalChecked();
			//
			Nan::Set(jsObject, propName, propValue);
			delete p.second;
		}
		jmap.clear();
	}
}


typedef struct LRU_ELEMENT_HDR {
	uint32_t	_prev;
	uint32_t	_next;
	uint64_t 	_hash;
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
			_hmap_i = nullptr;
			if ( (4*_step) >= region_size ) {
				_reason = "(constructor): regions is too small";
				_status = false;
			} else {
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
				next_free->_hash = UINT64_MAX;
				next_free->_when = 0;
				//
				curr += step;
				next += step;
			}

		}

		// set_hash_impl - called by initHopScotch
		//
		void set_hash_impl(HMap_interface *hmap) {
			_hmap_i = hmap;
		}

		// add_el
		// 		data -- data that will be stored at the end of the free list
		//		hash64 -- a large hash of the data. The hash will be processed for use in the hash table.
		//	The hash table will provide reverse lookup for the new element being added by allocating from the free list.
		//	The hash table stores the index of the element in the managed memory. 
		// 	So, to retrieve the element later, the hash will fetch the offset of the element in managed memory
		//	and then the element will be retrieved from its loction.
		//	The number of elements in the free list may be the same or much less than the number of hash table elements,
		//	the hash table can be kept sparse as a result, keeping its operation fairly efficient.
		//	-- The add_el method can tell that it has no more room for elements by looking at the free list.
		//	-- When the free list is empty, add_el returns UINT32_MAX. When the free list is empty,
		//	-- some applications may want to extend share memory by adding more hash slabs or by enlisting secondary processors, 
		//	-- or by running evictions on the tail of the list of allocated elements.
		//	Given there is a free slot for the element, add_el puts the elements offset into the hash table.
		//	add_el moves the element from the free list to the list of allocated positions. These are doubly linked lists.
		//	Each element of the list as a time of insertion, which add_el records.
		//
		 uint32_t add_el(char *data,uint64_t hash64) {
			//
			uint8_t *start = _region;
			size_t step = _step;

			LRU_element *ctrl_free = (LRU_element *)(start + 2*step);
			if (  ctrl_free->_next == UINT32_MAX ) {
				_status = false;
				_reason = "out of free memory: free count == 0";
				return(UINT32_MAX);
			}
			//
			uint32_t new_el_offset = ctrl_free->_next;
			// in rare cases the hash table may be frozen even if the LRU is not full
			//
			uint64_t store_stat = this->store_in_hash(hash64,new_el_offset);  // STORE
			//
			if ( store_stat == UINT64_MAX ) {
				return(UINT32_MAX);
			}

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
			new_el->_when = epoch_ms();
			new_el->_hash = hash64;
			char *store_el = (char *)(new_el + 1);
			//
			memset(store_el,0,this->_record_size);
			size_t cpsz = min(this->_record_size,strlen(data));
			memcpy(store_el,data,cpsz);
			//
			_count++;
			if ( _count_free > 0 ) _count_free--;
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

		// get_el_untouched
		uint8_t get_el_untouched(uint32_t offset,char *buffer) {
			if ( !this->check_offset(offset) ) return(2);
			//
			uint8_t *start = _region;
			LRU_element *stored = (LRU_element *)(start + offset);
			char *store_data = (char *)(stored + 1);
			memcpy(buffer,store_data,this->_record_size);
			return(0);
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

		//
		// del_el
		bool del_el(uint32_t offset) {
			if ( !this->check_offset(offset) ) return(false);
			uint8_t *start = _region;
			//
			LRU_element *stored = (LRU_element *)(start + offset);
			//
			uint32_t prev_off = stored->_prev;
			uint64_t hash = stored->_hash;
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
			stored->_hash = UINT64_MAX;
			stored->_prev = UINT32_MAX;
			//
			LRU_element *ctrl_free = (LRU_element *)(start + 2*(this->_step));
			stored->_next = ctrl_free->_next;
	//cout << "del_el: " << offset << " ctrl_free->_next: " << ctrl_free->_next << endl;
			ctrl_free->_next = offset;
			//
			this->remove_key(hash);
			_count_free++;
			//
			return(true);
		}


		// HASH TABLE USAGE
		void remove_key(uint64_t hash) {
			if ( _hmap_i == nullptr ) {   // no call to set_hash_impl
				_local_hash_table.erase(hash);
			} else {
				_hmap_i->del(hash);
			}
		}

		void clear_hash_table(void) {
			if ( _hmap_i == nullptr ) {   // no call to set_hash_impl
				_local_hash_table.clear();
			} else {
				_hmap_i->clear();
			}

		}

		uint64_t store_in_hash(uint64_t key64,uint32_t new_el_offset) {
			if ( _hmap_i == nullptr ) {   // no call to set_hash_impl
				_local_hash_table[key64] = new_el_offset;
			} else {
				uint64_t result = _hmap_i->store(key64,new_el_offset); //UINT64_MAX
				//count << "store_in_hash: " << result << endl;
				return result;
			}
			return 0;
		}


		// check_for_hash
		// either returns an offset to the data or return the UINT32_MAX.  (4,294,967,295)
		uint32_t check_for_hash(uint64_t key) {
			if ( _hmap_i == nullptr ) {   // no call to set_hash_impl
				if ( _local_hash_table.find(key) != _local_hash_table.end() ) {
					return(_local_hash_table[key]);
				}
			} else {
				uint32_t result = _hmap_i->get(key);
				if ( result != 0 ) return(result);
			}
			return(UINT32_MAX);
		}

		// evict_least_used
		void evict_least_used(time_t cutoff,uint8_t max_evict,list<uint64_t> &ev_list) {
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
				uint64_t hash = stored->_hash;
				ev_list.push_back(hash);
				test_time = stored->_when;
				this->del_el(prev_off);
			} while ( (test_time < cutoff) && (ev_count < max_evict) );
		}

		// ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ----
		void evict_least_used_to_value_map(time_t cutoff,uint8_t max_evict,map<uint64_t,char *> &ev_map) {
			uint8_t *start = _region;
			size_t step = _step;
			LRU_element *ctrl_tail = (LRU_element *)(start + step);
			time_t test_time = 0;
			uint8_t ev_count = 0;
			uint64_t last_hash = 0;
			do {
				uint32_t prev_off = ctrl_tail->_prev;
				if ( prev_off == 0 ) break; // no elements left... step?? instead of 0
				ev_count++;
				LRU_element *stored = (LRU_element *)(start + prev_off);
				uint64_t hash = stored->_hash;
				test_time = stored->_when;
				char *buffer = new char[this->record_size()];
				this->get_el_untouched(prev_off,buffer);
				this->del_el(prev_off);
				ev_map[hash] = buffer;
			} while ( (test_time < cutoff) && (ev_count < max_evict) );
		}

		uint8_t evict_least_used_near_hash(uint32_t hash,time_t cutoff,uint8_t max_evict,map<uint64_t,char *> &ev_map) {
			//
			if ( _hmap_i == nullptr ) {   // no call to set_hash_impl
				return 0;
			} else {
				uint32_t xs[32];
				uint8_t count = _hmap_i->get_bucket(hash, xs);
				//
				uint8_t *start = _region;
				size_t step = _step;
				LRU_element *ctrl_tail = (LRU_element *)(start + step);
				time_t test_time = 0;
				uint8_t ev_count = 0;
				uint64_t last_hash = 0;
				//
				uint8_t result = 0;
				if ( count > 0 ) {
					for ( uint8_t i = 0; i < count; i++ ) {
						uint32_t el_offset = xs[i];
						//
						LRU_element *stored = (LRU_element *)(start + el_offset);
						uint64_t hash = stored->_hash;
						test_time = stored->_when;
						if ( ((test_time < cutoff) || (ev_count < max_evict)) && (result < MAX_BUCKET_FLUSH) ) {
							char *buffer = new char[this->record_size()];
							this->get_el_untouched(el_offset,buffer);
							this->del_el(el_offset);
							ev_map[hash] = buffer;
							result++;
						}
					}
				}
				return result;
			}

		}


		// ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ---- ----
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
			this->clear_hash_table();
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

		uint16_t max_count(void) {
			return(_max_count);
		}

		uint16_t current_count(void) {
			return(_count);
		}

		uint16_t free_count(void) {
			return(_count_free);
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
			stored->_when = epoch_ms();
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
			if ( _hmap_i == nullptr ) {   // no call to set_hash_impl
				uint64_t hash = el->_hash;
				this->store_in_hash(hash,offset);
			}
		}

		void _add_map_filtered(LRU_element *el,uint32_t offset) {
			if ( _hmap_i == nullptr ) {   // no call to set_hash_impl
				if ( _share_key == el->_share_key ) {
					uint64_t hash = el->_hash;
					this->store_in_hash(hash,offset);
				}
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
		//
		HMap_interface 					*_hmap_i;
		//
		unordered_map<uint64_t,uint32_t>			_local_hash_table;
};




#endif // _H_HOPSCOTCH_HASH_LRU_