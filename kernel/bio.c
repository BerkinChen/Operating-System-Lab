// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NBUCKET 13
#define HASH(i) ((i) % NBUCKET)

struct {
  struct spinlock lock;
  struct buf buf[NBUF];
} bcache;

struct {
  struct buf head[NBUCKET];
  struct spinlock lock[NBUCKET];
} map;

void binit(void) {
  struct buf *b;
  char name[20];

  initlock(&bcache.lock, "bcache");

  for (int i = 0; i < NBUCKET; ++i) {
    snprintf(name, sizeof(name), "bcache: bucket_%d", i);
    initlock(&map.lock[i], name);
  }
  b = bcache.buf;
  for (int i = 0;; i = (i + 1) % NBUCKET) {
    for (int j = 0; j < (NBUF / NBUCKET); ++j) {
      if (b >= bcache.buf + NBUF) {
        return;
      }
      initsleeplock(&b->lock, "buffer");
      b->blockno = i;
      b->next = map.head[i].next;
      map.head[i].next = b++;
    }
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf *bget(uint dev, uint blockno) {
  struct buf *b;

  uint h = HASH(blockno);
  acquire(&map.lock[h]);

  // Is the block already cached?
  for (b = map.head[h].next; 0 != b; b = b->next) {
    if (b->dev == dev && b->blockno == blockno) {
      b->refcnt++;
      release(&map.lock[h]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  uint min = 0xffffffff;
  struct buf *empty = 0;
  for (b = map.head[h].next; 0 != b; b = b->next) {
    if (b->refcnt == 0 && b->timestamp < min) {
      empty = b;
      min = b->timestamp;
    }
  }
  if (empty) {
    goto EVICT;
  }

  // Now try to find an unused buffer in other buckets.
  acquire(&bcache.lock);
  int i;
  for (i = 0; i < NBUCKET; ++i) {
    if (i == h) {
      continue;
    }
    acquire(&map.lock[i]);
    for (b = map.head[i].next; 0 != b; b = b->next) {
      if (b->refcnt == 0) {
        empty = b;
        goto STEAL;
      }
    }
    release(&map.lock[i]);
  }
  panic("bget: no buffers");

STEAL:
  for (b = &map.head[i]; empty != b->next; b = b->next)
    ;                     // Locate the previous buffer of `b` in bucket `i`.
  b->next = empty->next;  // Remove `empty` from bucket `i`.
  empty->next = map.head[h].next;  // Insert `empty` into bucket `h`;
  map.head[h].next = empty;
  release(&map.lock[i]);
  release(&bcache.lock);

EVICT:
  empty->dev = dev;
  empty->blockno = blockno;
  empty->valid = 0;
  empty->refcnt = 1;
  release(&map.lock[h]);
  acquiresleep(&empty->lock);
  return empty;
}

// Return a locked buf with the contents of the indicated block.
struct buf *bread(uint dev, uint blockno) {
  struct buf *b;

  b = bget(dev, blockno);
  if (!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b) {
  if (!holdingsleep(&b->lock)) panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
void brelse(struct buf *b) {
  if (!holdingsleep(&b->lock)) panic("brelse");

  uint h = HASH(b->blockno);

  releasesleep(&b->lock);

  acquire(&map.lock[h]);

  if (0 == --b->refcnt) {
    acquire(&tickslock);
    b->timestamp = ticks;
    release(&tickslock);
  }

  release(&map.lock[h]);
}

void bpin(struct buf *b) {
  uint h = HASH(b->blockno);
  acquire(&map.lock[h]);
  b->refcnt++;
  release(&map.lock[h]);
}

void bunpin(struct buf *b) {
  uint h = HASH(b->blockno);
  acquire(&map.lock[h]);
  b->refcnt--;
  release(&map.lock[h]);
}