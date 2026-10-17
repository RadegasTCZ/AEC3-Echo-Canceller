#include "AllocCounter.h"
#include <cstdlib>
#include <malloc.h>
#include <new>

namespace
{
    // Constant-initialised thread_locals: no dynamic TLS init, safe to touch from operator new.
    thread_local int armDepth = 0;
    thread_local long long allocationCount = 0;

    inline void noteAllocation() noexcept
    {
        if (armDepth > 0)
            ++allocationCount;
    }

    inline void* allocPlain (std::size_t n) noexcept
    {
        return std::malloc (n == 0 ? 1 : n);
    }

    inline void* allocAligned (std::size_t n, std::align_val_t al) noexcept
    {
        return _aligned_malloc (n == 0 ? 1 : n, static_cast<std::size_t> (al));
    }
}

namespace aec::test
{
    ScopedAllocCheck::ScopedAllocCheck() noexcept
        : start (allocationCount)
    {
        ++armDepth;
    }

    ScopedAllocCheck::~ScopedAllocCheck()
    {
        --armDepth;
    }

    int ScopedAllocCheck::count() const noexcept
    {
        return static_cast<int> (allocationCount - start);
    }
}

//==============================================================================
// Replacement global allocation functions (all C++17 forms MSVC can call).

void* operator new (std::size_t n)
{
    noteAllocation();
    if (void* p = allocPlain (n))
        return p;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t n)
{
    noteAllocation();
    if (void* p = allocPlain (n))
        return p;
    throw std::bad_alloc();
}

void* operator new (std::size_t n, const std::nothrow_t&) noexcept
{
    noteAllocation();
    return allocPlain (n);
}

void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept
{
    noteAllocation();
    return allocPlain (n);
}

void* operator new (std::size_t n, std::align_val_t al)
{
    noteAllocation();
    if (void* p = allocAligned (n, al))
        return p;
    throw std::bad_alloc();
}

void* operator new[] (std::size_t n, std::align_val_t al)
{
    noteAllocation();
    if (void* p = allocAligned (n, al))
        return p;
    throw std::bad_alloc();
}

void* operator new (std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept
{
    noteAllocation();
    return allocAligned (n, al);
}

void* operator new[] (std::size_t n, std::align_val_t al, const std::nothrow_t&) noexcept
{
    noteAllocation();
    return allocAligned (n, al);
}

void operator delete (void* p) noexcept                                             { std::free (p); }
void operator delete[] (void* p) noexcept                                           { std::free (p); }
void operator delete (void* p, std::size_t) noexcept                                { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept                              { std::free (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept                      { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept                    { std::free (p); }

void operator delete (void* p, std::align_val_t) noexcept                           { _aligned_free (p); }
void operator delete[] (void* p, std::align_val_t) noexcept                         { _aligned_free (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept              { _aligned_free (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept            { _aligned_free (p); }
void operator delete (void* p, std::align_val_t, const std::nothrow_t&) noexcept    { _aligned_free (p); }
void operator delete[] (void* p, std::align_val_t, const std::nothrow_t&) noexcept  { _aligned_free (p); }
