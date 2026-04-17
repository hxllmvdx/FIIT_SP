#include <mutex>
#include <new>
#include <not_implemented.h>
#include <cstddef>
#include "../include/allocator_buddies_system.h"
#include "allocator_with_fit_mode.h"

struct allocator_header {
    std::pmr::memory_resource* parent;
    allocator_with_fit_mode::fit_mode mode;
    unsigned char max_k;
    std::mutex mtx;
    void** free_lists;
    void* data_start;
};

constexpr size_t header_size = sizeof(allocator_header);

auto allocator_buddies_system::get_parent_allocator() {
    return &static_cast<allocator_header*>(_trusted_memory)->parent;
}

auto allocator_buddies_system::get_fit_mode() {
    return &static_cast<allocator_header*>(_trusted_memory)->mode;
}

auto allocator_buddies_system::get_max_k() {
    return &static_cast<allocator_header*>(_trusted_memory)->max_k;
}

auto allocator_buddies_system::get_space_size() {
    return size_t(1) << *get_max_k();
}

auto allocator_buddies_system::get_mutex() {
    return &static_cast<allocator_header*>(_trusted_memory)->mtx;
}

auto allocator_buddies_system::get_free_list() {
    return &static_cast<allocator_header*>(_trusted_memory)->free_lists;
}

auto allocator_buddies_system::get_prev_block(void* block) {
    return reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(block_metadata));
}

auto allocator_buddies_system::get_next_block(void* block) {
    return reinterpret_cast<void**>(static_cast<char*>(block) + sizeof(block_metadata) + sizeof(void*));
}

void allocator_buddies_system::insert_into_free_list(void* block, unsigned char order) {
    void** head = &(*get_free_list())[order - min_k];

    *get_prev_block(block) = nullptr;
    *get_next_block(block) = *head;

    if (*head != nullptr) {
        *get_prev_block(*head) = block;
    }

    *head = block;
}

void allocator_buddies_system::remove_from_free_list(void* block, unsigned char order) {
    void** head = &(*get_free_list())[order - min_k];
    void* prev = *get_prev_block(block);
    void* next = *get_next_block(block);

    if (prev != nullptr) {
        *get_next_block(prev) = next;
    } else {
        *head = next;
    }

    if (next != nullptr) {
        *get_prev_block(next) = prev;
    }
}

auto allocator_buddies_system::split_block(void* block, unsigned char current_order, unsigned char target_order) {
    remove_from_free_list(block, current_order);

    while (current_order > target_order) {
        current_order--;
        size_t half_size = size_t(1) << current_order;
        void* buddy = static_cast<char*>(block) + half_size;

        block_metadata* buddy_meta = static_cast<block_metadata*>(buddy);
        buddy_meta->occupied = false;
        buddy_meta->size = current_order;
        insert_into_free_list(buddy, current_order);

        block_metadata* meta = static_cast<block_metadata*>(block);
        meta->size = current_order;
    }

    return block;
}

allocator_buddies_system::~allocator_buddies_system()
{
    auto* header = static_cast<allocator_header*>(_trusted_memory);
    header->mtx.~mutex();
    if (header->parent)
        header->parent->deallocate(_trusted_memory, get_space_size());
    else
        ::operator delete(_trusted_memory);
}

allocator_buddies_system::allocator_buddies_system(
        size_t space_size_power_of_two,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    if (space_size_power_of_two < (size_t(1) << min_k))
        throw std::logic_error("requested size too small for buddy system");

    unsigned char max_k = __detail::nearest_greater_k_of_2(space_size_power_of_two);
    if (max_k < min_k) max_k = min_k;
    if (max_k > 64) throw std::invalid_argument("space_size too large");

    size_t list_count = max_k - min_k + 1;
    size_t list_bytes = list_count * sizeof(void*);
    size_t align = size_t(1) << max_k;

    size_t total_bytes = header_size + list_bytes + align + (align - 1);

    _trusted_memory = parent_allocator
        ? parent_allocator->allocate(total_bytes)
        : ::operator new(total_bytes);

    char* base = static_cast<char*>(_trusted_memory);
    auto* header = new (_trusted_memory) allocator_header;
    header->parent = parent_allocator;
    header->mode = allocate_fit_mode;
    header->max_k = max_k;

    void** lists_array = reinterpret_cast<void**>(header + 1);
    header->free_lists = lists_array;
    for (size_t i = 0; i < list_count; ++i) lists_array[i] = nullptr;

    uintptr_t data_start_addr = (reinterpret_cast<uintptr_t>(base) + header_size + list_bytes + align - 1) & ~(align - 1);
    header->data_start = reinterpret_cast<void*>(data_start_addr);
    char* data_start = static_cast<char*>(header->data_start);

    if (data_start + align > base + total_bytes)
        throw std::bad_alloc();

    block_metadata* first_block = reinterpret_cast<block_metadata*>(data_start);
    first_block->occupied = false;
    first_block->size = max_k;
    *get_prev_block(first_block) = nullptr;
    *get_next_block(first_block) = nullptr;

    lists_array[max_k - min_k] = data_start;
}

[[nodiscard]] void *allocator_buddies_system::do_allocate_sm(
    size_t size)
{
    std::lock_guard<std::mutex> lock(*get_mutex());

    auto max_k = *get_max_k();
    size_t required_order = __detail::nearest_greater_k_of_2(size + occupied_block_metadata_size);
    if (required_order > max_k) throw std::bad_alloc();
    if (required_order < min_k) required_order = min_k;
    auto free_list = *get_free_list();

    int found_order = -1;
    fit_mode mode = *get_fit_mode();
    if (mode == allocator_with_fit_mode::fit_mode::first_fit || mode == allocator_with_fit_mode::fit_mode::the_best_fit) {
        for (int k = required_order; k <= max_k; ++k)
        {
            if (free_list[k - min_k] != nullptr)
            {
                found_order = k;
                break;
            }
        }
    } else {
        for (int k = max_k; k >= required_order; --k)
        {
            if (free_list[k - min_k] != nullptr)
            {
                found_order = k;
                break;
            }
        }
    }

    if (found_order == -1) throw std::bad_alloc();

    void *block = free_list[found_order - min_k];
    remove_from_free_list(block, found_order);

    if (found_order > (int)required_order) {
        block = split_block(block, found_order, required_order);
    }

    auto metadata = static_cast<block_metadata*>(block);
    metadata->occupied = true;
    return static_cast<char*>(block) + occupied_block_metadata_size;
}

void allocator_buddies_system::do_deallocate_sm(void *at) {
    std::lock_guard<std::mutex> lock(*get_mutex());
    if (!at) return;

    char* data_start = static_cast<char*>(static_cast<allocator_header*>(_trusted_memory)->data_start);
    unsigned char max_k = *get_max_k();
    char* data_end = data_start + (size_t(1) << max_k);

    char* block = static_cast<char*>(at) - occupied_block_metadata_size;
    if (block < data_start || block >= data_end) throw std::bad_alloc();

    auto meta = reinterpret_cast<block_metadata*>(block);
    unsigned char order = meta->size;
    meta->occupied = false;

    void* current = block;
    unsigned char cur_order = order;

    while (cur_order < max_k) {
        uintptr_t offset = static_cast<char*>(current) - data_start;
        uintptr_t buddy_offset = offset ^ (size_t(1) << cur_order);
        void* buddy = data_start + buddy_offset;

        block_metadata* buddy_meta = static_cast<block_metadata*>(buddy);
        if (buddy_meta->occupied || buddy_meta->size != cur_order)
            break;

        remove_from_free_list(buddy, cur_order);

        if (buddy < current) {
            current = buddy;
            offset = buddy_offset;
        }

        cur_order++;
    }

    if (cur_order != order) {
        block_metadata* merged_meta = static_cast<block_metadata*>(current);
        merged_meta->occupied = false;
        merged_meta->size = cur_order;
    }

    insert_into_free_list(current, cur_order);
}

bool allocator_buddies_system::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    if (this == &other) return true;
    auto* other_ptr = dynamic_cast<const allocator_buddies_system*>(&other);
    if (!other_ptr) return false;
    return _trusted_memory == other_ptr->_trusted_memory;
}

void allocator_buddies_system::set_fit_mode(
    allocator_with_fit_mode::fit_mode mode)
{
    std::lock_guard<std::mutex> lock(*get_mutex());
    *get_fit_mode() = mode;
}


std::vector<allocator_test_utils::block_info> allocator_buddies_system::get_blocks_info() const noexcept
{
    std::lock_guard<std::mutex> lock(static_cast<allocator_header*>(_trusted_memory)->mtx);
    return get_blocks_info_inner();
}

std::vector<allocator_test_utils::block_info> allocator_buddies_system::get_blocks_info_inner() const
{
    std::vector<allocator_test_utils::block_info> result;

    char* base = static_cast<char*>(_trusted_memory);
    unsigned char max_k = static_cast<allocator_header*>(_trusted_memory)->max_k;
    size_t list_bytes = (max_k - min_k + 1) * sizeof(void*);
    char* data_start = static_cast<char*>(static_cast<allocator_header*>(_trusted_memory)->data_start);
    char* data_end = data_start + (size_t(1) << max_k);

    char* current = data_start;
    while (current < data_end) {
        block_metadata* meta = reinterpret_cast<block_metadata*>(current);
        size_t block_size = size_t(1) << meta->size;
        bool occupied = meta->occupied;

        result.push_back({
            block_size,
            occupied
        });

        current += block_size;
    }

    return result;
}

allocator_buddies_system::buddy_iterator allocator_buddies_system::begin() const noexcept
{
    char* base = static_cast<char*>(_trusted_memory);
    unsigned char max_k = static_cast<allocator_header*>(_trusted_memory)->max_k;
    size_t list_bytes = (max_k - min_k + 1) * sizeof(void*);
    char* data_start = static_cast<char*>(static_cast<allocator_header*>(_trusted_memory)->data_start);
    return buddy_iterator(data_start);
}

allocator_buddies_system::buddy_iterator allocator_buddies_system::end() const noexcept
{
    return buddy_iterator();
}

bool allocator_buddies_system::buddy_iterator::operator!=(const buddy_iterator &other) const noexcept
{
    return _block != other._block;
}

allocator_buddies_system::buddy_iterator &allocator_buddies_system::buddy_iterator::operator++() & noexcept
{
    if (_block == nullptr) return *this;

    block_metadata* meta = static_cast<block_metadata*>(_block);
    size_t block_size = size_t(1) << meta->size;
    char* next = static_cast<char*>(_block) + block_size;

    _block = next;
    return *this;
}

allocator_buddies_system::buddy_iterator allocator_buddies_system::buddy_iterator::operator++(int n)
{
    buddy_iterator tmp = *this;
    ++(*this);
    return tmp;
}

size_t allocator_buddies_system::buddy_iterator::size() const noexcept
{
    if (_block == nullptr) return 0;
    block_metadata* meta = static_cast<block_metadata*>(_block);
    size_t total = size_t(1) << meta->size;
    return meta->occupied ? total - occupied_block_metadata_size : total - sizeof(block_metadata);
}

bool allocator_buddies_system::buddy_iterator::occupied() const noexcept
{
    if (_block == nullptr) return false;
    block_metadata* meta = static_cast<block_metadata*>(_block);
    return meta->occupied;
}

void *allocator_buddies_system::buddy_iterator::operator*() const noexcept
{
    if (_block == nullptr) return nullptr;
    block_metadata* meta = static_cast<block_metadata*>(_block);
    if (!meta->occupied) return nullptr;
    return static_cast<char*>(_block) + occupied_block_metadata_size;
}

allocator_buddies_system::buddy_iterator::buddy_iterator(void *start)
    : _block(start)
{ }

allocator_buddies_system::buddy_iterator::buddy_iterator()
    : _block(nullptr)
{ }
