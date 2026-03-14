#include <not_implemented.h>
#include "../include/allocator_global_heap.h"
#include <new>

[[nodiscard]] void *allocator_global_heap::do_allocate_sm(
    size_t size)
{
    std::lock_guard<std::mutex> lock(mtx);
    return ::operator new(size);
}

void allocator_global_heap::do_deallocate_sm(
    void *at)
{
    std::lock_guard<std::mutex> lock(mtx);
    ::operator delete(at);
}

allocator_global_heap::allocator_global_heap(const allocator_global_heap &other) : mtx() {}

allocator_global_heap &allocator_global_heap::operator=(const allocator_global_heap &other)
{
    return *this;
}

bool allocator_global_heap::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    auto const* p = dynamic_cast<allocator_global_heap const*>(&other);
    return p != nullptr && p == this;
}

allocator_global_heap::allocator_global_heap(allocator_global_heap &&other) noexcept : mtx() {}

allocator_global_heap &allocator_global_heap::operator=(allocator_global_heap &&other) noexcept
{
    return *this;
}
