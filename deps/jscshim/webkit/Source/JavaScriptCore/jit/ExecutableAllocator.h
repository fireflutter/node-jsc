/*
 * Copyright (C) 2008-2018 Apple Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. 
 */

#pragma once

#include "JITCompilationEffort.h"
#include "JSCPtrTag.h"
#include <stddef.h> // for ptrdiff_t
#include <limits>
#include <wtf/Assertions.h>
#include <wtf/Lock.h>
#include <wtf/MetaAllocatorHandle.h>
#include <wtf/MetaAllocator.h>

#if OS(IOS)
#include <libkern/OSCacheControl.h>
#endif

#if OS(IOS)
#include <sys/mman.h>
#endif

#if CPU(MIPS) && OS(LINUX)
#include <sys/cachectl.h>
#endif

#if ENABLE(FAST_JIT_PERMISSIONS)
#include <os/thread_self_restrict.h> 
#endif
#define JIT_ALLOCATOR_LARGE_ALLOC_SIZE (pageSize() * 4)

#define EXECUTABLE_POOL_WRITABLE true

namespace JSC {

static const unsigned jitAllocationGranule = 32;

typedef WTF::MetaAllocatorHandle ExecutableMemoryHandle;

#if ENABLE(ASSEMBLER)

extern JS_EXPORT_PRIVATE void* taggedStartOfFixedExecutableMemoryPool;
extern JS_EXPORT_PRIVATE void* taggedEndOfFixedExecutableMemoryPool;

template<typename T = void*>
T startOfFixedExecutableMemoryPool()
{
    return untagCodePtr<T, ExecutableMemoryPtrTag>(taggedStartOfFixedExecutableMemoryPool);
}

template<typename T = void*>
T endOfFixedExecutableMemoryPool()
{
    return untagCodePtr<T, ExecutableMemoryPtrTag>(taggedEndOfFixedExecutableMemoryPool);
}

inline bool isJITPC(void* pc)
{
    return startOfFixedExecutableMemoryPool() <= pc && pc < endOfFixedExecutableMemoryPool();
}

typedef void (*JITWriteSeparateHeapsFunction)(off_t, const void*, size_t);
extern JS_EXPORT_PRIVATE JITWriteSeparateHeapsFunction jitWriteSeparateHeapsFunction;

extern JS_EXPORT_PRIVATE bool useFastPermisionsJITCopy;

static inline void* performJITMemcpy(void *dst, const void *src, size_t n)
{
    if (dst >= startOfFixedExecutableMemoryPool() && dst < endOfFixedExecutableMemoryPool()) {
#if ENABLE(FAST_JIT_PERMISSIONS)
        if (useFastPermisionsJITCopy) {
            os_thread_self_restrict_rwx_to_rw();
            memcpy(dst, src, n);
            os_thread_self_restrict_rwx_to_rx();
            return dst;
        }
#endif

        if (jitWriteSeparateHeapsFunction) {
            // Use execute-only write thunk for writes inside the JIT region. This is a variant of
            // memcpy that takes an offset into the JIT region as its destination (first) parameter.
            off_t offset = (off_t)((uintptr_t)dst - startOfFixedExecutableMemoryPool<uintptr_t>());
            retagCodePtr<JITThunkPtrTag, CFunctionPtrTag>(jitWriteSeparateHeapsFunction)(offset, src, n);
            return dst;
        }
    }

    // Use regular memcpy for writes outside the JIT region.
    return memcpy(dst, src, n);
}

class ExecutableAllocator {
    enum ProtectionSetting { Writable, Executable };

public:
    static ExecutableAllocator& singleton();
    static void initializeAllocator();

    bool isValid() const;

    static bool underMemoryPressure();
    
    static double memoryPressureMultiplier(size_t addedMemoryUsage);
    
#if ENABLE(META_ALLOCATOR_PROFILE)
    static void dumpProfile();
#else
    static void dumpProfile() { }
#endif

    RefPtr<ExecutableMemoryHandle> allocate(size_t sizeInBytes, void* ownerUID, JITCompilationEffort);

    bool isValidExecutableMemory(const AbstractLocker&, void* address);

    static size_t committedByteCount();

    Lock& getLock() const;
private:

    ExecutableAllocator();
    ~ExecutableAllocator();
};

#else
inline bool isJITPC(void*) { return false; }
#endif // ENABLE(ASSEMBLER)


} // namespace JSC
