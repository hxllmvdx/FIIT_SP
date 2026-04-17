#ifndef MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_BUDDIES_SYSTEM_H
#define MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_BUDDIES_SYSTEM_H

#include <memory_resource>
#include <pp_allocator.h>
#include <allocator_test_utils.h>
#include <allocator_with_fit_mode.h>
#include <mutex>
#include <cmath>

namespace __detail
{
    constexpr size_t nearest_greater_k_of_2(size_t size) noexcept
    {
        int ones_counter = 0, index = -1;

        constexpr const size_t o = 1;

        for (int i = sizeof(size_t) * 8 - 1; i >= 0; --i)
        {
            if (size & (o << i))
            {
                if (ones_counter == 0)
                    index = i;
                ++ones_counter;
            }
        }

        return ones_counter <= 1 ? index : index + 1;
    }
}

class allocator_buddies_system final:
    public smart_mem_resource,
    public allocator_test_utils,
    public allocator_with_fit_mode
{

private:


    struct block_metadata
    {
        bool occupied : 1;
        unsigned char size : 7;
    };

    void *_trusted_memory;

    /**
     * TODO: You must improve it for alignment support
     */

    static constexpr const size_t allocator_metadata_size = sizeof(std::pmr::memory_resource*) + sizeof(fit_mode) + sizeof(unsigned char) + sizeof(std::mutex) + sizeof(void**);

    static constexpr const size_t occupied_block_metadata_size = sizeof(block_metadata) + sizeof(void*);

    static constexpr const size_t free_block_metadata_size = sizeof(block_metadata);

    static constexpr unsigned char min_k =
        __detail::nearest_greater_k_of_2(sizeof(block_metadata) + 2 * sizeof(void*));

public:

    explicit allocator_buddies_system(
            size_t space_size_power_of_two,
            std::pmr::memory_resource *parent_allocator = nullptr,
            allocator_with_fit_mode::fit_mode allocate_fit_mode = allocator_with_fit_mode::fit_mode::first_fit);

    allocator_buddies_system(
        allocator_buddies_system const &other) = delete;

    allocator_buddies_system &operator=(
        allocator_buddies_system const &other) = delete;

    allocator_buddies_system(
        allocator_buddies_system &&other) = delete;

    allocator_buddies_system &operator=(
        allocator_buddies_system &&other) = delete;

    ~allocator_buddies_system() override;

private:

    auto get_parent_allocator();
    auto get_fit_mode();
    auto get_max_k();
    auto get_space_size();
    auto get_mutex();
    auto get_free_list();
    auto get_prev_block(void* block);
    auto get_next_block(void* block);
    void insert_into_free_list(void* block, unsigned char order);
    void remove_from_free_list(void* block, unsigned char order);
    auto split_block(void* block, unsigned char current_order, unsigned char target_order);

    [[nodiscard]] void *do_allocate_sm(
        size_t size) override;

    void do_deallocate_sm(
        void *at) override;

    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;

    void set_fit_mode(
        allocator_with_fit_mode::fit_mode mode) override;


    std::vector<allocator_test_utils::block_info> get_blocks_info() const noexcept override;

private:

    std::vector<allocator_test_utils::block_info> get_blocks_info_inner() const override;

    /** TODO: Highly recommended for helper functions to return references */

    class buddy_iterator
    {
        void* _block;

    public:

        using iterator_category = std::forward_iterator_tag;
        using value_type = void*;
        using reference = void*&;
        using pointer = void**;
        using difference_type = ptrdiff_t;

        bool operator==(const buddy_iterator&) const noexcept;

        bool operator!=(const buddy_iterator&) const noexcept;

        buddy_iterator& operator++() & noexcept;

        buddy_iterator operator++(int n);

        size_t size() const noexcept;

        bool occupied() const noexcept;

        void* operator*() const noexcept;

        buddy_iterator();

        buddy_iterator(void* start);
    };

    friend class buddy_iterator;

    buddy_iterator begin() const noexcept;

    buddy_iterator end() const noexcept;

};

#endif //MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_BUDDIES_SYSTEM_H
