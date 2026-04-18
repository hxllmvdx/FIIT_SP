#include <not_implemented.h>
#include "../include/allocator_global_heap.h"
#include <new>

[[nodiscard]] void *allocator_global_heap::do_allocate_sm(
    size_t size)
{
    std::lock_guard<std::mutex> lock(mtx);
    return operator new(size);
}

void allocator_global_heap::do_deallocate_sm(
    void *at)
{
    std::lock_guard<std::mutex> lock(mtx);
    operator delete(at);
}

bool allocator_global_heap::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    auto p = dynamic_cast<const allocator_global_heap*>(&other);
    return p != nullptr;
}
