#include <cstdint>
#include <iterator>
#include <mutex>
#include <new>
#include <not_implemented.h>
#include <stdexcept>

#include "../include/allocator_red_black_tree.h"
#include "allocator_with_fit_mode.h"

auto allocator_red_black_tree::get_parent_allocator() {
    return &(reinterpret_cast<allocator_header*>(_trusted_memory)->parent_allocator);
}

auto allocator_red_black_tree::get_fit_mode() {
    return &(reinterpret_cast<allocator_header*>(_trusted_memory)->fit_mode);
}

auto allocator_red_black_tree::get_total_size() {
    return &(reinterpret_cast<allocator_header*>(_trusted_memory)->total_size);
}

auto allocator_red_black_tree::get_mutex() {
    return &(reinterpret_cast<allocator_header*>(_trusted_memory)->mutex);
}

auto allocator_red_black_tree::get_tree_root() {
    return &(reinterpret_cast<allocator_header*>(_trusted_memory)->root);
}

auto allocator_red_black_tree::get_free_list_head() {
    return &(reinterpret_cast<allocator_header*>(_trusted_memory)->head);
}

allocator_red_black_tree::block_ptr allocator_red_black_tree::get_parent(block_ptr node) {
    return node->parent;
}

allocator_red_black_tree::block_ptr allocator_red_black_tree::get_grandparent(block_ptr node) {
    auto p = get_parent(node);
    return p ? get_parent(p) : nullptr;
}

allocator_red_black_tree::block_ptr allocator_red_black_tree::get_uncle(block_ptr node) {
    auto p = get_parent(node);
    auto g = get_grandparent(node);
    if (!g) return nullptr;
    return p == g->left ? g->right : g->left;
}

allocator_red_black_tree::block_ptr allocator_red_black_tree::get_sibling(block_ptr node) {
    auto p = get_parent(node);
    if (!p) return nullptr;
    return node == p->left ? p->right : p->left;
}

auto allocator_red_black_tree::is_left_child(block_ptr node) {
    auto p = get_parent(node);
    return p && node == p->left;
}

auto allocator_red_black_tree::is_right_child(block_ptr node) {
    auto p = get_parent(node);
    return p && node == p->right;
}

void allocator_red_black_tree::rotate_left(block_ptr x, block_ptr& root) {
    auto y = x->right;
    x->right = y->left;
    if (y->left) y->left->parent = x;
    y->parent = x->parent;
    if (!x->parent) root = y;
    else if (is_left_child(x)) x->parent->left = y;
    else x->parent->right = y;
    y->left = x;
    x->parent = y;
}

void allocator_red_black_tree::rotate_right(block_ptr y, block_ptr& root) {
    auto x = y->left;
    y->left = x->right;
    if (x->right) x->right->parent = y;
    x->parent = y->parent;
    if (!y->parent) root = x;
    else if (is_left_child(y)) y->parent->left = x;
    else y->parent->right = x;
    x->right = y;
    y->parent = x;
}

void allocator_red_black_tree::insert_bst(block_ptr new_node, block_ptr& root) {
    block_ptr parent = nullptr;
    block_ptr* slot =  &root;
    while (*slot) {
        parent = *slot;
        if (new_node->size < (*slot)->size || (new_node->size == (*slot)->size && new_node < *slot)) {
            slot = &(*slot)->left;
        } else {
            slot = &(*slot)->right;
        }
    }
    *slot = new_node;
    new_node->parent = parent;
    new_node->left = nullptr;
    new_node->right = nullptr;
    new_node->data.color = color_t::RED;
}

void allocator_red_black_tree::fix_insert(block_ptr node, block_ptr& root) {
    while (node != root && node->parent->data.color == color_t::RED) {
        auto parent = get_parent(node);
        auto grandparent = get_parent(parent);
        if (is_left_child(parent)) {
            auto uncle = grandparent->right;
            if (uncle && uncle->data.color == color_t::RED) {
                parent->data.color = color_t::BLACK;
                uncle->data.color = color_t::BLACK;
                grandparent->data.color = color_t::RED;
                node = grandparent;
            } else {
                if (node == parent->right) {
                    rotate_left(parent, root);
                    node = parent;
                    parent = node->parent;
                }
                parent->data.color = color_t::BLACK;
                grandparent->data.color = color_t::RED;
                rotate_right(grandparent, root);
            }
        } else {
            auto uncle = grandparent->left;
            if (uncle && uncle->data.color == color_t::RED) {
                parent->data.color = color_t::BLACK;
                uncle->data.color = color_t::BLACK;
                grandparent->data.color = color_t::RED;
                node = grandparent;
            } else {
                if (node == parent->left) {
                    rotate_right(parent, root);
                    node = parent;
                    parent = node->parent;
                }
                parent->data.color = color_t::BLACK;
                grandparent->data.color = color_t::RED;
                rotate_left(grandparent, root);
            }
        }
    }
    root->data.color = color_t::BLACK;
}

void allocator_red_black_tree::add_to_tree(block_ptr node, block_ptr& root) {
    insert_bst(node, root);
    fix_insert(node, root);
}


void allocator_red_black_tree::transplant(block_ptr u, block_ptr v, block_ptr& root) {
    if (u->parent == nullptr) {
        root = v;
    } else if (is_left_child(u)) {
        u->parent->left = v;
    } else {
        u->parent->right = v;
    }
    if (v != nullptr) {
        v->parent = u->parent;
    }
}

auto allocator_red_black_tree::minimum(block_ptr node) {
    while (node->left) node = node->left;
    return node;
}

void allocator_red_black_tree::remove_from_tree(block_ptr z, block_ptr& root) {
    block_ptr y = z;
    block_ptr x;
    color_t y_original_color = y->data.color;

    if (!z->left) {
        x = z->right;
        transplant(z, z->right, root);
    } else if (!z->right) {
        x = z->left;
        transplant(z, z->left, root);
    } else {
        y = minimum(z->right);
        y_original_color = y->data.color;
        x = y->right;
        if (y->parent == z) {
            if (x) x->parent = y;
        } else {
            transplant(y, y->right, root);
            y->right = z->right;
            y->right->parent = y;
        }
        transplant(z, y, root);
        y->left = z->left;
        y->left->parent = y;
        y->data.color = z->data.color;
    }

    if (y_original_color == block_color::BLACK) {
        fix_delete(x, (x ? x->parent : y->parent), root);
    }
}

void allocator_red_black_tree::fix_delete(block_ptr x, block_ptr parent, block_ptr& root) {
    while (x != root && (x == nullptr || x->data.color == color_t::BLACK)) {
        if (x == parent->left) {
            block_ptr w = parent->right;
            if (w && w->data.color == color_t::RED) {
                w->data.color = color_t::BLACK;
                parent->data.color = color_t::RED;
                rotate_left(parent, root);
                w = parent->right;
            }
            if ((!w->left || w->left->data.color == color_t::BLACK) &&
                (!w->right || w->right->data.color == color_t::BLACK)) {
                w->data.color = color_t::RED;
                x = parent;
                parent = x->parent;
            } else {
                if (!w->right || w->right->data.color == color_t::BLACK) {
                    if (w->left) w->left->data.color = color_t::BLACK;
                    w->data.color = color_t::RED;
                    rotate_right(w, root);
                    w = parent->right;
                }
                w->data.color = parent->data.color;
                parent->data.color = color_t::BLACK;
                if (w->right) w->right->data.color = color_t::BLACK;
                rotate_left(parent, root);
                x = root;
            }
        } else {
            block_ptr w = parent->left;
            if (w && w->data.color == color_t::RED) {
                w->data.color = color_t::BLACK;
                parent->data.color = color_t::RED;
                rotate_right(parent, root);
                w = parent->left;
            }
            if ((!w->left || w->left->data.color == color_t::BLACK) &&
                (!w->right || w->right->data.color == color_t::BLACK)) {
                w->data.color = color_t::RED;
                x = parent;
                parent = x->parent;
            } else {
                if (!w->left || w->left->data.color == color_t::BLACK) {
                    if (w->right) w->right->data.color = color_t::BLACK;
                    w->data.color = color_t::RED;
                    rotate_left(w, root);
                    w = parent->left;
                }
                w->data.color = parent->data.color;
                parent->data.color = color_t::BLACK;
                if (w->left) w->left->data.color = color_t::BLACK;
                rotate_right(parent, root);
                x = root;
            }
        }
    }
    if (x) x->data.color = color_t::BLACK;
}

void allocator_red_black_tree::insert_into_free_list(block_ptr block, block_ptr& head) {
    if (!head || block < head) {
        block->next = head;
        block->prev = nullptr;
        if (head) head->prev = block;
        head = block;
    } else {
        auto curr = head;
        while (curr->next && curr->next < block)
            curr = curr->next;
        block->next = curr->next;
        block->prev = curr;
        if (curr->next) curr->next->prev = block;
        curr->next = block;
    }
}

void allocator_red_black_tree::remove_from_free_list(block_ptr block, block_ptr& free_list_head) {
    if (block->prev)
        block->prev->next = block->next;
    else
        free_list_head = block->next;
    if (block->next)
        block->next->prev = block->prev;
    block->prev = block->next = nullptr;
}

allocator_red_black_tree::~allocator_red_black_tree()
{
    get_mutex()->~mutex();

    auto alloc = *get_parent_allocator();

    if (alloc) {
        alloc->deallocate(_trusted_memory, *get_total_size() + allocator_metadata_size);
    } else {
        ::operator delete(_trusted_memory);
    }
}

allocator_red_black_tree::allocator_red_black_tree(
        size_t space_size,
        std::pmr::memory_resource *parent_allocator,
        allocator_with_fit_mode::fit_mode allocate_fit_mode)
{
    constexpr size_t min_block_size = std::max(sizeof(occupied_block), sizeof(free_block));
    if (space_size < min_block_size) {
        throw std::bad_alloc();
    }

    size_t needed_size = allocator_metadata_size + space_size;

    if (parent_allocator) {
        _trusted_memory = parent_allocator->allocate(needed_size);
    } else {
        _trusted_memory = ::operator new(needed_size);
    }

    auto* meta = new(_trusted_memory) allocator_header;
    meta->parent_allocator = parent_allocator;
    meta->fit_mode = allocate_fit_mode;
    meta->total_size = space_size;

    auto blocks_area = static_cast<char*>(_trusted_memory) + allocator_metadata_size;

    free_block* initial_block = reinterpret_cast<free_block*>(blocks_area);
    initial_block->size = space_size;
    initial_block->data.occupied = false;
    initial_block->data.color = color_t::BLACK;
    initial_block->left = nullptr;
    initial_block->right = nullptr;
    initial_block->parent = nullptr;
    initial_block->prev = nullptr;
    initial_block->next = nullptr;

    meta->root = initial_block;
    meta->head = initial_block;
}

bool allocator_red_black_tree::do_is_equal(const std::pmr::memory_resource &other) const noexcept
{
    if (this == &other) return true;
    auto* other_ptr = dynamic_cast<const allocator_red_black_tree*>(&other);
    if (!other_ptr) return false;
    return _trusted_memory == other_ptr->_trusted_memory;
}

[[nodiscard]] void *allocator_red_black_tree::do_allocate_sm(
    size_t size)
{
    std::lock_guard<std::mutex> lock(*get_mutex());

    block_ptr& head = *get_free_list_head();
    block_ptr& root = *get_tree_root();

    auto required_size = size + occupied_block_metadata_size;
    auto mode = *get_fit_mode();

    block_ptr found_block = nullptr;

    if (mode == allocator_with_fit_mode::fit_mode::first_fit) {
        for (auto current = head; current != nullptr; current = current->next) {
            if (current->size >= required_size) {
                found_block = current;
                break;
            }
        }
    } else if (mode == allocator_with_fit_mode::fit_mode::the_best_fit) {
        auto current = root;
        while (current != nullptr) {
            if (current->size >= required_size) {
                found_block = current;
                current = current->left;
            } else {
                current = current->right;
            }
        }
    } else {
        auto current = root;
        while (current && current->right) current = current->right;
        if (current && current->size >= required_size)
            found_block = current;
    }

    if (found_block == nullptr) {
        throw std::bad_alloc();
    }

    remove_from_free_list(found_block, head);
    remove_from_tree(found_block, root);

    size_t remaining = found_block->size - required_size;
    if (remaining >= free_block_metadata_size) {
        auto new_free = reinterpret_cast<block_ptr>(reinterpret_cast<char*>(found_block) + required_size);
        new_free->size = remaining;
        new_free->data.occupied = false;
        insert_into_free_list(new_free, head);
        add_to_tree(new_free, root);
        found_block->size = required_size;
    }

    found_block->data.occupied = true;
    return reinterpret_cast<char*>(found_block) + occupied_block_metadata_size;
}


void allocator_red_black_tree::do_deallocate_sm(
    void *at)
{
    if (!at) return;

    std::lock_guard<std::mutex> lock(*get_mutex());

    auto data_start = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
    auto data_end = data_start + *get_total_size();

    if (at < data_start || at >= data_end) {
        throw std::invalid_argument("");
    }

    auto block_hdr = reinterpret_cast<block_header*>(static_cast<char*>(at) - occupied_block_metadata_size);

    if (!block_hdr->data.occupied) {
        throw std::logic_error("");
    }

    if (block_hdr->size < sizeof(size_t) + occupied_block_metadata_size ||
            reinterpret_cast<char*>(block_hdr) + block_hdr->size > data_end) {
            throw std::logic_error("");
    }

    block_ptr& head = *get_free_list_head();
    block_ptr& root = *get_tree_root();

    free_block* prev_free = nullptr;
    free_block* next_free = nullptr;

    for (free_block* curr = head; curr; curr = curr->next) {
        if (reinterpret_cast<char*>(curr) + curr->size == reinterpret_cast<char*>(block_hdr)) {
            prev_free = curr;
        }
        if (reinterpret_cast<char*>(block_hdr) + block_hdr->size == reinterpret_cast<char*>(curr)) {
            next_free = curr;
            break;
        }
        if (curr > block_hdr) {
            break;
        }
    }

    block_ptr merged = reinterpret_cast<block_ptr>(block_hdr);
    merged->data.occupied = false;

    if (prev_free) {
        remove_from_free_list(prev_free, head);
        remove_from_tree(prev_free, root);
        prev_free->size += block_hdr->size;
        merged = prev_free;
    }

    if (next_free) {
        remove_from_free_list(next_free, head);
        remove_from_tree(next_free, root);
        merged->size += next_free->size;
    }

    merged->data.occupied = false;
    merged->left = nullptr;
    merged->right = nullptr;
    merged->parent = nullptr;
    merged->prev = nullptr;
    merged->next = nullptr;

    insert_into_free_list(merged, head);
    add_to_tree(merged, root);
}

void allocator_red_black_tree::set_fit_mode(allocator_with_fit_mode::fit_mode mode)
{
    std::lock_guard<std::mutex> lock(*get_mutex());
    *get_fit_mode() = mode;
}


std::vector<allocator_test_utils::block_info> allocator_red_black_tree::get_blocks_info() const
{
    std::lock_guard<std::mutex> lock(reinterpret_cast<allocator_header*>(_trusted_memory)->mutex);
    return get_blocks_info_inner();
}

std::vector<allocator_test_utils::block_info> allocator_red_black_tree::get_blocks_info_inner() const {
    std::vector<allocator_test_utils::block_info> result;

    char* const blocks_area = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
    size_t space_size = reinterpret_cast<allocator_header*>(_trusted_memory)->total_size;
    char* const blocks_end = blocks_area + space_size;

    char* current = blocks_area;
    while (current < blocks_end) {
        block_header* hdr = reinterpret_cast<block_header*>(current);

        if (hdr->size < sizeof(block_header) + occupied_block_metadata_size) {
            break;
        }
        if (current + hdr->size > blocks_end) {
            break;
        }

        allocator_test_utils::block_info info;
        info.block_size = hdr->size - (sizeof(block_header) + occupied_block_metadata_size);
        info.is_block_occupied = hdr->data.occupied;
        result.push_back(info);

        current += hdr->size;
    }
    return result;
}


allocator_red_black_tree::rb_iterator allocator_red_black_tree::begin() const noexcept {
    char* first_block = static_cast<char*>(_trusted_memory) + allocator_metadata_size;
    return rb_iterator(_trusted_memory, first_block);
}

allocator_red_black_tree::rb_iterator allocator_red_black_tree::end() const noexcept {
    return rb_iterator(_trusted_memory);
}


bool allocator_red_black_tree::rb_iterator::operator==(const rb_iterator& other) const noexcept {
    return _block_ptr == other._block_ptr && _trusted == other._trusted;
}

bool allocator_red_black_tree::rb_iterator::operator!=(const rb_iterator& other) const noexcept {
    return !(*this == other);
}

allocator_red_black_tree::rb_iterator& allocator_red_black_tree::rb_iterator::operator++() & noexcept {
    if (_block_ptr && _trusted) {
        block_header* hdr = static_cast<block_header*>(_block_ptr);
        char* next = static_cast<char*>(_block_ptr) + hdr->size;
        char* blocks_end = static_cast<char*>(_trusted) + allocator_metadata_size +
            reinterpret_cast<allocator_header*>(_trusted)->total_size;
        if (next < blocks_end) {
            _block_ptr = next;
        } else {
            _block_ptr = nullptr;
        }
    }
    return *this;
}

allocator_red_black_tree::rb_iterator allocator_red_black_tree::rb_iterator::operator++(int) {
    rb_iterator tmp = *this;
    ++(*this);
    return tmp;
}

size_t allocator_red_black_tree::rb_iterator::size() const noexcept {
    if (!_block_ptr) return 0;
    block_header* hdr = static_cast<block_header*>(_block_ptr);
    size_t total = hdr->size;
    size_t header_sz = sizeof(block_header) + occupied_block_metadata_size;
    return (total >= header_sz) ? total - header_sz : 0;
}

void* allocator_red_black_tree::rb_iterator::operator*() const noexcept {
    if (!_block_ptr) return nullptr;
    return static_cast<char*>(_block_ptr) + sizeof(block_header) + occupied_block_metadata_size;
}

bool allocator_red_black_tree::rb_iterator::occupied() const noexcept {
    if (!_block_ptr) return false;
    block_header* hdr = static_cast<block_header*>(_block_ptr);
    return hdr->data.occupied;
}

allocator_red_black_tree::rb_iterator::rb_iterator()
    : _block_ptr(nullptr), _trusted(nullptr) {}

allocator_red_black_tree::rb_iterator::rb_iterator(void* trusted)
    : _block_ptr(nullptr), _trusted(trusted) {}

allocator_red_black_tree::rb_iterator::rb_iterator(void* trusted, void* block_ptr)
    : _trusted(trusted), _block_ptr(block_ptr) {}
