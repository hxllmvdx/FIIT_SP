#ifndef MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_RED_BLACK_TREE_H
#define MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_RED_BLACK_TREE_H

#include <memory_resource>
#include <pp_allocator.h>
#include <allocator_test_utils.h>
#include <allocator_with_fit_mode.h>
#include <mutex>

class allocator_red_black_tree final:
    public smart_mem_resource,
    public allocator_test_utils,
    public allocator_with_fit_mode
{

private:

    enum class block_color : unsigned char
    { RED, BLACK };

    struct block_data
    {
        bool occupied : 4;
        block_color color : 4;
    };

    struct block_header {
        size_t size;
        allocator_red_black_tree::block_data data;
    };

    struct occupied_block : block_header {
        void* unused[3];
    };

    struct free_block : block_header {
        free_block* left;
        free_block* right;
        free_block* parent;
        free_block* prev;
        free_block* next;
    };

    struct allocator_header {
        std::pmr::memory_resource* parent_allocator;
        fit_mode fit_mode;
        size_t total_size;
        std::mutex mutex;
        free_block* root;
        free_block* head;
    };

    using block_ptr = free_block*;
    using color_t = allocator_red_black_tree::block_color;

    void *_trusted_memory;

    static constexpr const size_t allocator_metadata_size = sizeof(allocator_header);
    static constexpr const size_t occupied_block_metadata_size = sizeof(occupied_block);
    static constexpr const size_t free_block_metadata_size = sizeof(free_block);

public:

    ~allocator_red_black_tree() override;

    allocator_red_black_tree(
        allocator_red_black_tree const &other) = delete;

    allocator_red_black_tree &operator=(
        allocator_red_black_tree const &other) = delete;

    allocator_red_black_tree(
        allocator_red_black_tree &&other) = delete;

    allocator_red_black_tree &operator=(
        allocator_red_black_tree &&other) = delete;

public:

    explicit allocator_red_black_tree(
            size_t space_size,
            std::pmr::memory_resource *parent_allocator = nullptr,
            allocator_with_fit_mode::fit_mode allocate_fit_mode = allocator_with_fit_mode::fit_mode::first_fit);

private:

    auto get_parent_allocator();
    auto get_fit_mode();
    auto get_total_size();
    auto get_mutex();
    auto get_tree_root();
    auto get_free_list_head();

    block_ptr get_parent(block_ptr node);
    block_ptr get_uncle(block_ptr node);
    block_ptr get_grandparent(block_ptr node);
    block_ptr get_sibling(block_ptr node);
    auto is_left_child(block_ptr node);
    auto is_right_child(block_ptr node);
    auto get_color(block_ptr node);
    auto get_size(block_ptr node);

    void rotate_left(block_ptr x, block_ptr& root);
    void rotate_right(block_ptr y, block_ptr& root);

    void insert_bst(block_ptr new_node, block_ptr& root);
    void fix_insert(block_ptr node, block_ptr& root);
    void add_to_tree(block_ptr node, block_ptr& root);

    void transplant(block_ptr u, block_ptr v, block_ptr& root);
    auto minimum(block_ptr node);
    void remove_from_tree(block_ptr z, block_ptr& root);
    void fix_delete(block_ptr x, block_ptr parent, block_ptr& root);

    void insert_into_free_list(block_ptr block, block_ptr& head);
    void remove_from_free_list(block_ptr block, block_ptr& head);


    [[nodiscard]] void *do_allocate_sm(
        size_t size) override;

    void do_deallocate_sm(
        void *at) override;

    bool do_is_equal(const std::pmr::memory_resource&) const noexcept override;

    std::vector<allocator_test_utils::block_info> get_blocks_info() const override;

    inline void set_fit_mode(allocator_with_fit_mode::fit_mode mode) override;

private:

    std::vector<allocator_test_utils::block_info> get_blocks_info_inner() const override;

    class rb_iterator
    {
        void* _block_ptr;
        void* _trusted;

    public:

        using iterator_category = std::forward_iterator_tag;
        using value_type = void*;
        using reference = void*&;
        using pointer = void**;
        using difference_type = ptrdiff_t;

        bool operator==(const rb_iterator&) const noexcept;

        bool operator!=(const rb_iterator&) const noexcept;

        rb_iterator& operator++() & noexcept;

        rb_iterator operator++(int n);

        size_t size() const noexcept;

        void* operator*() const noexcept;

        bool occupied()const noexcept;

        rb_iterator();

        rb_iterator(void* trusted);

        rb_iterator(void* trusted, void* block_ptr);
    };

    friend class rb_iterator;

    rb_iterator begin() const noexcept;
    rb_iterator end() const noexcept;

};

#endif //MATH_PRACTICE_AND_OPERATING_SYSTEMS_ALLOCATOR_ALLOCATOR_RED_BLACK_TREE_H
