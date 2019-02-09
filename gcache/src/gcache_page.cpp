/*
 * Copyright (C) 2010-2019 Codership Oy <info@codership.com>
 */

/*! @file page file class implementation */

#include "gcache_page.hpp"
#include "gcache_limits.hpp"

#include <gu_throw.hpp>
#include <gu_logger.hpp>

// for posix_fadvise()
#if !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 600
#endif
#include <fcntl.h>

// for nonce initialization
#include <chrono>
#include <random>

gcache::Page::Nonce::Nonce() : d()
{
    std::random_device r;
    uint64_t const seed1(r());

    /* just in case random_device implementation happens to be too
     * determenistic, add a seed based on time. */
    uint64_t const seed2(std::chrono::high_resolution_clock::now()
                         .time_since_epoch().count());
    assert(seed2 != 0);

    std::seed_seq seeds{ seed1, seed2 };
    std::mt19937 rng(seeds);

    for (size_t i(0); i < (sizeof(d.i)/sizeof(d.i[0])); ++i)
    {
        d.i[i] = rng();
    }
}

/* how much to write to buffer given buffer size */
static inline size_t nonce_serial_size(size_t const buf_size)
{
    return std::min(gcache::Page::Nonce::size(), buf_size);
}

gcache::Page::Nonce::Nonce(const void* const ptr, size_t const size) : d()
{
    ::memcpy(&d, ptr, nonce_serial_size(size));
}

size_t
gcache::Page::Nonce::write(void* const ptr, size_t const size) const
{
    size_t const write_size(nonce_serial_size(size));
    ::memcpy(ptr, &d, write_size);
    return write_size;
}

void
gcache::Page::reset ()
{
    if (gu_unlikely (used_ > 0))
    {
        log_fatal << "Attempt to reset a page '" << name()
                  << "' used by " << used_ << " buffers. Aborting.";
        abort();
    }

    /* preserve the nonce */
    size_type const nonce_size(Page::aligned_size(nonce_.write(next_, space_)));
    space_ = mmap_.size - nonce_size;
    next_  = static_cast<uint8_t*>(mmap_.ptr) + nonce_size;
}

void
gcache::Page::drop_fs_cache() const
{
    mmap_.dont_need();

#if !defined(__APPLE__)
    int const err (posix_fadvise (fd_.get(), 0, fd_.size(),
                                  POSIX_FADV_DONTNEED));
    if (err != 0)
    {
        log_warn << "Failed to set POSIX_FADV_DONTNEED on " << fd_.name()
                 << ": " << err << " (" << strerror(err) << ")";
    }
#endif
}

gcache::Page::Page (void*              ps,
                    const std::string& name,
                    const EncKey&      key,
                    const Nonce&       nonce,
                    size_t size,
                    int dbg)
    :
    fd_   (name, aligned_size(size), false, false),
    mmap_ (fd_),
    key_  (key),
    nonce_(nonce),
    ps_   (ps),
    next_ (start()),
    space_(mmap_.size),
    used_ (0),
    debug_(dbg)
{
    size_type const nonce_size(Page::aligned_size(nonce_.write(next_, space_)));
    next_  += nonce_size;
    space_ -= nonce_size;

    log_info << "Created page " << name << " of size " << space_
             << " bytes";
}

void
gcache::Page::close()
{
    // write empty header to signify end of chain for subsequent recovery
    if (space_ >= sizeof(BufferHeader)) BH_clear(BH_cast(next_));
}

void*
gcache::Page::malloc (size_type size)
{
    Limits::assert_size(size);
    size_type const alloc_size(aligned_size(size));

    if (alloc_size <= space_)
    {
        void* ret = next_;
        space_ -= alloc_size;
        next_  += alloc_size;
        used_++;

#ifndef NDEBUG
        assert (next_ <= start() + mmap_.size);
        if (debug_)
        {
            log_info << name() << " allocd " << size << '/' << alloc_size;
            log_info << name() << " incremented ref count to " << used_;
        }
#endif
        return ret;
    }
    else
    {
        close(); // this page will not be used any more.
        log_debug << "Failed to allocate " << size << " bytes, space left: "
                  << space_ << " bytes, total allocated: "
                  << next_ - static_cast<uint8_t*>(mmap_.ptr);
        return 0;
    }
}

void*
gcache::Page::realloc (void* ptr, size_type size)
{
    assert(0); // all logic must go to PageStore.
    return NULL;
}

bool
gcache::Page::realloc (void*     const ptr,
                       size_type const old_size,
                       size_type const new_size)
{
    assert(uintptr_t(ptr) % ALIGNMENT == 0);

    uint8_t* p(static_cast<uint8_t*>(ptr));
    assert(p > start());
    assert(p < next_);

    if (p + old_size == next_)
    {
        /* last buffer can shrink/expand */
        diff_type const diff_size(new_size - old_size);
        assert(diff_size % ALIGNMENT == 0);

        if (diff_size < 0 || size_t(diff_size) < space_)
        {
            space_ -= diff_size;
            next_  += diff_size;
            return true;
        }
    }
    return false;
}

void
gcache::Page::xcrypt(wsrep_encrypt_cb_t    const encrypt_cb,
                     void*                 const app_ctx,
                     const void*           const from,
                     void*                 const to,
                     size_type             const size,
                     wsrep_enc_direction_t const dir)
{
    assert(encrypt_cb);

    size_t const offset(dir == WSREP_ENC ?
                        /* writing to page */
                        static_cast<uint8_t*>(to) - start() :
                        /* reading from page */
                        static_cast<const uint8_t*>(from) - start());
    Nonce const nonce(nonce_ + offset);
    wsrep_enc_key_t const enc_key = { key_.data(), key_.size() };
    wsrep_enc_ctx_t       enc_ctx = { &enc_key, nonce.iv(), NULL };
    wsrep_buf_t     const input   = { from, size };

    int const ret
        (encrypt_cb(app_ctx, &enc_ctx, &input, to, dir, true));

    if (gu_unlikely(ret != int(input.len)))
    {
        assert(0);
        gu_throw_fatal << "Encryption callback failed with return value " << ret
                       <<". Page: " << *this
                       << ", offset: " << offset << ", size: " << size
                       << ", direction: " << dir;
    }
}


void gcache::Page::print(std::ostream& os) const
{
    os << "page file: " << name() << ", size: " << size() << ", used: "
       << used_;

    if (used_ > 0 && debug_ > 0)
    {
        bool was_released(true);
        const uint8_t* const start(static_cast<uint8_t*>(mmap_.ptr));
        const uint8_t* p(start);
        assert(p != next_);
        while (p != next_)
        {
            ptrdiff_t const offset(p - start);
            const BufferHeader* const bh(BH_const_cast(p));
            p += bh->size;
            if (!BH_is_released(bh))
            {
                os << "\noff: " << offset << ", " << bh;
                was_released = false;
            }
            else
            {
                if (!was_released && p != next_)
                {
                    os << "\n..."; /* indicate gap */
                }
                was_released = true;
            }
        }
    }
}
