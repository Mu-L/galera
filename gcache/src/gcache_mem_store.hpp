/*
 * Copyright (C) 2010-2024 Codership Oy <info@codership.com>
 */

/*! @file mem store class */

#ifndef _gcache_mem_store_hpp_
#define _gcache_mem_store_hpp_

#include "gcache_memops.hpp"
#include "gcache_bh.hpp"
#include "gcache_types.hpp"
#include "gcache_limits.hpp"

#include <string>
#include <set>

namespace gcache
{
    class MemStore : public MemOps
    {
    public:

        MemStore (size_t const max_size, seqno2ptr_t& seqno2ptr, int const dbg)
            : max_size_ (max_size),
              size_     (0),
              allocd_   (),
              seqno2ptr_(seqno2ptr),
              seqno_locked_(SEQNO_MAX),
              debug_    (dbg & DEBUG)
        {}

        void reset ()
        {
            for (std::set<void*>::iterator buf(allocd_.begin());
                 buf != allocd_.end(); ++buf)
            {
                ::free (*buf);
            }

            allocd_.clear();
            size_ = 0;
        }

        ~MemStore () { reset(); }

        void* malloc  (size_type size)
        {
            Limits::assert_size(size);

            if (size > max_size_ || have_free_space(size) == false) return 0;

            assert (size_ + size <= max_size_);

            BufferHeader* bh (BH_cast (::malloc (size)));

            if (gu_likely(0 != bh))
            {
                allocd_.insert(bh);

                bh->size    = size;
                bh->seqno_g = SEQNO_NONE;
                bh->flags   = 0;
                bh->store   = BUFFER_IN_MEM;
                bh->ctx     = reinterpret_cast<BH_ctx_t>(this);

                size_ += size;

                return (bh + 1);
            }

            return 0;
        }

        void  free (BufferHeader* bh)
        {
            assert(bh->size > 0);
            assert(bh->size <= size_);
            assert(bh->store == BUFFER_IN_MEM);
            assert(bh->ctx == reinterpret_cast<BH_ctx_t>(this));

            if (SEQNO_NONE == bh->seqno_g) discard (bh);
        }

        void  repossess(BufferHeader* bh)
        {
            assert(bh->size > 0);
            assert(bh->seqno_g != SEQNO_NONE);
            assert(bh->store == BUFFER_IN_MEM);
            assert(bh->ctx == reinterpret_cast<BH_ctx_t>(this));
            assert(BH_is_released(bh)); // will be marked unreleased by caller
        }

        void* realloc (void* ptr, size_type size)
        {
            if (!ptr) return malloc(size);

            BufferHeader* bh(ptr2BH(ptr));
            assert (SEQNO_NONE == bh->seqno_g);

            if (!size)
            {
                free(bh);
                return nullptr;
            }

            uintptr_t const orig(reinterpret_cast<uintptr_t>(bh));
            size_type const old_size(bh->size);
            diff_type const diff_size(size - old_size);

            if (size > max_size_ ||
                have_free_space(diff_size) == false) return 0;

            assert (size_ + diff_size <= max_size_);

            allocd_.erase(bh);
            void* tmp = ::realloc (bh, size);

            if (tmp)
            {
                allocd_.insert(tmp);

                bh = BH_cast(tmp);
                assert (bh->size == old_size);
                bh->size  = size;

                size_ += diff_size;

                return (bh + 1);
            }
            else
            {
                assert(size > 0);
                /* orginal buffer is still allocated so we need to restore it
                 * but we can't use bh directly due to GCC warnings */
                allocd_.insert(reinterpret_cast<BufferHeader*>(orig));
            }

            return 0;
        }

        void discard (BufferHeader* bh)
        {
            assert (BH_is_released(bh));
            assert (bh->seqno_g < seqno_locked_);

            size_ -= bh->size;
            allocd_.erase(bh);
            ::free (bh);
        }

        void set_max_size (size_t size) { max_size_ = size; }

        void seqno_reset();

        // for unit tests only
        size_t _allocd () const { return size_; }

        void set_debug(int const dbg) { debug_ = dbg & DEBUG; }

        void seqno_lock(seqno_t const seqno_g) { seqno_locked_ = seqno_g; }

        void seqno_unlock() { seqno_locked_ = SEQNO_MAX; }

    private:

        static int const DEBUG = 1;

        bool have_free_space (size_type size);

        size_t          max_size_;
        size_t          size_;
        std::set<void*> allocd_;
        seqno2ptr_t&    seqno2ptr_;
        seqno_t         seqno_locked_;
        int             debug_;
    };
}

#endif /* _gcache_mem_store_hpp_ */

