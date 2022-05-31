## Mutex Operations and Code

The shared memory regions keep data structures, which should be altered only atomically. Shared memory is set up so that more than on process can call into the shared regions. So, processes should share a mutex, which will also reside in a shared region.

The POSIX mutex has been programmed to use a shared memory region. The POSIX mutex subroutines can be given reference to the region, enabling the use of locks.

The C++ code for the Mutex is in **node\_shm.h**.

Here us an example of the initializer from **shm-lru-cache**:

```
    let sz = INTER_PROC_DESCRIPTOR_WORDS
    this.initializer = conf.initialzer
    if ( this.initializer ) {
        this.com_buffer = shm.create(proc_count*sz + SUPER_HEADER,'Uint32Array',this.shm_com_key)
    } else {
        this.com_buffer = shm.get(this.shm_com_key,'Uint32Array')
    }
    //
    ...
    // put the mutex at the very start of the communicator region.
    shm.init_mutex(this.shm_com_key,this.initializer) 

```

The method, *init\_mutex* sets up the necessary data structures for the calling process. In particular, in the C++ code the mutex methods are alerted the share memory region will be used:

```
result = pthread_mutexattr_setpshared(&_mutex_attributes,PTHREAD_PROCESS_SHARED);
```

The other methods simply wrap the POSIX pthread calls, e.g. in C++:

```
 pthread_mutex_lock( _mutex_ptr );
```

Here is a usage example in the JavaScript side:

```
lock_asset() {
    if ( this.proc_index >= 0 && this.com_buffer.length ) {
        if ( this.asset_lock ) return; // it is already locked
        //
        let result = shm.lock(this.shm_com_key)
        if ( result !== true ) {
            console.log(shm.get_last_mutex_reason(this.shm_com_key))
        }
    }
}
```

