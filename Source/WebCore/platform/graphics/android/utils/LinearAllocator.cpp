/*
 * Copyright 2012, The Android Open Source Project
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#define LOG_TAG "LinearAllocator"
#define LOG_NDEBUG 1

#include "config.h"
#include "LinearAllocator.h"

#include "AndroidLog.h"

namespace WebCore {

// The ideal size of a page allocation
#define TARGET_PAGE_SIZE 16384 // 16kb

// our pool needs to big enough to hold at least this many items
#define MIN_OBJECT_COUNT 4

#if LOG_NDEBUG
#define ADD_ALLOCATION(size)
#define RM_ALLOCATION(size)
#else
#include <utils/Thread.h>
static size_t s_totalAllocations = 0;
static double s_lastLogged = 0;
static android::Mutex s_mutex;

static void _logUsageLocked() {
    double now = currentTimeMS();
    if (now - s_lastLogged > 5) {
        s_lastLogged = now;
        ALOGV("Total memory usage: %d kb", s_totalAllocations / 1024);
    }
}

static void _addAllocation(size_t size) {
    android::AutoMutex lock(s_mutex);
    s_totalAllocations += size;
    _logUsageLocked();
}

#define ADD_ALLOCATION(size) _addAllocation(size);
#define RM_ALLOCATION(size) _addAllocation(-size);
#endif

class LinearAllocator::Page {
public:
    Page* next() { return m_nextPage; }
    void setNext(Page* next) { m_nextPage = next; }

    Page()
        : m_nextPage(0)
    {}

    void* start()
    {
        return (void*) (((unsigned)this) + sizeof(LinearAllocator::Page));
    }

    void* end(int pageSize)
    {
        return (void*) (((unsigned)start()) + pageSize);
    }

private:
    Page(const Page& other) {}
    Page* m_nextPage;
};

LinearAllocator::LinearAllocator(size_t averageAllocSize)
    : m_next(0)
    , m_currentPage(0)
    , m_pages(0)
{
    if (averageAllocSize) {
        int usable_page_size = TARGET_PAGE_SIZE - sizeof(LinearAllocator::Page);
        int pcount = usable_page_size / averageAllocSize;
        if (pcount < MIN_OBJECT_COUNT)
            pcount = MIN_OBJECT_COUNT;
        m_pageSize = pcount * averageAllocSize + sizeof(LinearAllocator::Page);
    } else
        m_pageSize = TARGET_PAGE_SIZE;
    m_maxAllocSize = (m_pageSize - sizeof(LinearAllocator::Page));
}

LinearAllocator::~LinearAllocator(void)
{
    Page* p = m_pages;
    while (p) {
        Page* next = p->next();
        delete p;
        RM_ALLOCATION(m_pageSize);
        p = next;
    }
}

void* LinearAllocator::start(Page* p)
{
    return ((char*)p) + sizeof(Page);
}

void* LinearAllocator::end(Page* p)
{
    return ((char*)p) + m_pageSize;
}

void LinearAllocator::ensureNext(size_t size)
{
    if (m_next && ((char*)m_next + size) <= end(m_currentPage))
        return;
    Page* p = newPage();
    if (m_currentPage)
        m_currentPage->setNext(p);
    m_currentPage = p;
    if (!m_pages)
        m_pages = m_currentPage;
    m_next = start(m_currentPage);
}

unsigned LinearAllocator::memusage()
{
    unsigned memusage = 0;
    Page* p = m_pages;
    while (p) {
        memusage += m_pageSize;
        p = p->next();
    }
    return memusage;
}

void* LinearAllocator::alloc(size_t size)
{
    if (size > m_maxAllocSize) {
        ALOGE("Allocation too large! (%d exceeds max size %d)", size, m_maxAllocSize);
        return 0;
    }
    ensureNext(size);
    void* ptr = m_next;
    m_next = ((char*)m_next) + size;
    return ptr;
}

void LinearAllocator::rewindTo(void* ptr)
{
    // Don't bother rewinding across pages
    if (ptr >= start(m_currentPage) && ptr < end(m_currentPage))
        m_next = ptr;
}

LinearAllocator::Page* LinearAllocator::newPage()
{
    ADD_ALLOCATION(m_pageSize);
    void* buf = malloc(m_pageSize);
    return new (buf) Page();
}

} // namespace WebCore