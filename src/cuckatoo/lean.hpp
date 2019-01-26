// Cuck(at)oo Cycle, a memory-hard proof-of-work
// Copyright (c) 2013-2019 John Tromp
// The edge-trimming memory optimization is due to Dave Andersen
// http://da-data.blogspot.com/2014/03/a-public-review-of-cuckoo-cycle.html
// The use of prefetching was suggested by Alexander Peslyak (aka Solar Designer)

#include "stdio.h"

#ifdef ATOMIC
#include <atomic>
#endif
#include "cuckatoo.h"
#include "../crypto/siphashxN.h"
#include "graph.hpp"
#include <stdio.h>
#include <pthread.h>
#include "../threads/barrier.hpp"
#include <assert.h>

#ifndef MAXSOLS
#define MAXSOLS 4
#endif

typedef uint64_t u64; // save some typing

#ifdef ATOMIC
typedef std::atomic<u32> au32;
typedef std::atomic<u64> au64;
#else
typedef u32 au32;
typedef u64 au64;
#endif

// algorithm/performance parameters; assume EDGEBITS < 31

const u32 NODEBITS = EDGEBITS + 1;
const word_t NODEMASK = (EDGEMASK << 1) | (word_t)1;

#ifndef PART_BITS
// #bits used to partition edge set processing to save memory
// a value of 0 does no partitioning and is fastest
// a value of 1 partitions in two, making twice_set the
// same size as shrinkingset at about 33% slowdown
// higher values are not that interesting
#define PART_BITS 0
#endif

#ifndef NPREFETCH
// how many prefetches to queue up
// before accessing the memory
// must be a multiple of NSIPHASH
#define NPREFETCH 1
#endif

#ifndef IDXSHIFT
// minimum shift that allows cycle finding data to fit in node bitmap space
// allowing them to share the same memory
// rbshi: do not compress so much for small edges
#define IDXSHIFT (PART_BITS + 8)
#endif
#define MAXEDGES (NEDGES >> IDXSHIFT)

const u32 PART_MASK = (1 << PART_BITS) - 1;
const u32 NONPART_BITS = EDGEBITS - PART_BITS;
const word_t NONPART_MASK = ((word_t)1 << NONPART_BITS) - 1;

// set that starts out full and gets reset by threads on disjoint words
class shrinkingset {
public:
  bitmap<u64> bmap;
  u64 *cnt;
  u32 nthreads;

  shrinkingset(const u32 nt) : bmap(NEDGES) {
    cnt  = new u64[nt];
    nthreads = nt;
  }
  ~shrinkingset() {
    delete[] cnt;
  }
  void clear() {
    bmap.clear();
    memset(cnt, 0, nthreads * sizeof(u64));
    cnt[0] = NEDGES;
  }
  u64 count() const {
    u64 sum = 0LL;
    for (u32 i=0; i<nthreads; i++)
      sum += cnt[i];
    return sum;
  }
  void reset(word_t n, u32 thread) {
    bmap.set(n);
    cnt[thread]--;
  }
  bool test(word_t n) const {
    return !bmap.test(n);
  }
  u64 block(word_t n) const {
    return ~bmap.block(n);
  }
};

class cuckoo_ctx {
public:
  siphash_keys sip_keys;
  shrinkingset alive;
  bitmap<word_t> nonleaf;
  graph<word_t> cg;
  u32 nonce;
  proof *sols;
  u32 nsols;
  u32 nthreads;
  u32 ntrims;
  bool mutatenonce;
  trim_barrier barry;

  cuckoo_ctx(u32 n_threads, u32 n_trims, u32 max_sols, bool mutate_nonce) : alive(n_threads), nonleaf(NEDGES >> PART_BITS),
//      cg(MAXEDGES, MAXEDGES, max_sols, IDXSHIFT, (char *)nonleaf.bits), barry(n_threads) {
      cg(MAXEDGES, MAXEDGES, max_sols, IDXSHIFT), barry(n_threads) {
    print_log("cg.bytes %llu NEDGES/8 %llu\n", cg.bytes(), NEDGES/8);
    // rbshi
    // u32 tmp1 = cg.bytes();
    // assert(cg.bytes() <= NEDGES/8); // check that graph cg can fit in share nonleaf's memory
    nthreads = n_threads;
    ntrims = n_trims;
    sols = new proof[max_sols];
    nsols = 0;
    mutatenonce = mutate_nonce;
  }
  void setheadernonce(char* headernonce, const u32 len, const u32 nce) {
    nonce = nce;
    if (mutatenonce) {
      ((u32 *)headernonce)[len/sizeof(u32)-1] = htole32(nonce); // place nonce at end
    }
    setheader(headernonce, len, &sip_keys);
    alive.clear(); // set all edges to be alive
    nsols = 0;
  }
  ~cuckoo_ctx() {
    delete[] sols;
  }
  void barrier() {
    barry.wait();
  }
  void abort() {
    barry.abort();
  }
  void prefetch(const u64 *hashes, const u32 part) const {
    for (u32 i=0; i < NSIPHASH; i++) {
      u64 u = hashes[i] & EDGEMASK;
      if ((u >> NONPART_BITS) == part) {
        nonleaf.prefetch(u & NONPART_MASK);
      }
    }
  }
  void node_deg(const u64 *hashes, const u32 nsiphash, const u32 part) {
    for (u32 i=0; i < nsiphash; i++) {
      u64 u = hashes[i] & EDGEMASK;
//      if ((u >> NONPART_BITS) == part) {
      nonleaf.set(u & NONPART_MASK);
//      }
    }
  }
  void kill(const u64 *hashes, const u64 *indices, const u32 nsiphash, const u32 part, const u32 id) {
    for (u32 i=0; i < nsiphash; i++) {
      u64 u = hashes[i] & EDGEMASK;
//      if ((u >> NONPART_BITS) == part && !nonleaf.test((u & NONPART_MASK) ^ 1)) {
        if (!nonleaf.test((u & NONPART_MASK) ^ 1)) {
            alive.reset(indices[i]/2, id);
      }
    }
  }
  void count_node_deg(const u32 id, const u32 uorv, const u32 part) {
    alignas(64) u64 indices[NSIPHASH];
    alignas(64) u64 hashes[NPREFETCH];

    int sum_edge = 0;
  
    memset(hashes, 0, NPREFETCH * sizeof(u64)); // allow many nonleaf.set(0) to reduce branching
    u32 nidx = 0;
    for (word_t block = id*64; block < NEDGES; block += nthreads*64) {
      u64 alive64 = alive.block(block);
      for (word_t nonce = block-1; alive64; ) { // -1 compensates for 1-based ffs
        u32 ffs = __builtin_ffsll(alive64);
        nonce += ffs; alive64 >>= ffs;
        sum_edge++;

//        indices[nidx++ % NSIPHASH] = 2*nonce + uorv;
//        if (nidx % NSIPHASH == 0) {
//          node_deg(hashes+nidx-NSIPHASH, NSIPHASH, part);
//          siphash24xN(&sip_keys, indices, hashes+nidx-NSIPHASH);
//          // prefetch(hashes+nidx-NSIPHASH, part);
//          nidx %= NPREFETCH;
//        }

          indices[0] = 2*nonce + uorv;
          node_deg(hashes, NSIPHASH, part);
          siphash24xN(&sip_keys, indices, hashes);
          // prefetch(hashes+nidx-NSIPHASH, part);
          nidx = 0;

        if (ffs & 64) break; // can't shift by 64
      }
    }
    node_deg(hashes, NPREFETCH, part);
//    if (nidx % NSIPHASH != 0) {
//      siphash24xN(&sip_keys, indices, hashes+(nidx&-NSIPHASH));
//      node_deg(hashes+(nidx&-NSIPHASH), nidx%NSIPHASH, part);
//    }

//    printf("sumedge=%d\n", sum_edge);

  }
  void kill_leaf_edges(const u32 id, const u32 uorv, const u32 part) {
    alignas(64) u64 indices[NPREFETCH];
    alignas(64) u64 hashes[NPREFETCH];
  
    for (int i=0; i < NPREFETCH; i++)
      hashes[i] = 1; // allow many nonleaf.test(0) to reduce branching
    u32 nidx = 0;
    for (word_t block = id*64; block < NEDGES; block += nthreads*64) {
      u64 alive64 = alive.block(block);
      for (word_t nonce = block-1; alive64; ) { // -1 compensates for 1-based ffs
        u32 ffs = __builtin_ffsll(alive64);
        nonce += ffs; alive64 >>= ffs;

//        indices[nidx++] = 2*nonce + uorv;
//        if (nidx % NSIPHASH == 0) {
//          siphash24xN(&sip_keys, indices+nidx-NSIPHASH, hashes+nidx-NSIPHASH);
//          // prefetch(hashes+nidx-NSIPHASH, part);
//          nidx %= NPREFETCH;
//          kill(hashes+nidx, indices+nidx, NSIPHASH, part, id);
//        }


        indices[0] = 2*nonce + uorv;
        siphash24xN(&sip_keys, indices, hashes);
        // prefetch(hashes+nidx-NSIPHASH, part);
        nidx = 0;
        kill(hashes, indices, NSIPHASH, part, id);


        if (ffs & 64) break; // can't shift by 64
      }
    }
    const u32 pnsip = nidx & -NSIPHASH;
    if (pnsip != nidx) {
      siphash24xN(&sip_keys, indices+pnsip, hashes+pnsip);
    }
    kill(hashes, indices, nidx, part, id);
    const u32 nnsip = pnsip + NSIPHASH;
    kill(hashes+nnsip, indices+nnsip, NPREFETCH-nnsip, part, id);
  }
};

typedef struct {
  u32 id;
  pthread_t thread;
  cuckoo_ctx *ctx;
} thread_ctx;

void worker(thread_ctx *tp) {
  // thread_ctx *tp = (thread_ctx *)vp;
  cuckoo_ctx *ctx = tp->ctx;

  shrinkingset &alive = ctx->alive;
  // if (tp->id == 0) print_log("initial size %d\n", NEDGES);
  u32 round;
  for (round=0; round < ctx->ntrims; round++) {
    // if (tp->id == 0) print_log("round %2d partition sizes", round);
    for (u32 part = 0; part <= PART_MASK; part++) {
      if (tp->id == 0)
      ctx->nonleaf.clear(); // clear all counts
      // ctx->barrier();
      ctx->count_node_deg(tp->id,round&1,part);
      // ctx->barrier();
      ctx->kill_leaf_edges(tp->id,round&1,part);

      // if (tp->id == 0) print_log(" %c%d %d", "UV"[round&1], part, alive.count());
      // ctx->barrier();
    }
    // if (tp->id == 0) print_log("\n");
  }
  if (tp->id != 0)
//    pthread_exit(NULL);
  print_log("%d trims completed  %d edges left\n", round-1, alive.count());


  ctx->cg.reset();
  for (word_t block = 0; block < NEDGES; block += 64) {
    u64 alive64 = alive.block(block);
    for (word_t nonce = block-1; alive64; ) { // -1 compensates for 1-based ffs
      u32 ffs = __builtin_ffsll(alive64);
      nonce += ffs; alive64 >>= ffs;
      word_t u=sipnode(&ctx->sip_keys, nonce, 0), v=sipnode(&ctx->sip_keys, nonce, 1);
      ctx->cg.add_compress_edge(u, v);
      if (ffs & 64) break; // can't shift by 64
    }
  }
  for (u32 s=0; s < ctx->cg.nsols; s++) {
    u32 j = 0, nalive = 0;
    for (word_t block = 0; block < NEDGES; block += 64) {
      u64 alive64 = alive.block(block);
      for (word_t nonce = block-1; alive64; ) { // -1 compensates for 1-based ffs
        u32 ffs = __builtin_ffsll(alive64);
        nonce += ffs; alive64 >>= ffs;
        if (j < PROOFSIZE && nalive++ == ctx->cg.sols[s][j]) {
          ctx->sols[s][j++] = nonce;
        }
        if (ffs & 64) break; // can't shift by 64
      }
    }
    assert (j == PROOFSIZE);
  }
  ctx->nsols = ctx->cg.nsols;
//  pthread_exit(NULL);
}
