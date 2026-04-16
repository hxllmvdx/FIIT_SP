#include <cstddef>
#include <cstdint>
#include <memory_resource>
#include <mutex>
#include <not_implemented.h>
#include "../include/allocator_boundary_tags.h"
#include "allocator_with_fit_mode.h"

static constexpr size_t MIN_FREE_BLOCK_SIZE = sizeof(size_t) + 2 * sizeof(void*);

auto allocator_boundary_tags::get_parent_allocator() {
    return reinterpret_cast<void**>(_trusted_memory);
}

auto allocator_boundary_tags::get_fit_mode() {
    return reinterpret_cast<fit_mode*>(static_cast<char*>(_trusted_memory) + sizeof(memory_resource*));
}

auto allocator_boundary_tags::get_total_size() {
    return reinterpret_cast<size_t*>(
        static_cast<char*>(_trusted_memory) + sizeof(memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode));
}

auto allocator_boundary_tags::get_mutex() {
    return reinterpret_cast<std::mutex*>(
        static_cast<char*>(_trusted_memory) + sizeof(memory_resource*) +
        sizeof(allocator_with_fit_mode::fit_mode) + sizeof(size_t));
}

auto allocator_boundary_tags::get_free_list_head() {
    return reinterpret_cast<void**>(
        static_cast<char*>(_trusted_memory) + sizeof(memory_resource*) +
        sizeof(allocator_with_fit_mode::fit_mode) + sizeof(size_t) + sizeof(std::mutex));
}

static void** get_prev_block(void* block) {
    return reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(size_t));
}

static void** get_next_block(void* block) {
    return reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(size_t) + sizeof(void*));
}

static void** get_data_ptr(void* block) {
    return reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(size_t) + sizeof(void*) + sizeof(void*));
}

static size_t* get_ptr_block_size(void* block) {
    return reinterpret_cast<size_t*>(block);
}

static size_t get_block_size(void* block) {
    return (*get_ptr_block_size(block)) & ~size_t(1);
}

static void** get_phys_prev(void* block) {
    return reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(size_t) + 2 * sizeof(void*));
}

static void set_phys_prev(void* block, void* phys_prev) {
    *reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(size_t) + 2 * sizeof(void*)) = phys_prev;
}

static bool is_free(void* block) {
    return (*get_ptr_block_size(block) & 1) == 0;
}

void allocator_boundary_tags::insert_into_free_list(void* block) {
    void** head = get_free_list_head();
    void* curr = *head;
    void* prev = nullptr;

    while (curr != nullptr && curr < block) {
        prev = curr;
        curr = *get_next_block(curr);
    }

    *get_prev_block(block) = prev;
    *get_next_block(block) = curr;

    if (prev == nullptr) {
        *head = block;
    } else {
        *get_next_block(prev) = block;
    }

    if (curr != nullptr) {
        *get_prev_block(curr) = block;
    }
}

void allocator_boundary_tags::remove_from_free_list(void* block) {
    auto next = *get_next_block(block);
    auto prev = *get_prev_block(block);

    if (prev == nullptr) {
        *get_free_list_head() = next;
    } else {
        *get_next_block(prev) = next;
    }

    if (next != nullptr) {
        *get_prev_block(next) = prev;
    }
}

allocator_boundary_tags::~allocator_boundary_tags()
{
    get_mutex()->~mutex();
    std::pmr::memory_resource* parent = *reinterpret_cast<std::pmr::memory_resource**>(_trusted_memory);
    parent->deallocate(_trusted_memory, *get_total_size());
}

allocator_boundary_tags::allocator_boundary_tags(
        size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    if (parent_allocator == nullptr)
        parent_allocator = std::pmr::get_default_resource();

    size_t total_alloc_size = space_size + allocator_metadata_size;
    void* full_memory = parent_allocator->allocate(total_alloc_size);
    _trusted_memory = full_memory;

    *get_parent_allocator() = parent_allocator;
    *get_fit_mode() = allocate_fit_mode;
    *get_total_size() = total_alloc_size;
    new (get_mutex()) std::mutex();

    void* first_block = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
    *get_free_list_head() = first_block;

    size_t remaining_size = space_size;

    if (remaining_size < occupied_block_metadata_size) {
        parent_allocator->deallocate(full_memory, total_alloc_size);
        throw std::bad_alloc();
    }

    *get_ptr_block_size(first_block) = remaining_size & ~size_t(1);
    *get_prev_block(first_block) = nullptr;
    *get_next_block(first_block) = nullptr;
    *get_data_ptr(first_block) = nullptr;
}

[[nodiscard]] void* allocator_boundary_tags::do_allocate_sm(size_t size) {
    size_t required_size = size + occupied_block_metadata_size;

    void* current = *get_free_list_head();
    auto fit_mode = *get_fit_mode();

    void* found_block = nullptr;
    size_t found_block_size = 0;

    void* best_fit = nullptr;
    size_t best_fit_size = SIZE_MAX;
    void* worst_fit = nullptr;
    size_t worst_fit_size = 0;

    while (current != nullptr) {
        size_t block_size = get_block_size(current);

        if (block_size >= required_size) {
            switch (fit_mode) {
                case fit_mode::first_fit:
                    found_block = current;
                    found_block_size = block_size;
                    break;
                case fit_mode::the_best_fit:
                    if (block_size <= best_fit_size) {
                        best_fit = current;
                        best_fit_size = block_size;
                    }
                    break;
                case fit_mode::the_worst_fit:
                    if (block_size >= worst_fit_size) {
                        worst_fit = current;
                        worst_fit_size = block_size;
                    }
                    break;
            }
            if (fit_mode == fit_mode::first_fit && found_block != nullptr) {
                break;
            }
        }
        current = *get_next_block(current);
    }

    if (fit_mode == fit_mode::the_best_fit) {
        found_block = best_fit;
        found_block_size = best_fit_size;
    } else if (fit_mode == fit_mode::the_worst_fit) {
        found_block = worst_fit;
        found_block_size = worst_fit_size;
    }

    if (found_block == nullptr) {
        throw std::bad_alloc();
    }

    void* saved_phys_prev = *get_phys_prev(found_block);
    remove_from_free_list(found_block);

    size_t remaining = found_block_size - required_size;

    if (remaining >= occupied_block_metadata_size) {
        *get_ptr_block_size(found_block) = required_size | 1;
        set_phys_prev(found_block, saved_phys_prev);

        void* new_free_block = static_cast<char*>(found_block) + required_size;
        *get_ptr_block_size(new_free_block) = remaining & ~size_t(1);
        *get_prev_block(new_free_block) = nullptr;
        *get_next_block(new_free_block) = nullptr;
        set_phys_prev(new_free_block, found_block);

        void* next_block = static_cast<char*>(new_free_block) + remaining;
        if (next_block < static_cast<char*>(_trusted_memory) + *get_total_size()) {
            set_phys_prev(next_block, new_free_block);
        }

        insert_into_free_list(new_free_block);
    } else {
        *get_ptr_block_size(found_block) = found_block_size | 1;
        set_phys_prev(found_block, saved_phys_prev);
    }

    return static_cast<char*>(found_block) + occupied_block_metadata_size;
}

void allocator_boundary_tags::do_deallocate_sm(void* at) {
    if (at == nullptr) return;

    char* start = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
    char* end   = static_cast<char*>(_trusted_memory) + *get_total_size();
    if (at < start || at >= end) {
        throw std::bad_alloc();
    }

    void* block = static_cast<char*>(at) - occupied_block_metadata_size;
    size_t block_size = get_block_size(block);
    *get_ptr_block_size(block) = block_size;

    void* left = *get_phys_prev(block);
    if (left != nullptr && is_free(left)) {
        remove_from_free_list(left);
        size_t left_size = get_block_size(left);
        *get_ptr_block_size(left) = left_size + block_size;
        block = left;
    }

    void* right = static_cast<char*>(block) + get_block_size(block);
    if (right < end && is_free(right)) {
        remove_from_free_list(right);
        size_t current_size = get_block_size(block);
        size_t right_size   = get_block_size(right);
        *get_ptr_block_size(block) = current_size + right_size;
    }

    void* next = static_cast<char*>(block) + get_block_size(block);
    if (next < end) {
        set_phys_prev(next, block);
    }

    insert_into_free_list(block);
}

inline void allocator_boundary_tags::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    std::lock_guard<std::mutex> lock(*get_mutex());
    *get_fit_mode() = mode;
}

std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info() const {
    std::lock_guard<std::mutex> lock(*reinterpret_cast<std::mutex*>(
        static_cast<char*>(_trusted_memory) + sizeof(memory_resource*) +
        sizeof(allocator_with_fit_mode::fit_mode) + sizeof(size_t)));
    return get_blocks_info_inner();
}

std::vector<allocator_test_utils::block_info> allocator_boundary_tags::get_blocks_info_inner() const {
    std::vector<allocator_test_utils::block_info> result;
    void* start = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
    void* end   = static_cast<char*>(_trusted_memory) + *reinterpret_cast<size_t*>(
        static_cast<char*>(_trusted_memory) + sizeof(memory_resource*) + sizeof(allocator_with_fit_mode::fit_mode));
    void* curr  = start;

    while (curr < end) {
        size_t size = get_block_size(curr);
        bool occupied = !is_free(curr);
        result.push_back({size, occupied});
        curr = static_cast<char*>(curr) + size;
    }
    return result;
}

auto allocator_boundary_tags::begin() const noexcept -> boundary_iterator {
    return boundary_iterator( _trusted_memory);
}

allocator_boundary_tags::boundary_iterator allocator_boundary_tags::end() const noexcept {
    return boundary_iterator();
}

bool allocator_boundary_tags::do_is_equal(const std::pmr::memory_resource &other) const noexcept {
    if (this == &other) return true;
    auto* other_ptr = dynamic_cast<const allocator_boundary_tags*>(&other);
    if (!other_ptr) return false;
    return _trusted_memory == other_ptr->_trusted_memory;
}

bool allocator_boundary_tags::boundary_iterator::operator==(
        const boundary_iterator& other) const noexcept {
    return _occupied_ptr == other._occupied_ptr &&
           _occupied == other._occupied &&
           _trusted_memory == other._trusted_memory;
}

bool allocator_boundary_tags::boundary_iterator::operator!=(
        const boundary_iterator& other) const noexcept {
    return !(*this == other);
}

allocator_boundary_tags::boundary_iterator&
allocator_boundary_tags::boundary_iterator::operator++() & noexcept {
    if (_occupied_ptr && _trusted_memory) {
        size_t sz = get_block_size(_occupied_ptr);
        char* next = static_cast<char*>(_occupied_ptr) + sz;
        char* end = static_cast<char*>(_trusted_memory) + *reinterpret_cast<size_t*>(
                        static_cast<char*>(_trusted_memory) + sizeof(std::pmr::memory_resource*) +
                        sizeof(allocator_with_fit_mode::fit_mode)
                    );
        if (next < end) {
            _occupied_ptr = next;
            _occupied = !is_free(_occupied_ptr);
        } else {
            _occupied_ptr = nullptr;
            _occupied = false;
        }
    }
    return *this;
}

allocator_boundary_tags::boundary_iterator&
allocator_boundary_tags::boundary_iterator::operator--() & noexcept {
    if (_occupied_ptr && _trusted_memory) {
        void* prev = *get_phys_prev(_occupied_ptr);
        char* start = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
        if (prev && prev >= start) {
            _occupied_ptr = prev;
            _occupied = !is_free(_occupied_ptr);
        } else {
        }
    }
    return *this;
}

allocator_boundary_tags::boundary_iterator
allocator_boundary_tags::boundary_iterator::operator++(int n) {
    boundary_iterator tmp = *this;
    ++(*this);
    return tmp;
}

allocator_boundary_tags::boundary_iterator
allocator_boundary_tags::boundary_iterator::operator--(int n) {
    boundary_iterator tmp = *this;
    --(*this);
    return tmp;
}

size_t allocator_boundary_tags::boundary_iterator::size() const noexcept {
    return _occupied_ptr ? get_block_size(_occupied_ptr) : 0;
}

bool allocator_boundary_tags::boundary_iterator::occupied() const noexcept {
    return _occupied;
}

void* allocator_boundary_tags::boundary_iterator::operator*() const noexcept {
    if (_occupied_ptr && _occupied) {
        return static_cast<char*>(_occupied_ptr) + occupied_block_metadata_size;
    }
    return nullptr;
}

allocator_boundary_tags::boundary_iterator::boundary_iterator()
    : _occupied_ptr(nullptr), _occupied(false), _trusted_memory(nullptr)
{ }

allocator_boundary_tags::boundary_iterator::boundary_iterator(void* trusted)
    : _trusted_memory(trusted)
{
    if (_trusted_memory) {
        char* start = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
        char* end   = static_cast<char*>(_trusted_memory) + *reinterpret_cast<size_t*>(
                          static_cast<char*>(_trusted_memory) + sizeof(std::pmr::memory_resource*) +
                          sizeof(allocator_with_fit_mode::fit_mode)
                      );
        if (start < end) {
            _occupied_ptr = start;
            _occupied = !is_free(_occupied_ptr);
        } else {
            _occupied_ptr = nullptr;
            _occupied = false;
        }
    } else {
        _occupied_ptr = nullptr;
        _occupied = false;
    }
}

void* allocator_boundary_tags::boundary_iterator::get_ptr() const noexcept {
    return _occupied_ptr;
}
