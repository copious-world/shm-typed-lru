#ifndef _H_HAMP_INTERFACE_
#define _H_HAMP_INTERFACE_

#include <sys/ipc.h>
#include <sys/types.h>
#include <sys/shm.h>

#include <iostream>
#include <sstream>

using namespace node;
using namespace v8;
using namespace std;



class HMap_interface {
	public:
		virtual uint64_t store(uint32_t hash_bucket, uint32_t el_key, uint32_t v_value) = 0;
		virtual uint32_t get(uint64_t key) = 0;
		virtual uint32_t get(uint32_t key,uint32_t bucket) = 0;
		virtual uint8_t get_bucket(uint32_t h, uint32_t xs[32]) = 0;
		virtual uint32_t del(uint64_t key) = 0;
		virtual void	 clear(void) = 0;
};





#endif // _H_HAMP_INTERFACE_