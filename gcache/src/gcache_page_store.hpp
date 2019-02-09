/*
 * Copyright (C) 2010-2019 Codership Oy <info@codership.com>
 */

/*! @file page store class */

#ifndef _gcache_page_store_hpp_
#define _gcache_page_store_hpp_

#include "gcache_memops.hpp"
#include "gcache_page.hpp"
#include "gcache_seqno.hpp"

#include <gu_macros.hpp> // GU_COMPILE_ASSERT

#include <string>
#include <deque>
#include <map>
#include <type_traits> // std::is_standard_layout
#include <cstddef> // offsetof

namespace gcache
{
    class PageStore : public MemOps
    {
    public:

        static int  const DEBUG = 4; // debug flag

        PageStore (const std::string& dir_name,
                   wsrep_encrypt_cb_t encrypt_cb,
                   void*              app_ctx,
                   size_t             keep_size,
                   size_t             page_size,
                   size_t             plaintext_size,
                   int                dbg,
                   bool               keep_page);

        ~PageStore ();

        static PageStore* page_store(const Page* p)
        {
            return static_cast<PageStore*>(p->parent());
        }

        /* This is just to satisfy the MemOps interface. Should not be called */
        void* malloc  (size_type size) { assert(0); return NULL; }
        void* malloc  (size_type size, void*& ptx);

        void* realloc (void* ptr, size_type size);

        void  free    (BufferHeader* bh, const void* ptr)
        {
            release<false>(bh, ptr);
        }
        void  free    (BufferHeader* bh) { free(bh, NULL); }

        void* get_plaintext(const void* ptr, bool writable = false);
        void  drop_plaintext(const void* ptr)
        {
            drop_plaintext(find_plaintext(ptr), ptr, false);
        }

        BufferHeader* get_BH(const void* const ptr, bool change = false)
        {
            assert(encrypt_cb_);
            Plain& p(find_plaintext(ptr)->second);
            p.changed_ = p.changed_ || change;
            return &(p.bh_);
        }

        void  repossess(BufferHeader* bh, const void* ptr);
        void  repossess(BufferHeader* bh) { assert(0); repossess(bh, NULL); }

        void  discard (BufferHeader* bh, const void* ptr)
        {
            release<true>(bh, ptr);
        }
        void  discard (BufferHeader* bh) { discard(bh, NULL); }

        bool  page_cleanup_needed() const { return total_size_ > keep_size_; }

        void  reset();

        void  set_enc_key(const Page::EncKey& key);

        void  set_page_size (size_t size) { page_size_ = size; }

        void  set_keep_size (size_t size) { keep_size_ = size; }

        void  set_plaintext_size (size_t size) { plaintext_size_ = size; }

        void  set_debug(int dbg);

        /* for unit tests */
        size_t count()       const { return count_;        }
        size_t total_pages() const { return pages_.size(); }
        size_t total_size()  const { return total_size_;   }

        void meta(const void* const ptr, std::ostream& os)
        {
            os << find_plaintext(ptr)->second;
        }

    private:

        struct Plain
        {
            Page*         page_;       /* page containing ciphertext */
            BufferHeader* ptx_;        /* corresponding plaintext buffer */
            BufferHeader  bh_;         /* plaintex copy of buffer header */
            size_type     alloc_size_; /* total allocated size */
            int           ref_count_;  /* reference counter */
            bool          changed_;    /* whether we need to flush it to disk */
            bool          freed_;      /* free() was called on the buffer */

            void print(std::ostream& os) const;

            friend std::ostream&
            operator << (std::ostream& os, const Plain& p)
            { p.print(os); return os; }
        };

        typedef std::pair<const void*, Plain> PlainMapEntry;

        std::string const base_name_; /* /.../.../gcache.page. */
        wsrep_encrypt_cb_t const encrypt_cb_;
        void* const       app_ctx_;   /* context for encryption callback */
        Page::EncKey      enc_key_;   /* current key */
        Page::Nonce       nonce_;     /* current nonce */
        size_t            keep_size_; /* how much pages to keep after freeing */
        size_t            page_size_; /* min size of the individual page */
        size_t            keep_plaintext_size_; /* max plaintext to keep */
        size_t            count_;     /* page counter to make unique file name */
        typedef std::deque<Page*> PageQueue;
        PageQueue         pages_;
        Page*             current_;
        size_t            total_size_;
        typedef std::map<const void*, Plain> PlainMap;
        PlainMap          enc2plain_;
        size_t            plaintext_size_; /* how much plaintext allocated */
        pthread_attr_t    delete_page_attr_;
#ifndef GCACHE_DETACH_THREAD
        pthread_t         delete_thr_;
#endif /* GCACHE_DETACH_THREAD */
        int               debug_;
        bool        const keep_page_; /* whether to keep the last page */

        void new_page    (size_type size, const Page::EncKey& k);

        // returns true if a page could be deleted
        bool delete_page ();

        // cleans up extra pages.
        void cleanup     ();

        void* malloc_new (size_type size);

        PlainMap::iterator find_plaintext(const void* ptr);

        /* shared functionality for public drop_palintext() and free() */
        void drop_plaintext(PlainMap::iterator i, const void* ptr, bool free);

        void discard_plaintext(PlainMap::iterator i)
        {
#ifndef NDEBUG
            Plain& p(i->second);
            assert(p.freed_);
            assert(0     == p.ref_count_);
            assert(false == p.changed_);
            assert(NULL  == p.ptx_);
#endif
            enc2plain_.erase(i);
        }

        template <bool discard> void
        release(BufferHeader* bh, const void* ptr)
        {
            assert(BH_is_released(bh));
            assert(ptr || !encrypt_cb_);

            Page* page(static_cast<Page*>(BH_ctx(bh)));

            if (discard)
            {
                page->discard(bh);
                if (encrypt_cb_) discard_plaintext(find_plaintext(ptr));
            }
            else
            {
                bool const dis(page->free(bh, ptr));

                if (encrypt_cb_)
                {
                    PlainMap::iterator const i(find_plaintext(ptr));
                    drop_plaintext(i, ptr, true);
                    if (dis) discard_plaintext(i);
                }
            }

            if (0 == page->used()) cleanup();
        }

        Plain& bh2Plain(BufferHeader* bh)
        {
            GU_COMPILE_ASSERT(std::is_standard_layout<Plain>::value,
                              Plain_is_not_standard_layout);
            return *(reinterpret_cast<Plain*>(reinterpret_cast<char*>(bh) -
                                              offsetof(Plain, bh_)));
        }

        void initialize_nonce();

        PageStore(const gcache::PageStore&);
        PageStore& operator=(const gcache::PageStore&);
    };
}

#endif /* _gcache_page_store_hpp_ */
