#pragma once
// Some common atomic helpers
// See https://en.cppreference.com/w/c/atomic
// See https://en.cppreference.com/w/c/atomic/memory_order

#ifdef __cplusplus
  #include <atomic>
  #define r_memory_order(name) ::std::memory_order::memory_order_##name
  typedef ::std::atomic<bool>    atomic_bool;
  typedef ::std::atomic<i8>      atomic_i8;
  typedef ::std::atomic<u8>      atomic_u8;
  typedef ::std::atomic<i16>     atomic_i16;
  typedef ::std::atomic<u16>     atomic_u16;
  typedef ::std::atomic<i32>     atomic_i32;
  typedef ::std::atomic<u32>     atomic_u32;
  typedef ::std::atomic<i64>     atomic_i64;
  typedef ::std::atomic<u64>     atomic_u64;
  typedef ::std::atomic<f32>     atomic_f32;
  typedef ::std::atomic<f64>     atomic_f64;
  typedef ::std::atomic<uint>    atomic_uint;
  typedef ::std::atomic<size_t>  atomic_size;
  typedef ::std::atomic<ssize_t> atomic_ssize;

  // ATOMIC_VAR_INIT is defined as "define ATOMIC_VAR_INIT(__v) {__v}" which triggers
  // -Wbraced-scalar-init for scalars in C++.
  #ifdef ATOMIC_VAR_INIT
    #undef ATOMIC_VAR_INIT
  #endif
  #define ATOMIC_VAR_INIT(x) x
#else
  #define r_memory_order(name) memory_order_##name
  typedef _Atomic(bool)    atomic_bool;
  typedef _Atomic(i8)      atomic_i8;
  typedef _Atomic(u8)      atomic_u8;
  typedef _Atomic(i16)     atomic_i16;
  typedef _Atomic(u16)     atomic_u16;
  typedef _Atomic(i32)     atomic_i32;
  typedef _Atomic(u32)     atomic_u32;
  typedef _Atomic(i64)     atomic_i64;
  typedef _Atomic(u64)     atomic_u64;
  typedef _Atomic(f32)     atomic_f32;
  typedef _Atomic(f64)     atomic_f64;
  typedef _Atomic(uint)    atomic_uint;
  typedef _Atomic(size_t)  atomic_size;
  typedef _Atomic(ssize_t) atomic_ssize;
#endif

#define AtomicLoadx(x, mo)     atomic_load_explicit((x), (mo))
#define AtomicLoad(x)          atomic_load_explicit((x), r_memory_order(relaxed))
#define AtomicLoadAcq(x)       atomic_load_explicit((x), r_memory_order(acquire))
#define AtomicStorex(x, v, mo) atomic_store_explicit((x), (v), (mo))
#define AtomicStore(x, v)      atomic_store_explicit((x), (v), r_memory_order(relaxed))
#define AtomicStoreRel(x, v)   atomic_store_explicit((x), (v), r_memory_order(release))

// note: these operations return the previous value; _before_ applying the operation
#define AtomicAdd(x, n)      atomic_fetch_add_explicit((x), (n), r_memory_order(relaxed))
#define AtomicAddx(x, n, mo) atomic_fetch_add_explicit((x), (n), (mo))
#define AtomicSub(x, n)      atomic_fetch_sub_explicit((x), (n), r_memory_order(relaxed))
#define AtomicOr(x, n)       atomic_fetch_or_explicit((x), (n), r_memory_order(relaxed))
#define AtomicAnd(x, n)      atomic_fetch_and_explicit((x), (n), r_memory_order(relaxed))
#define AtomicXor(x, n)      atomic_fetch_xor_explicit((x), (n), r_memory_order(relaxed))

#define AtomicCASx(p, oldval, newval, mosuccess, mofail) \
  atomic_compare_exchange_strong_explicit((p), (oldval), (newval), (mosuccess), (mofail))

#define AtomicCAS(p, oldval, newval) \
  atomic_compare_exchange_strong_explicit( \
    (p), (oldval), (newval), r_memory_order(relaxed), r_memory_order(relaxed))

#define AtomicCASRel(p, oldval, newval) \
  atomic_compare_exchange_strong_explicit( \
    (p), (oldval), (newval), r_memory_order(release), r_memory_order(relaxed))

#define AtomicCASAcqRel(p, oldval, newval) \
  atomic_compare_exchange_strong_explicit( \
    (p), (oldval), (newval), r_memory_order(acq_rel), r_memory_order(relaxed))

// // r_atomic_once(flag, statement):
// //   static r_atomic_once_flag once;
// //   r_atomic_once(&once, { /* only run once */ });
// // Note: Threads losing the race does NOT wait or sync. Use r_sync_once() for that instead.
// #define r_atomic_once_flag _Atomic(u32)
// #define r_atomic_once(flagptr, stmt) \
//   ({ u32 zero = 0; \
//      if (atomic_compare_exchange_strong_explicit(\
//          (flagptr), &zero, 1, r_memory_order(acq_rel), r_memory_order(relaxed))) \
//        stmt; \
//   })
