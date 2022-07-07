#include "node_shm.h"

//-------------------------------
using namespace Nan;

namespace node {
namespace Buffer {

	using v8::ArrayBuffer;
	using v8::SharedArrayBuffer;
	using v8::ArrayBufferCreationMode;
	using v8::EscapableHandleScope;
	using node::AddEnvironmentCleanupHook;
	using v8::Isolate;
	using v8::Local;
	using v8::MaybeLocal;
	//
	using v8::Object;
	using v8::Array;
	using v8::Integer;
	using v8::Maybe;
	using v8::String;
	using v8::Value;
	using v8::Int8Array;
	using v8::Uint8Array;
	using v8::Uint8ClampedArray;
	using v8::Int16Array;
	using v8::Uint16Array;
	using v8::Int32Array;
	using v8::Uint32Array;
	using v8::Float32Array;
	using v8::Float64Array;
	


	MaybeLocal<Object> NewTyped(
		Isolate* isolate, 
		char* data, 
		size_t count
	#if NODE_MODULE_VERSION > IOJS_2_0_MODULE_VERSION
	    , node::Buffer::FreeCallback callback
	#else
	    , node::smalloc::FreeCallback callback
	#endif
	    , void *hint
		, ShmBufferType type
	) {
		size_t length = count * getSize1ForShmBufferType(type);

		EscapableHandleScope scope(isolate);

		/*
		MaybeLocal<Object> mlarr = node::Buffer::New(
			isolate, data, length, callback, hint);
		Local<Object> larr = mlarr.ToLocalChecked();
		
		Uint8Array* arr = (Uint8Array*) *larr;
		Local<ArrayBuffer> ab = arr->Buffer();
		*/
		//Local<ArrayBuffer> ab = ArrayBuffer::New(isolate, data, length, ArrayBufferCreationMode::kExternalized);

		std::shared_ptr<v8::BackingStore> backing = v8::SharedArrayBuffer::NewBackingStore(data, length, 
																						[](void*, size_t, void*){}, nullptr);
		Local<SharedArrayBuffer> ab = v8::SharedArrayBuffer::New(isolate, std::move(backing));
		
		Local<Object> ui;
		switch(type) {
			case SHMBT_INT8:
				ui = Int8Array::New(ab, 0, count);
			break;
			case SHMBT_UINT8:
				ui = Uint8Array::New(ab, 0, count);
			break;
			case SHMBT_UINT8CLAMPED:
				ui = Uint8ClampedArray::New(ab, 0, count);
			break;
			case SHMBT_INT16:
				ui = Int16Array::New(ab, 0, count);
			break;
			case SHMBT_UINT16:
				ui = Uint16Array::New(ab, 0, count);
			break;
			case SHMBT_INT32:
				ui = Int32Array::New(ab, 0, count);
			break;
			case SHMBT_UINT32:
				ui = Uint32Array::New(ab, 0, count);
			break;
			case SHMBT_FLOAT32:
				ui = Float32Array::New(ab, 0, count);
			break;
			default:
			case SHMBT_FLOAT64:
				ui = Float64Array::New(ab, 0, count);
			break;
		}

		return scope.Escape(ui);
	}

}
}

//-------------------------------

namespace Nan {

	inline MaybeLocal<Object> NewTypedBuffer(
	      char *data
	    , size_t count
	#if NODE_MODULE_VERSION > IOJS_2_0_MODULE_VERSION
	    , node::Buffer::FreeCallback callback
	#else
	    , node::smalloc::FreeCallback callback
	#endif
	    , void *hint
	    , ShmBufferType type
	) {
		size_t length = count * getSize1ForShmBufferType(type);

		if (type != SHMBT_BUFFER) {
	  	assert(count <= node::Buffer::kMaxLength && "too large typed buffer");
			#if NODE_MODULE_VERSION > IOJS_2_0_MODULE_VERSION
			    return node::Buffer::NewTyped(
			        Isolate::GetCurrent(), data, count, callback, hint, type);
			#else
			    return MaybeLocal<v8::Object>(node::Buffer::NewTyped(
			        Isolate::GetCurrent(), data, count, callback, hint, type));
			#endif
	  } else {
	  	assert(length <= node::Buffer::kMaxLength && "too large buffer");
			#if NODE_MODULE_VERSION > IOJS_2_0_MODULE_VERSION
			    return node::Buffer::New(
			        Isolate::GetCurrent(), data, length, callback, hint);
			#else
			    return MaybeLocal<v8::Object>(node::Buffer::New(
			        Isolate::GetCurrent(), data, length, callback, hint));
			#endif
	  }

	}

}

//-------------------------------

namespace node {
namespace node_shm {

	using node::AtExit;
	using v8::Local;
	using v8::Number;
	using v8::Object;
	using v8::Value;


	map<key_t,LRU_cache *>  g_LRU_caches_per_segment;
	map<key_t,HH_map *>  g_HMAP_caches_per_segment;
	map<int,size_t> g_ids_to_seg_sizes;
	map<key_t,MutexHolder *> g_MUTEX_per_segment;



	// Arrays to keep info about created segments, call it "info ararys"
	int shmSegmentsCnt = 0;
	size_t shmSegmentsBytes = 0;
	int shmSegmentsCntMax = 0;
	int* shmSegmentsIds = NULL;
	void** shmSegmentsAddrs = NULL;

	// Declare private methods
	static int detachShmSegments();
	static void initShmSegmentsInfo();
	static int detachShmSegment(int resId, void* addr, bool force = false, bool onExit = false);
	static void addShmSegmentInfo(int resId, void* addr, size_t sz);
	static bool hasShmSegmentInfo(int resId);
	static void * getShmSegmentAddr(int resId);
	static bool removeShmSegmentInfo(int resId);
	static void FreeCallback(char* data, void* hint);
	static void Init(Local<Object> target);
	static void AtNodeExit(void*);
	//

	// Init info arrays
	static void initShmSegmentsInfo() {
		detachShmSegments();

		shmSegmentsCnt = 0;
		shmSegmentsCntMax = 16; //will be multiplied by 2 when arrays are full
		shmSegmentsIds = new int[shmSegmentsCntMax];
		shmSegmentsAddrs = new void*[shmSegmentsCntMax];
	}

	// Detach all segments and delete info arrays
	// Returns count of destroyed segments
	static int detachShmSegments() {
		int res = 0;
		if (shmSegmentsCnt > 0) {
			void* addr;
			int resId;
			for (int i = 0 ; i < shmSegmentsCnt ; i++) {
				addr = shmSegmentsAddrs[i];
				resId = shmSegmentsIds[i];
				if (detachShmSegment(resId, addr, false, true) == 0)
					res++;
			}
		}

		SAFE_DELETE_ARR(shmSegmentsIds);
		SAFE_DELETE_ARR(shmSegmentsAddrs);
		shmSegmentsCnt = 0;
		return res;
	}

	// Add segment to info arrays
	static void addShmSegmentInfo(int resId, void* addr, size_t sz) {
		int* newShmSegmentsIds;
		void** newShmSegmentsAddrs;
		if (shmSegmentsCnt == shmSegmentsCntMax) {
			//extend ararys by *2 when full
			shmSegmentsCntMax *= 2;
			newShmSegmentsIds = new int[shmSegmentsCntMax];
			newShmSegmentsAddrs = new void*[shmSegmentsCntMax];
			std::copy(shmSegmentsIds, shmSegmentsIds + shmSegmentsCnt, newShmSegmentsIds);
			std::copy(shmSegmentsAddrs, shmSegmentsAddrs + shmSegmentsCnt, newShmSegmentsAddrs);
			delete [] shmSegmentsIds;
			delete [] shmSegmentsAddrs;
			shmSegmentsIds = newShmSegmentsIds;
			shmSegmentsAddrs = newShmSegmentsAddrs;
		}
		shmSegmentsIds[shmSegmentsCnt] = resId;
		shmSegmentsAddrs[shmSegmentsCnt] = addr;
		shmSegmentsCnt++;
		g_ids_to_seg_sizes[resId] = sz;
	}

	static bool hasShmSegmentInfo(int resId) {
		int* end = shmSegmentsIds + shmSegmentsCnt;
		int* found = std::find(shmSegmentsIds, shmSegmentsIds + shmSegmentsCnt, resId);
		return (found != end);
	}

	static void *getShmSegmentAddr(int resId) {
		int* end = shmSegmentsIds + shmSegmentsCnt;
		int* found = std::find(shmSegmentsIds, shmSegmentsIds + shmSegmentsCnt, resId);
		if (found == end) {
			//not found in info array
			return nullptr;
		}
		int i = found - shmSegmentsIds;
		void* addr = shmSegmentsAddrs[i];
		return(addr);
	}

	// Remove segment from info arrays
	static bool removeShmSegmentInfo(int resId) {
		int* end = shmSegmentsIds + shmSegmentsCnt;
		int* found = std::find(shmSegmentsIds, shmSegmentsIds + shmSegmentsCnt, resId);
		if (found == end)
			return false; //not found
		int i = found - shmSegmentsIds;
		if (i == shmSegmentsCnt-1) {
			//removing last element
		} else {
			std::copy(shmSegmentsIds + i + 1, 
				shmSegmentsIds + shmSegmentsCnt, 
				shmSegmentsIds + i);
			std::copy(shmSegmentsAddrs + i + 1, 
				shmSegmentsAddrs + shmSegmentsCnt,
				shmSegmentsAddrs + i);
		}
		shmSegmentsIds[shmSegmentsCnt-1] = 0;
		shmSegmentsAddrs[shmSegmentsCnt-1] = NULL;
		shmSegmentsCnt--;
		return true;
	}

	// Detach segment
	// Returns count of left attaches or -1 on error
	static int detachShmSegment(int resId, void* addr, bool force, bool onExit) {
		int err;
		struct shmid_ds shminf;
		//detach
		err = shmdt(addr);
		if (err == 0) {
			//get stat
			err = shmctl(resId, IPC_STAT, &shminf);
			if (err == 0) {
				//destroy if there are no more attaches or force==true
				if (force || shminf.shm_nattch == 0) {
					err = shmctl(resId, IPC_RMID, 0);
					if (err == 0) {
						shmSegmentsBytes -= shminf.shm_segsz;
						return 0; //detached and destroyed
					} else {
						if(!onExit)
							Nan::ThrowError(strerror(errno));
					}
				} else {
					return shminf.shm_nattch; //detached, but not destroyed
				}
			} else {
				if(!onExit)
					Nan::ThrowError(strerror(errno));
			}
		} else {
			switch(errno) {
				case EINVAL: // wrong addr
				default:
					if(!onExit)
						Nan::ThrowError(strerror(errno));
				break;
			}
		}
		return -1;
	}



	bool shmCheckKey(key_t key) {

		int resId = shmget(key, 0, 0);
		if (resId == -1) {
			switch(errno) {
				case ENOENT: // not exists
				case EIDRM:  // scheduled for deletion
					return(false);
				default:
					return(false);
			}
		}
		return true;
	}

	// Used only when creating byte-array (Buffer), not typed array
	// Because impl of CallbackInfo::New() is not public (see https://github.com/nodejs/node/blob/v6.x/src/node_buffer.cc)
	// Developer can detach shared memory segments manually by shm.detach()
	// Also shm.detachAll() will be called on process termination
	static void FreeCallback(char* data, void* hint) {
		int resId = reinterpret_cast<intptr_t>(hint);
		void* addr = (void*) data;

		detachShmSegment(resId, addr, false, true);
		removeShmSegmentInfo(resId);
	}

	NAN_METHOD(get) {
		Nan::HandleScope scope;
		int err;
		struct shmid_ds shminf;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		size_t count = Nan::To<uint32_t>(info[1]).FromJust();
		int shmflg = Nan::To<uint32_t>(info[2]).FromJust();
		int at_shmflg = Nan::To<uint32_t>(info[3]).FromJust();
		ShmBufferType type = (ShmBufferType) Nan::To<int32_t>(info[4]).FromJust();
		size_t size = count * getSize1ForShmBufferType(type);
		bool isCreate = (size > 0);
		
		int resId = shmget(key, size, shmflg);
		if (resId == -1) {
			switch(errno) {
				case EEXIST: // already exists
				case EIDRM:  // scheduled for deletion
				case ENOENT: // not exists
					info.GetReturnValue().SetNull();
					return;
				case EINVAL: // should be SHMMIN <= size <= SHMMAX
					return Nan::ThrowRangeError(strerror(errno));
				default:
					return Nan::ThrowError(strerror(errno));
			}
		} else {
			if (!isCreate) {
				err = shmctl(resId, IPC_STAT, &shminf);
				if (err == 0) {
					size = shminf.shm_segsz;
					count = size / getSize1ForShmBufferType(type);
				} else
					return Nan::ThrowError(strerror(errno));
			}
			
			void* res = shmat(resId, NULL, at_shmflg);
			if (res == (void *)-1)
				return Nan::ThrowError(strerror(errno));

			if (!hasShmSegmentInfo(resId)) {
				addShmSegmentInfo(resId, res, size);
				shmSegmentsBytes += size;
			}

			info.GetReturnValue().Set(Nan::NewTypedBuffer(
				reinterpret_cast<char*>(res),
				count,
				FreeCallback,
				reinterpret_cast<void*>(static_cast<intptr_t>(resId)),
				type
			).ToLocalChecked());
		}
	}

	NAN_METHOD(detach) {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		bool forceDestroy = Nan::To<bool>(info[1]).FromJust();
		//
		bool check = (g_LRU_caches_per_segment.find(key) != g_LRU_caches_per_segment.end());
		if ( check) {
			g_LRU_caches_per_segment.erase(key);
		}
		check =  (g_HMAP_caches_per_segment.find(key) != g_HMAP_caches_per_segment.end());
		if ( check) {
			g_HMAP_caches_per_segment.erase(key);
		}
		//
		int resId = shmget(key, 0, 0);
		if (resId == -1) {
			switch(errno) {
				case ENOENT: // not exists
				case EIDRM:  // scheduled for deletion
					info.GetReturnValue().Set(Nan::New<Number>(-1));
					return;
				default:
					return Nan::ThrowError(strerror(errno));
			}
		} else {
			int* end = shmSegmentsIds + shmSegmentsCnt;
			int* found = std::find(shmSegmentsIds, shmSegmentsIds + shmSegmentsCnt, resId);
			if (found == end) {
				//not found in info array
				info.GetReturnValue().Set(Nan::New<Number>(-1));
				return;
			}
			int i = found - shmSegmentsIds;
			void* addr = shmSegmentsAddrs[i];

			int res = detachShmSegment(resId, addr, forceDestroy);
			if (res != -1)
				removeShmSegmentInfo(resId);
			info.GetReturnValue().Set(Nan::New<Number>(res));
		}
	}

	NAN_METHOD(detachAll) {
		int cnt = detachShmSegments();
		initShmSegmentsInfo();
		info.GetReturnValue().Set(Nan::New<Number>(cnt));
	}

	NAN_METHOD(getTotalSize) {
		info.GetReturnValue().Set(Nan::New<Number>(shmSegmentsBytes));
	}

	// node::AtExit
	static void AtNodeExit(void*) {
		detachShmSegments();
	}


	// fixed size data elements 
	NAN_METHOD(initLRU) {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		size_t rc_sz = Nan::To<uint32_t>(info[1]).FromJust();
		size_t seg_sz = Nan::To<uint32_t>(info[2]).FromJust();
		bool am_initializer = Nan::To<bool>(info[3]).FromJust();
		//
		int resId = shmget(key, 0, 0);
		if (resId == -1) {
			switch(errno) {
				case ENOENT: // not exists
				case EIDRM:  // scheduled for deletion
					info.GetReturnValue().Set(Nan::New<Number>(-1));
					return;
				default:
					return Nan::ThrowError(strerror(errno));
			}
		}
		
		LRU_cache *plr = g_LRU_caches_per_segment[key];
		//
		if ( hasShmSegmentInfo(resId) ) {
			if ( plr != nullptr ) {
				info.GetReturnValue().Set(Nan::New<Number>(plr->max_count()));
			} else {
				void *region = getShmSegmentAddr(resId);
				if ( region == nullptr ) {
					info.GetReturnValue().Set(Nan::New<Number>(-1));
					return;
				}
				//
				size_t rec_size = rc_sz;
				size_t seg_size = g_ids_to_seg_sizes[resId];
				seg_size = ( seg_size > seg_sz ) ? seg_sz : seg_size;
				g_ids_to_seg_sizes[resId] = seg_size;

				LRU_cache *lru_cache = new LRU_cache(region,rec_size,seg_size,am_initializer);
				if ( lru_cache->ok() ) {
					g_LRU_caches_per_segment[key] = lru_cache;
					info.GetReturnValue().Set(Nan::New<Number>(lru_cache->max_count()));
				} else {
					return Nan::ThrowError("Bad parametes for initLRU");
				}


			}
		} else {
			if ( plr != nullptr ) {
				g_LRU_caches_per_segment.erase(key);
			}
			info.GetReturnValue().Set(Nan::New<Number>(-1));
		}
	}


	
	// fixed size data elements 
	NAN_METHOD(initHopScotch) {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		key_t lru_key =  Nan::To<uint32_t>(info[1]).FromJust();
		bool am_initializer = Nan::To<bool>(info[2]).FromJust();
		size_t max_element_count = Nan::To<uint32_t>(info[3]).FromJust();
		//
		int resId = shmget(key, 0, 0);
		if (resId == -1) {
			switch(errno) {
				case ENOENT: // not exists
				case EIDRM:  // scheduled for deletion
					info.GetReturnValue().Set(Nan::New<Number>(-1));
					return;
				default:
					return Nan::ThrowError(strerror(errno));
			}
		}
		//
		HH_map *phm = g_HMAP_caches_per_segment[key];
		//
		if ( hasShmSegmentInfo(resId) ) {
			if ( phm != nullptr ) {
				info.GetReturnValue().Set(Nan::New<Number>(key));
			} else {
				void *region = getShmSegmentAddr(resId);
				if ( region == nullptr ) {
					info.GetReturnValue().Set(Nan::New<Number>(-1));
					return;
				}
				//
				HH_map *hmap = new HH_map(region,max_element_count,am_initializer);
				if ( hmap->ok() ) {
					g_HMAP_caches_per_segment[key] = hmap;
					// assign this HH_map to an LRU
					LRU_cache *lru_cache = g_LRU_caches_per_segment[lru_key];
					if ( lru_cache == nullptr ) {
						if ( shmCheckKey(key) ) {
							info.GetReturnValue().Set(Nan::New<Boolean>(false));
						} else {
							info.GetReturnValue().Set(Nan::New<Number>(-2));
						}
					} else {
						lru_cache->set_hash_impl(hmap);
						info.GetReturnValue().Set(Nan::New<Boolean>(true));
					}
					//
				} else {
					return Nan::ThrowError("Bad parametes for initLRU");
				}
				//
			}

		} else {
			if ( phm != nullptr ) {
				g_HMAP_caches_per_segment.erase(key);
			}
			info.GetReturnValue().Set(Nan::New<Number>(-1));
		}
	}




	NAN_METHOD(getSegSize) {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();

		int resId = shmget(key, 0, 0);
		if (resId == -1) {
			switch(errno) {
				case ENOENT: // not exists
				case EIDRM:  // scheduled for deletion
					info.GetReturnValue().Set(Nan::New<Number>(-1));
					return;
				default:
					return Nan::ThrowError(strerror(errno));
			}
		}
		size_t seg_size = g_ids_to_seg_sizes[resId];
		info.GetReturnValue().Set(Nan::New<Number>(seg_size));
	}



	NAN_METHOD(getMaxCount) {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();

		LRU_cache *lru_cache = g_LRU_caches_per_segment[key];
		if ( lru_cache == nullptr ) {
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			uint32_t maxcount = lru_cache->max_count();
			info.GetReturnValue().Set(Nan::New<Number>(maxcount));
		}
	}

	NAN_METHOD(getCurrentCount) {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();

		LRU_cache *lru_cache = g_LRU_caches_per_segment[key];
		if ( lru_cache == nullptr ) {
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			uint32_t count = lru_cache->current_count();
			info.GetReturnValue().Set(Nan::New<Number>(count));
		}
	}


	NAN_METHOD(getFreeCount) {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();

		LRU_cache *lru_cache = g_LRU_caches_per_segment[key];
		if ( lru_cache == nullptr ) {
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			uint32_t count = lru_cache->free_count();
			info.GetReturnValue().Set(Nan::New<Number>(count));
		}
	}


	// time_since_epoch
	// helper to return the time in milliseconds
	NAN_METHOD(time_since_epoch) {
		Nan::HandleScope scope;
		uint64_t epoch_time;
		epoch_time = epoch_ms();
		info.GetReturnValue().Set(Nan::New<Number>(epoch_time));
	}


	// set el -- add a new entry to the LRU.  IF the LRU is full, return indicative value.
	//
	NAN_METHOD(set_el)  {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		uint32_t hash_bucket = Nan::To<uint32_t>(info[1]).FromJust();
		uint32_t full_hash = Nan::To<uint32_t>(info[2]).FromJust();
		Utf8String data_arg(info[3]);
		//
		// originally full_hash is the whole 32 bit hash and hash_bucket is the modulus of it by the number of buckets
		uint64_t hash64 = (((uint64_t)full_hash << HALF) | (uint64_t)hash_bucket);
		//

		// First check to see if a buffer was every allocated
		LRU_cache *lru_cache = g_LRU_caches_per_segment[key];
		if ( lru_cache == nullptr ) {		// buffer was not set yield an error
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			char *data = *data_arg;
			// is the key already assigned ?  >> check_for_hash 
			uint32_t offset = lru_cache->check_for_hash(hash64);
			if ( offset == UINT32_MAX ) {  // no -- go ahead and add a new element  >> add_el
				offset = lru_cache->add_el(data,hash64);
				if ( offset == UINT32_MAX ) {
					info.GetReturnValue().Set(Nan::New<Boolean>(false));
				} else {
					info.GetReturnValue().Set(Nan::New<Number>(offset));
				}
			} else {
				// there is already data -- so attempt ot update the element with new data.
				if ( lru_cache->update_el(offset,data) ) {
					info.GetReturnValue().Set(Nan::New<Number>(offset));
				} else {
					info.GetReturnValue().Set(Nan::New<Boolean>(false));
				}
			}
		}
	}


	// set_many -- add list of new entries to the LRU.  Return the results of adding.
	//
	NAN_METHOD(set_many)  {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		Local<Array> jsArray = Local<Array>::Cast(info[1]);
		//
		// First check to see if a buffer was every allocated
		LRU_cache *lru_cache = g_LRU_caches_per_segment[key];
		if ( lru_cache == nullptr ) {		// buffer was not set yield an error
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			// important -- the code is only really simple to write if v8 is used straightup.
			// nan will help get the context -- use v8 get and set with the context
			v8::Local<v8::Context> context = info.GetIsolate()->GetCurrentContext();
			//
			uint16_t n = jsArray->Length();
			Local<Array> jsArrayResults = Nan::New<Array>(n);
			//
//cout << "N " << n << endl;
			//
			for (uint16_t i = 0; i < n; i++) {		
				Local<v8::Array> jsSubArray = Local<Array>::Cast(jsArray->Get(context, i).ToLocalChecked());
        		uint32_t hash = jsSubArray->Get(context, 0).ToLocalChecked()->Uint32Value(context).FromJust();
        		uint32_t index = jsSubArray->Get(context, 1).ToLocalChecked()->Uint32Value(context).FromJust();
				Utf8String data_arg(jsSubArray->Get(context, 2).ToLocalChecked());
				uint64_t hash64 = (((uint64_t)index << HALF) | (uint64_t)hash);
				char *data = *data_arg;
	//cout << data << endl;
				// is the key already assigned ?  >> check_for_hash 
				uint32_t offset = lru_cache->check_for_hash(hash64);
				if ( offset == UINT32_MAX ) {  // no -- go ahead and add a new element  >> add_el
					offset = lru_cache->add_el(data,hash64);
					if ( offset == UINT32_MAX ) {
						Nan::Set(jsArrayResults, i, Nan::New<Boolean>(false));
					} else {
						Nan::Set(jsArrayResults, i, Nan::New<Number>(offset));
					}
				} else {
					// there is already data -- so attempt ot update the element with new data.
					if ( lru_cache->update_el(offset,data) ) {
						Nan::Set(jsArrayResults, i, Nan::New<Number>(offset));
					} else {
						Nan::Set(jsArrayResults, i, Nan::New<Boolean>(false));
					}
				}
				//
			}
			info.GetReturnValue().Set(jsArrayResults);
		}
	}


	NAN_METHOD(get_el)  {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		key_t index = Nan::To<uint32_t>(info[1]).FromJust();
		LRU_cache *lru_cache = g_LRU_caches_per_segment[key];
		if ( lru_cache == nullptr ) {
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			char data[lru_cache->record_size()];
			uint8_t rslt = lru_cache->get_el(index,data);
			if ( rslt == 0 || rslt == 1 ) {
				if ( rslt == 0 ) {
					info.GetReturnValue().Set(New(data).ToLocalChecked());
				} else {
					string fix_data = strdup(data);
					string prefix = "DELETED: ";
					fix_data = prefix + fix_data;
					memset(data,0,lru_cache->record_size());
					memcpy(data,fix_data.c_str(),
										min( fix_data.size(), (lru_cache->record_size() - 1)) );
					info.GetReturnValue().Set(New(data).ToLocalChecked());
				}
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-2));
			}
		}
	}


	NAN_METHOD(get_el_hash)  {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		uint32_t hash = Nan::To<uint32_t>(info[1]).FromJust();
		uint32_t index = Nan::To<uint32_t>(info[2]).FromJust();
		//
		uint64_t hash64 = (((uint64_t)index << HALF) | (uint64_t)hash);
		//
//cout << "get h> " << hash << " i> " << index << " " << hash64 << endl;
		//
		LRU_cache *lru_cache = g_LRU_caches_per_segment[key];
		if ( lru_cache == nullptr ) {
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			uint32_t index = lru_cache->check_for_hash(hash64);
			if ( index == UINT32_MAX ) {
				info.GetReturnValue().Set(Nan::New<Number>(-2));
			} else {
				char data[lru_cache->record_size()];
				uint8_t rslt = lru_cache->get_el(index,data);
				if ( rslt == 0 ) {
					if ( rslt == 0 ) {
						info.GetReturnValue().Set(New(data).ToLocalChecked());
					}
				} else {
					info.GetReturnValue().Set(Nan::New<Number>(-2));
				}
			}
		}
	}



	NAN_METHOD(del_el)  {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		key_t index = Nan::To<uint32_t>(info[1]).FromJust();
		//
		LRU_cache *lru_cache = g_LRU_caches_per_segment[key];
		if ( lru_cache == nullptr ) {
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			if ( lru_cache->del_el(index) ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(true));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-2));
			}
		}
	}

	NAN_METHOD(del_key)  {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		uint32_t hash = Nan::To<uint32_t>(info[1]).FromJust();		// bucket index
		uint32_t full_hash = Nan::To<uint32_t>(info[2]).FromJust();
		uint64_t hash64 = (((uint64_t)full_hash << HALF) | (uint64_t)hash);
		//
		LRU_cache *lru_cache = g_LRU_caches_per_segment[key];
		if ( lru_cache == nullptr ) {
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			uint32_t index = lru_cache->check_for_hash(hash64);
			if ( index == UINT32_MAX ) {
				info.GetReturnValue().Set(Nan::New<Number>(-2));
			} else {
				if ( lru_cache->del_el(index) ) {
					info.GetReturnValue().Set(Nan::New<Boolean>(true));
				} else {
					info.GetReturnValue().Set(Nan::New<Number>(-2));
				}
			}

		}
	}

	NAN_METHOD(remove_key)  {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		uint32_t hash = Nan::To<uint32_t>(info[1]).FromJust();
		uint32_t index = Nan::To<uint32_t>(info[2]).FromJust();
		uint64_t hash64 = (((uint64_t)index << HALF) | (uint64_t)hash);
		//
		LRU_cache *lru_cache = g_LRU_caches_per_segment[key];
		if ( lru_cache == nullptr ) {
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			lru_cache->remove_key(hash64);
			info.GetReturnValue().Set(Nan::New<Boolean>(true));
		}
	}

	NAN_METHOD(set_share_key)  {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		key_t index = Nan::To<uint32_t>(info[1]).FromJust();
		uint32_t share_key = Nan::To<uint32_t>(info[2]).FromJust();
		//
		LRU_cache *lru_cache = g_LRU_caches_per_segment[key];
		if ( lru_cache == nullptr ) {
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			if ( lru_cache->set_share_key(index,share_key) ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(true));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-2));
			}
		}
	}

	NAN_METHOD(get_last_reason)  {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		//
		LRU_cache *lru_cache = g_LRU_caches_per_segment[key];
		if ( lru_cache == nullptr ) {
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			const char *reason = lru_cache->get_last_reason();
			info.GetReturnValue().Set(New(reason).ToLocalChecked());
		}
	}

	NAN_METHOD(reload_hash_map)  {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		//
		LRU_cache *lru_cache = g_LRU_caches_per_segment[key];
		if ( lru_cache == nullptr ) {
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			lru_cache->reload_hash_map();
			info.GetReturnValue().Set(Nan::New<Boolean>(true));
		}
	}

	NAN_METHOD(reload_hash_map_update)  {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		uint32_t share_key = Nan::To<uint32_t>(info[0]).FromJust();
		//
		LRU_cache *lru_cache = g_LRU_caches_per_segment[key];
		if ( lru_cache == nullptr ) {
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			lru_cache->reload_hash_map_update(share_key);
			info.GetReturnValue().Set(Nan::New<Boolean>(true));
		}
	}

	NAN_METHOD(run_lru_eviction)  {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		time_t cutoff = Nan::To<uint32_t>(info[1]).FromJust();
		uint32_t max_evict_b = Nan::To<uint32_t>(info[2]).FromJust();
		LRU_cache *lru_cache = g_LRU_caches_per_segment[key];
		if ( lru_cache == nullptr ) {
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			list<uint64_t> evict_list;
			uint8_t max_evict = (uint8_t)(max_evict_b);
			time_t time_shift = epoch_ms();
			time_shift -= cutoff;
			lru_cache->evict_least_used(time_shift,max_evict,evict_list);
			string evicted_hash_as_str = joiner(evict_list);
			info.GetReturnValue().Set(New(evicted_hash_as_str.c_str()).ToLocalChecked());
		}
	}

	NAN_METHOD(run_lru_eviction_get_values)  {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		time_t cutoff = Nan::To<uint32_t>(info[1]).FromJust();
		uint32_t max_evict_b = Nan::To<uint32_t>(info[2]).FromJust();
		LRU_cache *lru_cache = g_LRU_caches_per_segment[key];
		if ( lru_cache == nullptr ) {
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			map<uint64_t,char *> evict_map;
			uint8_t max_evict = (uint8_t)(max_evict_b);
			time_t time_shift = epoch_ms();
			time_shift -= cutoff;
			lru_cache->evict_least_used_to_value_map(time_shift,max_evict,evict_map);

			//string test = map_maker_destruct(evict_map);
			//cout << test << endl;

			Local<Object> jsObject = Nan::New<Object>();
			js_map_maker_destruct(evict_map,jsObject);
			info.GetReturnValue().Set(jsObject);
		}
	}

	NAN_METHOD(run_lru_targeted_eviction_get_values)  {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		time_t cutoff = Nan::To<uint32_t>(info[1]).FromJust();
		uint32_t max_evict_b = Nan::To<uint32_t>(info[2]).FromJust();
		//
		uint32_t hash_bucket = Nan::To<uint32_t>(info[3]).FromJust();
		uint32_t original_hash = Nan::To<uint32_t>(info[4]).FromJust();
		//
		uint64_t hash64 = (((uint64_t)index << HALF) | (uint64_t)original_hash);

		LRU_cache *lru_cache = g_LRU_caches_per_segment[key];
		if ( lru_cache == nullptr ) {
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			map<uint64_t,char *> evict_map;
			uint8_t max_evict = (uint8_t)(max_evict_b);
			time_t time_shift = epoch_ms();
			time_shift -= cutoff;
			//
			uint8_t evict_count = lru_cache->evict_least_used_near_hash(hash_bucket,time_shift,max_evict,evict_map);
			//
			if ( evict_count < max_evict ) {
				uint8_t remaining = max_evict - evict_count;
				lru_cache->evict_least_used_to_value_map(time_shift,remaining,evict_map);
			}

			//string test = map_maker_destruct(evict_map);
			//cout << test << endl;

			Local<Object> jsObject = Nan::New<Object>();
			js_map_maker_destruct(evict_map,jsObject);
			info.GetReturnValue().Set(jsObject);
		}
	}


	NAN_METHOD(debug_dump_list)  {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		bool backwards = Nan::To<bool>(info[1]).FromJust();
		//
		LRU_cache *lru_cache = g_LRU_caches_per_segment[key];
		if ( lru_cache == nullptr ) {
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			list<uint32_t> evict_list;
			lru_cache->_walk_allocated_list(3,backwards);
			info.GetReturnValue().Set(Nan::New<Boolean>(true));
		}	
	}


	// MUTEX

	// fixed size data elements 
	NAN_METHOD(init_mutex) {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		bool am_initializer = Nan::To<bool>(info[1]).FromJust();
		//
		int resId = shmget(key, 0, 0);
		if (resId == -1) {
			switch(errno) {
				case ENOENT: // not exists
				case EIDRM:  // scheduled for deletion
					info.GetReturnValue().Set(Nan::New<Number>(-1));
					return;
				default:
					return Nan::ThrowError(strerror(errno));
			}
		}
		
		MutexHolder *mtx = g_MUTEX_per_segment[key];
		//
		if ( hasShmSegmentInfo(resId) ) {
			if ( mtx != nullptr ) {
				// just say that access through the key is possible
				info.GetReturnValue().Set(Nan::New<Boolean>(true));
			} else {
				// setup the access
				void *region = getShmSegmentAddr(resId);
				if ( region == nullptr ) {
					info.GetReturnValue().Set(Nan::New<Number>(-1));
					return;
				}
				//
				mtx = new MutexHolder(region,am_initializer);

				if ( mtx->ok() ) {
					g_MUTEX_per_segment[key] = mtx;
					info.GetReturnValue().Set(Nan::New<Boolean>(true));
				} else {
					string throw_message = mtx->get_last_reason();
					return Nan::ThrowError(throw_message.c_str());
				}
			}
		} else {
			if ( mtx != nullptr ) {
				g_MUTEX_per_segment.erase(key);
			}
			info.GetReturnValue().Set(Nan::New<Number>(-1));
		}
	}


	NAN_METHOD(get_last_mutex_reason)  {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		//
		MutexHolder *mtx = g_MUTEX_per_segment[key];
		if ( mtx == nullptr ) {
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			string mtx_reason = mtx->get_last_reason();
			const char *reason = mtx_reason.c_str();
			info.GetReturnValue().Set(New(reason).ToLocalChecked());
		}
	}



	NAN_METHOD(try_lock)  {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		//
		MutexHolder *mtx = g_MUTEX_per_segment[key];
		if ( mtx == nullptr ) {
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Number>(-3));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			if ( mtx->try_lock() ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(true));
			}
			if ( mtx->ok() ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		}
	}


	NAN_METHOD(lock)  {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		//
		MutexHolder *mtx = g_MUTEX_per_segment[key];
		if ( mtx == nullptr ) {
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Number>(-3));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			if ( mtx->lock() ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(true));
			} else {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			}
		}
	}



	NAN_METHOD(unlock)  {
		Nan::HandleScope scope;
		key_t key = Nan::To<uint32_t>(info[0]).FromJust();
		//
		MutexHolder *mtx = g_MUTEX_per_segment[key];
		if ( mtx == nullptr ) {
			if ( shmCheckKey(key) ) {
				info.GetReturnValue().Set(Nan::New<Number>(-3));
			} else {
				info.GetReturnValue().Set(Nan::New<Number>(-1));
			}
		} else {
			if ( mtx->unlock() ) {
				info.GetReturnValue().Set(Nan::New<Boolean>(true));
			} else {
				info.GetReturnValue().Set(Nan::New<Boolean>(false));
			}
		}
	}


	// Init module
	static void Init(Local<Object> target) {
		initShmSegmentsInfo();
		
		Nan::SetMethod(target, "get", get);
		Nan::SetMethod(target, "detach", detach);
		Nan::SetMethod(target, "detachAll", detachAll);
		Nan::SetMethod(target, "getTotalSize", getTotalSize);
		// ---- ---- ---- ---- ---- ---- ---- ---- ---- ----
		Nan::SetMethod(target, "initLRU", initLRU);
		Nan::SetMethod(target, "getSegSize", getSegSize);
		Nan::SetMethod(target, "max_count", getMaxCount);
		Nan::SetMethod(target, "current_count", getCurrentCount);
		Nan::SetMethod(target, "free_count", getFreeCount);
		Nan::SetMethod(target, "epoch_time", time_since_epoch);

		// ---- ---- ---- ---- ---- ---- ---- ---- ---- ----
		Nan::SetMethod(target, "set_el", set_el);
		Nan::SetMethod(target, "set_many", set_many);
		//
		Nan::SetMethod(target, "get_el", get_el);
		Nan::SetMethod(target, "del_el", del_el);
		Nan::SetMethod(target, "del_key", del_key);
		Nan::SetMethod(target, "remove_key", remove_key);

		// ---- ---- ---- ---- ---- ---- ---- ---- ---- ----
		Nan::SetMethod(target, "get_el_hash", get_el_hash);
		Nan::SetMethod(target, "get_last_reason", get_last_reason);
		Nan::SetMethod(target, "reload_hash_map", reload_hash_map);
		Nan::SetMethod(target, "reload_hash_map_update", reload_hash_map_update);
		Nan::SetMethod(target, "set_share_key", set_share_key);
		//
		Nan::SetMethod(target, "debug_dump_list", debug_dump_list);
		//
		// HOPSCOTCH HASH
		Nan::SetMethod(target, "initHopScotch", initHopScotch);
		//
		Nan::SetMethod(target, "run_lru_eviction", run_lru_eviction);
		Nan::SetMethod(target, "run_lru_eviction_get_values", run_lru_eviction_get_values);

		// ---- ---- ---- ---- ---- ---- ---- ---- ---- ----

		Nan::SetMethod(target, "init_mutex", init_mutex);
		Nan::SetMethod(target, "try_lock", try_lock);
		Nan::SetMethod(target, "lock", lock);
		Nan::SetMethod(target, "unlock", unlock);
		Nan::SetMethod(target, "get_last_mutex_reason", get_last_mutex_reason);
	
		//
		// ---- ---- ---- ---- ---- ---- ---- ---- ---- ----
		Nan::Set(target, Nan::New("IPC_PRIVATE").ToLocalChecked(), Nan::New<Number>(IPC_PRIVATE));
		Nan::Set(target, Nan::New("IPC_CREAT").ToLocalChecked(), Nan::New<Number>(IPC_CREAT));
		Nan::Set(target, Nan::New("IPC_EXCL").ToLocalChecked(), Nan::New<Number>(IPC_EXCL));
		Nan::Set(target, Nan::New("SHM_RDONLY").ToLocalChecked(), Nan::New<Number>(SHM_RDONLY));
		Nan::Set(target, Nan::New("NODE_BUFFER_MAX_LENGTH").ToLocalChecked(), Nan::New<Number>(node::Buffer::kMaxLength));
		//enum ShmBufferType
		Nan::Set(target, Nan::New("SHMBT_BUFFER").ToLocalChecked(), Nan::New<Number>(SHMBT_BUFFER));
		Nan::Set(target, Nan::New("SHMBT_INT8").ToLocalChecked(), Nan::New<Number>(SHMBT_INT8));
		Nan::Set(target, Nan::New("SHMBT_UINT8").ToLocalChecked(), Nan::New<Number>(SHMBT_UINT8));
		Nan::Set(target, Nan::New("SHMBT_UINT8CLAMPED").ToLocalChecked(), Nan::New<Number>(SHMBT_UINT8CLAMPED));
		Nan::Set(target, Nan::New("SHMBT_INT16").ToLocalChecked(), Nan::New<Number>(SHMBT_INT16));
		Nan::Set(target, Nan::New("SHMBT_UINT16").ToLocalChecked(), Nan::New<Number>(SHMBT_UINT16));
		Nan::Set(target, Nan::New("SHMBT_INT32").ToLocalChecked(), Nan::New<Number>(SHMBT_INT32));
		Nan::Set(target, Nan::New("SHMBT_UINT32").ToLocalChecked(), Nan::New<Number>(SHMBT_UINT32));
		Nan::Set(target, Nan::New("SHMBT_FLOAT32").ToLocalChecked(), Nan::New<Number>(SHMBT_FLOAT32));
		Nan::Set(target, Nan::New("SHMBT_FLOAT64").ToLocalChecked(), Nan::New<Number>(SHMBT_FLOAT64));


		Isolate* isolate = target->GetIsolate();
		AddEnvironmentCleanupHook(isolate,AtNodeExit,nullptr);
		//node::AtExit(AtNodeExit);
	}

}
}

//-------------------------------

NODE_MODULE(shm, node::node_shm::Init);
