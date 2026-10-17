#pragma once

namespace aec::test
{
    /** Counts calls to the global operator new / new[] (every overload, including aligned and
        nothrow) made on the constructing thread while this object is alive. Allocations on other
        threads are never counted. Scopes may nest; each reports its own count.

        Usage: read count() INSIDE the scope, call expect*() AFTER the scope closes
        (expect*() builds juce::Strings and therefore allocates).

        Only valid in the test app: AllocCounter.cpp replaces the global allocation operators. */
    struct ScopedAllocCheck
    {
        ScopedAllocCheck() noexcept;
        ~ScopedAllocCheck();

        int count() const noexcept;

        ScopedAllocCheck (const ScopedAllocCheck&) = delete;
        ScopedAllocCheck& operator= (const ScopedAllocCheck&) = delete;

    private:
        long long start = 0;
    };
}
