#include <cstddef>
#include <mutex>
#include <new>
#include <not_implemented.h>
#include "../include/allocator_sorted_list.h"
#include "allocator_test_utils.h"
#include "allocator_with_fit_mode.h"

auto allocator_sorted_list::get_parent_allocator() {
    return reinterpret_cast<std::pmr::memory_resource**>(_trusted_memory);
}
auto allocator_sorted_list::get_fit_mode() {
    return reinterpret_cast<allocator_with_fit_mode::fit_mode*>(reinterpret_cast<char*>(_trusted_memory) + sizeof(std::pmr::memory_resource*));
}
auto allocator_sorted_list::get_total_size() {
    return reinterpret_cast<size_t*>(reinterpret_cast<char*>(_trusted_memory) + sizeof(std::pmr::memory_resource*) + sizeof(fit_mode));
}
auto allocator_sorted_list::get_mutex() {
    return reinterpret_cast<std::mutex*>(reinterpret_cast<char*>(_trusted_memory) + sizeof(std::pmr::memory_resource*) + sizeof(fit_mode) + sizeof(size_t));
}
auto allocator_sorted_list::get_free_head() {
    return reinterpret_cast<void**>(reinterpret_cast<char*>(_trusted_memory) + sizeof(std::pmr::memory_resource*) + sizeof(fit_mode) + sizeof(size_t) + sizeof(std::mutex));
}

void allocator_sorted_list::insert_free_block(void *new_block) {
    void *current = *get_free_head();
    void *prev = nullptr;

    while (current != nullptr && current < new_block)
    {
        prev = current;
        current = *reinterpret_cast<void**>(static_cast<char*>(current) + sizeof(size_t));
    }

    void **new_next_ptr = reinterpret_cast<void**>(static_cast<char*>(new_block) + sizeof(size_t));
    *new_next_ptr = current;

    if (prev == nullptr)
    {
        *get_free_head() = new_block;
    }
    else
    {
        void **prev_next_ptr = reinterpret_cast<void**>(static_cast<char*>(prev) + sizeof(size_t));
        *prev_next_ptr = new_block;
    }
}

allocator_sorted_list::~allocator_sorted_list()
{
    if (_trusted_memory) {
        get_mutex()->~mutex();

    auto parent = *get_parent_allocator();
    if (parent != nullptr) {
        parent->deallocate(_trusted_memory, *get_total_size());
    } else {
        ::operator delete(_trusted_memory);
    }
    }
}

allocator_sorted_list::allocator_sorted_list(
        size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    if (parent_allocator != nullptr)
    {
        _trusted_memory = parent_allocator->allocate(space_size);
    } else {
        _trusted_memory = ::operator new(space_size);
    }

    *get_parent_allocator() = parent_allocator;
    *get_fit_mode() = allocate_fit_mode;
    *get_total_size() = space_size;
    new (get_mutex()) std::mutex();

    auto block_start = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
    size_t size = space_size - allocator_metadata_size;

    size_t* size_ptr = reinterpret_cast<size_t*>(block_start);
    *size_ptr = size & ~size_t(1);

    void** next_ptr = reinterpret_cast<void**>(block_start + sizeof(size_t));
    *next_ptr = nullptr;

    *get_free_head() = block_start;
}

[[nodiscard]] void *allocator_sorted_list::do_allocate_sm(size_t size)
{
    std::lock_guard<std::mutex> lock(*get_mutex());

    size_t needed = size + sizeof(size_t);

    void *found_block = nullptr;
    void *found_prev = nullptr;
    fit_mode mode = *get_fit_mode();

    if (mode == fit_mode::first_fit)
    {
        void *block = *get_free_head();
        void *prev = nullptr;
        while (block != nullptr)
        {
            size_t block_size = *reinterpret_cast<size_t*>(block) & ~size_t(1);
            if (block_size >= needed)
            {
                found_block = block;
                found_prev = prev;
                break;
            }
            prev = block;

            block = *reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(size_t));
        }
    }
    else if (mode == fit_mode::the_best_fit)
    {
        void *block = *get_free_head();
        void *prev = nullptr;
        void *best_block = nullptr;
        void *best_prev = nullptr;
        size_t best_size = SIZE_MAX;

        while (block != nullptr)
        {
            size_t block_size = *reinterpret_cast<size_t*>(block) & ~size_t(1);
            if (block_size >= needed && block_size < best_size)
            {
                best_block = block;
                best_prev = prev;
                best_size = block_size;
            }
            prev = block;
            block = *reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(size_t));
        }
        found_block = best_block;
        found_prev = best_prev;
    }
    else if (mode == fit_mode::the_worst_fit)
    {
        void *block = *get_free_head();
        void *prev = nullptr;
        void *worst_block = nullptr;
        void *worst_prev = nullptr;
        size_t worst_size = 0;

        while (block != nullptr)
        {
            size_t block_size = *reinterpret_cast<size_t*>(block) & ~size_t(1);
            if (block_size >= needed && block_size > worst_size)
            {
                worst_block = block;
                worst_prev = prev;
                worst_size = block_size;
            }
            prev = block;
            block = *reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(size_t));
        }
        found_block = worst_block;
        found_prev = worst_prev;
    }

    if (found_block == nullptr)
        throw std::bad_alloc();

    size_t block_size = *reinterpret_cast<size_t*>(found_block) & ~size_t(1);
    size_t remaining = block_size - needed;

    void *next_block = *reinterpret_cast<void**>(static_cast<char*>(found_block) + sizeof(size_t));
    if (found_prev == nullptr)
    {
        *get_free_head() = next_block;
    }
    else
    {
        void **prev_next_ptr = reinterpret_cast<void**>(static_cast<char*>(found_prev) + sizeof(size_t));
        *prev_next_ptr = next_block;
    }

    if (remaining >= block_metadata_size)
    {
        void *new_free = static_cast<char*>(found_block) + needed;

        *reinterpret_cast<size_t*>(new_free) = remaining;

        *reinterpret_cast<void**>(static_cast<char*>(new_free) + sizeof(size_t)) = nullptr;

        insert_free_block(new_free);
    }

    size_t allocated_size = (remaining >= block_metadata_size) ? needed : block_size;
    *reinterpret_cast<size_t*>(found_block) = allocated_size | size_t(1);

    return static_cast<char*>(found_block) + sizeof(size_t);
}

bool allocator_sorted_list::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    auto p = dynamic_cast<const allocator_sorted_list*>(&other);
    return p != nullptr && p->_trusted_memory == _trusted_memory;
}

void allocator_sorted_list::do_deallocate_sm(void *at)
{
    std::lock_guard<std::mutex> lock(*get_mutex());

    if (at == nullptr) return;

    char *block_start = static_cast<char*>(at) - sizeof(size_t);

    char *trusted_end = static_cast<char*>(_trusted_memory) + *get_total_size();
    if (block_start < static_cast<char*>(_trusted_memory) || block_start >= trusted_end)
        throw std::invalid_argument("deallocate: pointer out of bounds");

    size_t *size_ptr = reinterpret_cast<size_t*>(block_start);
    size_t block_size_with_flag = *size_ptr;
    if (!(block_size_with_flag & size_t(1)))
        throw std::invalid_argument("deallocate: block is already free");

    size_t block_size = block_size_with_flag & ~size_t(1);
    *size_ptr = block_size;

    void **free_head = get_free_head();
    void *prev = nullptr;
    void *cur = *free_head;
    while (cur != nullptr && cur < block_start)
    {
        prev = cur;
        cur = *reinterpret_cast<void**>(static_cast<char*>(cur) + sizeof(size_t));
    }

    if (cur != nullptr && block_start + block_size == cur)
    {
        size_t *cur_size_ptr = reinterpret_cast<size_t*>(cur);
        size_t cur_size = *cur_size_ptr & ~size_t(1);
        block_size += cur_size;
        *size_ptr = block_size;

        void *next_cur = *reinterpret_cast<void**>(static_cast<char*>(cur) + sizeof(size_t));
        if (prev != nullptr)
            *reinterpret_cast<void**>(static_cast<char*>(prev) + sizeof(size_t)) = next_cur;
        else
            *free_head = next_cur;

        cur = next_cur;
    }

    if (prev != nullptr)
    {
        char *prev_start = static_cast<char*>(prev);
        size_t *prev_size_ptr = reinterpret_cast<size_t*>(prev_start);
        size_t prev_size = *prev_size_ptr & ~size_t(1);
        if (prev_start + prev_size == block_start)
        {
            prev_size += block_size;
            *prev_size_ptr = prev_size;

            *reinterpret_cast<void**>(static_cast<char*>(prev) + sizeof(size_t)) = cur;
            return;
        }
    }

    *reinterpret_cast<void**>(block_start + sizeof(size_t)) = cur;

    if (prev != nullptr)
        *reinterpret_cast<void**>(static_cast<char*>(prev) + sizeof(size_t)) = block_start;
    else
        *free_head = block_start;
}

inline void allocator_sorted_list::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    std::lock_guard<std::mutex> lock(*get_mutex());
    *get_fit_mode() = mode;
}

std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info() const noexcept
{
    std::lock_guard<std::mutex> lock(*reinterpret_cast<std::mutex*>(reinterpret_cast<char*>(_trusted_memory) +
        sizeof(std::pmr::memory_resource*) + sizeof(fit_mode) + sizeof(size_t)));
    return get_blocks_info_inner();
}


std::vector<allocator_test_utils::block_info> allocator_sorted_list::get_blocks_info_inner() const
{
    std::vector<allocator_test_utils::block_info> blocks_info;

    char* const mem_begin = static_cast<char*>(_trusted_memory);
    char* const mem_end = mem_begin +
        *reinterpret_cast<size_t*>(reinterpret_cast<char*>(_trusted_memory) + sizeof(std::pmr::memory_resource*) +
            sizeof(fit_mode));
    char* current = mem_begin + allocator_metadata_size;

    while (current < mem_end)
    {
        size_t header = *reinterpret_cast<size_t*>(current);
        bool is_block_occupied = (header & 1) != 0;
        size_t block_size = header & ~size_t(1);

        if (current + block_size > mem_end)
        {
            break;
        }

        blocks_info.push_back({block_size, is_block_occupied});
        current += block_size;
    }

    return blocks_info;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_begin() const noexcept
{
    return sorted_free_iterator(reinterpret_cast<void**>(reinterpret_cast<char*>(_trusted_memory) +
        sizeof(std::pmr::memory_resource*) + sizeof(fit_mode) + sizeof(size_t) + sizeof(std::mutex)));
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::free_end() const noexcept
{
    return sorted_free_iterator();
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::begin() const noexcept
{
    return sorted_iterator(const_cast<allocator_sorted_list*>(this));
}

void allocator_sorted_list::sorted_iterator::set_trusted_memory(void* trusted) noexcept
{
    _trusted_memory = trusted;
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::end() const noexcept
{
    auto it = sorted_iterator();
    it.set_trusted_memory(const_cast<allocator_sorted_list*>(this));
    return it;
}


bool allocator_sorted_list::sorted_free_iterator::operator==(
        const allocator_sorted_list::sorted_free_iterator & other) const noexcept
{
    return _free_ptr == other._free_ptr;
}

bool allocator_sorted_list::sorted_free_iterator::operator!=(
        const allocator_sorted_list::sorted_free_iterator &other) const noexcept
{
    return !(*this == other);
}

allocator_sorted_list::sorted_free_iterator &allocator_sorted_list::sorted_free_iterator::operator++() & noexcept
{
    if (_free_ptr) {
        _free_ptr = *reinterpret_cast<void**>(
            static_cast<char*>(_free_ptr) + sizeof(size_t)
        );
    }
    return *this;
}

allocator_sorted_list::sorted_free_iterator allocator_sorted_list::sorted_free_iterator::operator++(int n)
{
    auto tmp = *this;
    ++(*this);
    return tmp;
}

size_t allocator_sorted_list::sorted_free_iterator::size() const noexcept
{
    if (!_free_ptr) {
        return 0;
    }

    size_t raw = *reinterpret_cast<size_t*>(_free_ptr);
    return raw & ~size_t(1);
}

void *allocator_sorted_list::sorted_free_iterator::operator*() const noexcept
{
    if (!_free_ptr) {
        return nullptr;
    }

    return static_cast<char*>(_free_ptr) + sizeof(size_t);
}

allocator_sorted_list::sorted_free_iterator::sorted_free_iterator() : _free_ptr(nullptr)
{ }

allocator_sorted_list::sorted_free_iterator::sorted_free_iterator(void *trusted) : _free_ptr(trusted)
{ }

bool allocator_sorted_list::sorted_iterator::operator==(const allocator_sorted_list::sorted_iterator & other) const noexcept
{
    return _current_ptr == other._current_ptr && _trusted_memory == other._trusted_memory;
}

bool allocator_sorted_list::sorted_iterator::operator!=(const allocator_sorted_list::sorted_iterator &other) const noexcept
{
    return !(*this == other);
}

allocator_sorted_list::sorted_iterator &allocator_sorted_list::sorted_iterator::operator++() & noexcept
{
    if (!_current_ptr && !_trusted_memory) {
        return *this;
    }

    auto alloc = reinterpret_cast<allocator_sorted_list*>(_trusted_memory);
    char* mem_start = static_cast<char*>(alloc->_trusted_memory);
    char* mem_end = mem_start + *alloc->get_total_size();

    size_t block_size = *reinterpret_cast<size_t*>(_current_ptr) & ~size_t(1);
    char* next_ptr = static_cast<char*>(_current_ptr) + block_size;

    if (next_ptr > mem_end) {
        _current_ptr = nullptr;
    } else {
        _current_ptr = next_ptr;
    }

    return *this;
}

allocator_sorted_list::sorted_iterator allocator_sorted_list::sorted_iterator::operator++(int n)
{
    auto tmp = *this;
    ++(*this);
    return tmp;
}

size_t allocator_sorted_list::sorted_iterator::size() const noexcept
{
    if (!_current_ptr)  {
        return 0;
    }

    size_t raw = *reinterpret_cast<size_t*>(_current_ptr);
    return raw & ~size_t(1);
}

void *allocator_sorted_list::sorted_iterator::operator*() const noexcept
{
    if (!_current_ptr) {
        return nullptr;
    }

    return static_cast<char*>(_current_ptr) + sizeof(size_t);
}

allocator_sorted_list::sorted_iterator::sorted_iterator()
: _free_ptr(nullptr),
_current_ptr(nullptr),
_trusted_memory(nullptr)
{ }

allocator_sorted_list::sorted_iterator::sorted_iterator(void *trusted)
: _free_ptr(nullptr),
_current_ptr(nullptr),
_trusted_memory(trusted)
{
    if (_trusted_memory)
    {
        auto alloc = reinterpret_cast<allocator_sorted_list*>(trusted);
        char* mem_start = static_cast<char*>(alloc->_trusted_memory);
        _current_ptr = mem_start + alloc->allocator_metadata_size;
        if (_current_ptr >= mem_start + *alloc->get_total_size())
        {
            _current_ptr = nullptr;
        }
    }
}

bool allocator_sorted_list::sorted_iterator::occupied() const noexcept
{
    if (!_current_ptr) {
        return false;
    }

    size_t raw = *reinterpret_cast<size_t*>(_current_ptr);
    return (raw & 1) != 0;
}
