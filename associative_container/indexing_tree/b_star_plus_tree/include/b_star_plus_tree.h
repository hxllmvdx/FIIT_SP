#ifndef SYS_PROG_BS_PLUS_TREE_H
#define SYS_PROG_BS_PLUS_TREE_H

#include <cassert>
#include <concepts>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <boost/container/static_vector.hpp>

#include <associative_container.h>
#include <pp_allocator.h>

template <typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5>
class BSP_tree final : private compare
{
    static_assert(t >= 2, "BSP_tree minimum degree must be at least 2");

public:
    using tree_data_type = std::pair<tkey, tvalue>;
    using tree_data_type_const = std::pair<const tkey, tvalue>;
    using value_type = tree_data_type_const;

    using allocator_type = pp_allocator<value_type>;
    using alloc_traits = std::allocator_traits<allocator_type>;

    using propagate_on_copy = typename alloc_traits::propagate_on_container_copy_assignment;
    using propagate_on_move = typename alloc_traits::propagate_on_container_move_assignment;
    using propagate_on_swap = typename alloc_traits::propagate_on_container_swap;
    using is_always_equal = typename alloc_traits::is_always_equal;

private:
    static constexpr std::size_t minimum_keys_in_node = t - 1;
    static constexpr std::size_t maximum_keys_in_node = 2 * t - 1;

    [[nodiscard]] bool compare_keys(const tkey& lhs, const tkey& rhs) const {
        return compare::operator()(lhs, rhs);
    }

    [[nodiscard]] bool keys_equal(const tkey& lhs, const tkey& rhs) const {
        return !compare_keys(lhs, rhs) && !compare_keys(rhs, lhs);
    }

    struct bsptree_node_base
    {
        bool _is_terminated;

        explicit bsptree_node_base(bool is_terminated = false) noexcept
            : _is_terminated(is_terminated)
        {}

        virtual ~bsptree_node_base() = default;
    };

    struct bsptree_node_term final : public bsptree_node_base
    {
        bsptree_node_term* _next;
        boost::container::static_vector<tree_data_type, maximum_keys_in_node + 1> _data;

        bsptree_node_term() noexcept
            : bsptree_node_base(true),
              _next(nullptr)
        {}
    };

    struct bsptree_node_middle final : public bsptree_node_base
    {
        boost::container::static_vector<tkey, maximum_keys_in_node + 1> _keys;
        boost::container::static_vector<bsptree_node_base*, maximum_keys_in_node + 2> _pointers;

        bsptree_node_middle() noexcept
            : bsptree_node_base(false)
        {}
    };

    using path_entry = std::pair<bsptree_node_middle*, std::size_t>;
    using path_type = std::vector<path_entry>;

public:
    class bsptree_iterator;
    class bsptree_const_iterator;

private:
    allocator_type _allocator;
    bsptree_node_base* _root;
    std::size_t _size;

    [[nodiscard]] allocator_type get_allocator() const noexcept {
        return _allocator;
    }

    [[nodiscard]] bsptree_node_term* create_terminal_node() {
        return _allocator.template new_object<bsptree_node_term>();
    }

    [[nodiscard]] bsptree_node_middle* create_middle_node() {
        return _allocator.template new_object<bsptree_node_middle>();
    }

    void destroy_node(bsptree_node_base* node) noexcept {
        if (node == nullptr) {
            return;
        }

        if (is_terminal(node)) {
            _allocator.template delete_object<bsptree_node_term>(as_terminal(node));
        } else {
            _allocator.template delete_object<bsptree_node_middle>(as_middle(node));
        }
    }

    void clear_subtree(bsptree_node_base* node) noexcept {
        if (node == nullptr) {
            return;
        }

        if (!node->_is_terminated) {
            auto* middle = as_middle(node);
            for (bsptree_node_base* child : middle->_pointers) {
                clear_subtree(child);
            }
        }

        destroy_node(node);
    }

    void swap(BSP_tree& other) noexcept {
        using std::swap;

        if constexpr (propagate_on_swap::value) {
            swap(_allocator, other._allocator);
        } else {
            assert(_allocator == other._allocator);
        }

        swap(static_cast<compare&>(*this), static_cast<compare&>(other));
        swap(_root, other._root);
        swap(_size, other._size);
    }

    [[nodiscard]] bool is_terminal(const bsptree_node_base* node) const noexcept {
        return node->_is_terminated;
    }

    [[nodiscard]] bsptree_node_term* as_terminal(bsptree_node_base* node) const noexcept {
        return static_cast<bsptree_node_term*>(node);
    }

    [[nodiscard]] const bsptree_node_term* as_terminal(const bsptree_node_base* node) const noexcept {
        return static_cast<const bsptree_node_term*>(node);
    }

    [[nodiscard]] bsptree_node_middle* as_middle(bsptree_node_base* node) const noexcept {
        return static_cast<bsptree_node_middle*>(node);
    }

    [[nodiscard]] const bsptree_node_middle* as_middle(const bsptree_node_base* node) const noexcept {
        return static_cast<const bsptree_node_middle*>(node);
    }

    [[nodiscard]] std::size_t lower_bound_index(const bsptree_node_term* node, const tkey& key) const {
        std::size_t left = 0;
        std::size_t right = node->_data.size();

        while (left < right) {
            const std::size_t middle = left + (right - left) / 2;
            if (compare_keys(node->_data[middle].first, key)) {
                left = middle + 1;
            } else {
                right = middle;
            }
        }

        return left;
    }

    [[nodiscard]] std::size_t upper_bound_index(const bsptree_node_term* node, const tkey& key) const {
        std::size_t index = lower_bound_index(node, key);
        while (index < node->_data.size() && keys_equal(node->_data[index].first, key)) {
            ++index;
        }
        return index;
    }

    [[nodiscard]] std::size_t lower_bound_index(const bsptree_node_middle* node, const tkey& key) const {
        std::size_t left = 0;
        std::size_t right = node->_keys.size();

        while (left < right) {
            const std::size_t middle = left + (right - left) / 2;
            if (compare_keys(node->_keys[middle], key)) {
                left = middle + 1;
            } else {
                right = middle;
            }
        }

        return left;
    }

    [[nodiscard]] std::size_t upper_bound_index(const bsptree_node_middle* node, const tkey& key) const {
        std::size_t index = lower_bound_index(node, key);
        if (index < node->_keys.size() && keys_equal(key, node->_keys[index])) {
            ++index;
        }
        return index;
    }

    [[nodiscard]] std::pair<bsptree_node_term*, std::size_t> locate_key(const tkey& key) const {
        auto [leaf, path] = locate_leaf_for_read(key);
        static_cast<void>(path);

        if (leaf == nullptr) {
            return {nullptr, 0};
        }

        const std::size_t index = lower_bound_index(leaf, key);
        if (index < leaf->_data.size() && keys_equal(leaf->_data[index].first, key)) {
            return {const_cast<bsptree_node_term*>(leaf), index};
        }

        return {nullptr, 0};
    }

    [[nodiscard]] std::pair<bsptree_node_term*, path_type> locate_leaf_for_insert(const tkey& key) const {
        bsptree_node_base* current = _root;
        path_type path;

        if (current == nullptr) {
            return {nullptr, path};
        }

        while (!is_terminal(current)) {
            auto* middle = as_middle(current);
            const std::size_t index = upper_bound_index(middle, key);
            path.emplace_back(middle, index);
            current = middle->_pointers[index];
        }

        return {as_terminal(current), path};
    }

    [[nodiscard]] std::pair<const bsptree_node_term*, path_type> locate_leaf_for_read(const tkey& key) const {
        const bsptree_node_base* current = _root;
        path_type path;

        if (current == nullptr) {
            return {nullptr, path};
        }

        while (!is_terminal(current)) {
            auto* middle = as_middle(current);
            const std::size_t index = upper_bound_index(middle, key);
            path.emplace_back(const_cast<bsptree_node_middle*>(middle), index);
            current = middle->_pointers[index];
        }

        return {as_terminal(current), path};
    }

    [[nodiscard]] bsptree_node_term* leftmost_leaf() const {
        if (_root == nullptr) {
            return nullptr;
        }

        bsptree_node_base* current = _root;
        while (!is_terminal(current)) {
            auto* middle = as_middle(current);
            assert(!middle->_pointers.empty());
            current = middle->_pointers.front();
        }

        return as_terminal(current);
    }

    [[nodiscard]] bsptree_iterator make_iterator(bsptree_node_term* node, std::size_t index) noexcept {
        if (node == nullptr || index >= node->_data.size()) {
            return make_end_iterator();
        }
        return bsptree_iterator(this, node, index);
    }

    [[nodiscard]] bsptree_const_iterator make_const_iterator(const bsptree_node_term* node, std::size_t index) const noexcept {
        if (node == nullptr || index >= node->_data.size()) {
            return make_end_iterator();
        }
        return bsptree_const_iterator(this, node, index);
    }

    [[nodiscard]] bsptree_iterator make_end_iterator() noexcept {
        return bsptree_iterator(this, nullptr, 0);
    }

    [[nodiscard]] bsptree_const_iterator make_end_iterator() const noexcept {
        return bsptree_const_iterator(this, nullptr, 0);
    }

    void assign_leaf_range(
        bsptree_node_term* node,
        const std::vector<tree_data_type>& source,
        std::size_t begin,
        std::size_t end)
    {
        node->_data.clear();
        for (std::size_t index = begin; index < end; ++index) {
            node->_data.push_back(source[index]);
        }
    }

    void assign_middle_keys_range(
        bsptree_node_middle* node,
        const std::vector<tkey>& source,
        std::size_t begin,
        std::size_t end)
    {
        node->_keys.clear();
        for (std::size_t index = begin; index < end; ++index) {
            node->_keys.push_back(source[index]);
        }
    }

    void assign_middle_children_range(
        bsptree_node_middle* node,
        const std::vector<bsptree_node_base*>& source,
        std::size_t begin,
        std::size_t end)
    {
        node->_pointers.clear();
        for (std::size_t index = begin; index < end; ++index) {
            node->_pointers.push_back(source[index]);
        }
    }

    [[nodiscard]] bool redistribute_leaf_with_left_sibling(bsptree_node_middle* parent, std::size_t child_index)
    {
        if (child_index == 0) {
            return false;
        }

        auto* left = as_terminal(parent->_pointers[child_index - 1]);
        auto* current = as_terminal(parent->_pointers[child_index]);
        if (left->_data.size() >= maximum_keys_in_node) {
            return false;
        }

        std::vector<tree_data_type> combined;
        combined.reserve(left->_data.size() + current->_data.size());
        combined.insert(combined.end(), left->_data.begin(), left->_data.end());
        combined.insert(combined.end(), current->_data.begin(), current->_data.end());

        const std::size_t left_size = combined.size() / 2;
        assign_leaf_range(left, combined, 0, left_size);
        assign_leaf_range(current, combined, left_size, combined.size());
        parent->_keys[child_index - 1] = current->_data.front().first;
        return true;
    }

    [[nodiscard]] bool redistribute_leaf_with_right_sibling(bsptree_node_middle* parent, std::size_t child_index)
    {
        if (child_index + 1 >= parent->_pointers.size()) {
            return false;
        }

        auto* current = as_terminal(parent->_pointers[child_index]);
        auto* right = as_terminal(parent->_pointers[child_index + 1]);
        if (right->_data.size() >= maximum_keys_in_node) {
            return false;
        }

        std::vector<tree_data_type> combined;
        combined.reserve(current->_data.size() + right->_data.size());
        combined.insert(combined.end(), current->_data.begin(), current->_data.end());
        combined.insert(combined.end(), right->_data.begin(), right->_data.end());

        const std::size_t current_size = combined.size() / 2;
        assign_leaf_range(current, combined, 0, current_size);
        assign_leaf_range(right, combined, current_size, combined.size());
        parent->_keys[child_index] = right->_data.front().first;
        return true;
    }

    [[nodiscard]] bool redistribute_middle_with_left_sibling(bsptree_node_middle* parent, std::size_t child_index)
    {
        if (child_index == 0) {
            return false;
        }

        auto* left = as_middle(parent->_pointers[child_index - 1]);
        auto* current = as_middle(parent->_pointers[child_index]);
        if (left->_keys.size() >= maximum_keys_in_node) {
            return false;
        }

        std::vector<tkey> combined_keys;
        combined_keys.reserve(left->_keys.size() + 1 + current->_keys.size());
        combined_keys.insert(combined_keys.end(), left->_keys.begin(), left->_keys.end());
        combined_keys.push_back(parent->_keys[child_index - 1]);
        combined_keys.insert(combined_keys.end(), current->_keys.begin(), current->_keys.end());

        std::vector<bsptree_node_base*> combined_children;
        combined_children.reserve(left->_pointers.size() + current->_pointers.size());
        combined_children.insert(combined_children.end(), left->_pointers.begin(), left->_pointers.end());
        combined_children.insert(combined_children.end(), current->_pointers.begin(), current->_pointers.end());

        const std::size_t left_size = combined_keys.size() / 2;
        const std::size_t separator_index = left_size;

        assign_middle_keys_range(left, combined_keys, 0, left_size);
        parent->_keys[child_index - 1] = combined_keys[separator_index];
        assign_middle_keys_range(current, combined_keys, separator_index + 1, combined_keys.size());
        assign_middle_children_range(left, combined_children, 0, left_size + 1);
        assign_middle_children_range(current, combined_children, left_size + 1, combined_children.size());
        return true;
    }

    [[nodiscard]] bool redistribute_middle_with_right_sibling(bsptree_node_middle* parent, std::size_t child_index)
    {
        if (child_index + 1 >= parent->_pointers.size()) {
            return false;
        }

        auto* current = as_middle(parent->_pointers[child_index]);
        auto* right = as_middle(parent->_pointers[child_index + 1]);
        if (right->_keys.size() >= maximum_keys_in_node) {
            return false;
        }

        std::vector<tkey> combined_keys;
        combined_keys.reserve(current->_keys.size() + 1 + right->_keys.size());
        combined_keys.insert(combined_keys.end(), current->_keys.begin(), current->_keys.end());
        combined_keys.push_back(parent->_keys[child_index]);
        combined_keys.insert(combined_keys.end(), right->_keys.begin(), right->_keys.end());

        std::vector<bsptree_node_base*> combined_children;
        combined_children.reserve(current->_pointers.size() + right->_pointers.size());
        combined_children.insert(combined_children.end(), current->_pointers.begin(), current->_pointers.end());
        combined_children.insert(combined_children.end(), right->_pointers.begin(), right->_pointers.end());

        const std::size_t current_size = combined_keys.size() / 2;
        const std::size_t separator_index = current_size;

        assign_middle_keys_range(current, combined_keys, 0, current_size);
        parent->_keys[child_index] = combined_keys[separator_index];
        assign_middle_keys_range(right, combined_keys, separator_index + 1, combined_keys.size());
        assign_middle_children_range(current, combined_children, 0, current_size + 1);
        assign_middle_children_range(right, combined_children, current_size + 1, combined_children.size());
        return true;
    }

    [[nodiscard]] bool redistribute_overflow(bsptree_node_middle* parent, std::size_t child_index, bool child_is_leaf)
    {
        if (child_is_leaf) {
            return redistribute_leaf_with_right_sibling(parent, child_index);
        }

        return redistribute_middle_with_right_sibling(parent, child_index);
    }

    void split_two_leaves_into_three(bsptree_node_middle* parent, std::size_t left_child_index)
    {
        auto* left = as_terminal(parent->_pointers[left_child_index]);
        auto* right = as_terminal(parent->_pointers[left_child_index + 1]);

        std::vector<tree_data_type> combined;
        combined.reserve(left->_data.size() + right->_data.size());
        combined.insert(combined.end(), left->_data.begin(), left->_data.end());
        combined.insert(combined.end(), right->_data.begin(), right->_data.end());

        const std::size_t base_size = combined.size() / 3;
        const std::size_t remainder = combined.size() % 3;
        const std::size_t left_size = base_size + (remainder > 0 ? 1 : 0);
        const std::size_t middle_size = base_size + (remainder > 1 ? 1 : 0);
        const std::size_t right_begin = left_size + middle_size;

        auto* middle = create_terminal_node();
        assign_leaf_range(left, combined, 0, left_size);
        assign_leaf_range(middle, combined, left_size, right_begin);
        assign_leaf_range(right, combined, right_begin, combined.size());

        middle->_next = right;
        left->_next = middle;

        parent->_keys[left_child_index] = middle->_data.front().first;
        parent->_keys.insert(parent->_keys.begin() + static_cast<std::ptrdiff_t>(left_child_index + 1), right->_data.front().first);
        parent->_pointers.insert(parent->_pointers.begin() + static_cast<std::ptrdiff_t>(left_child_index + 1), middle);
    }

    void split_two_middles_into_three(bsptree_node_middle* parent, std::size_t left_child_index)
    {
        auto* left = as_middle(parent->_pointers[left_child_index]);
        auto* right = as_middle(parent->_pointers[left_child_index + 1]);

        std::vector<tkey> combined_keys;
        combined_keys.reserve(left->_keys.size() + 1 + right->_keys.size());
        combined_keys.insert(combined_keys.end(), left->_keys.begin(), left->_keys.end());
        combined_keys.push_back(parent->_keys[left_child_index]);
        combined_keys.insert(combined_keys.end(), right->_keys.begin(), right->_keys.end());

        std::vector<bsptree_node_base*> combined_children;
        combined_children.reserve(left->_pointers.size() + right->_pointers.size());
        combined_children.insert(combined_children.end(), left->_pointers.begin(), left->_pointers.end());
        combined_children.insert(combined_children.end(), right->_pointers.begin(), right->_pointers.end());

        const std::size_t distributable_keys = combined_keys.size() - 2;
        const std::size_t base_size = distributable_keys / 3;
        const std::size_t remainder = distributable_keys % 3;

        const std::size_t left_size = base_size + (remainder > 0 ? 1 : 0);
        const std::size_t middle_size = base_size + (remainder > 1 ? 1 : 0);
        const std::size_t first_separator_index = left_size;
        const std::size_t second_separator_index = left_size + 1 + middle_size;

        auto* middle = create_middle_node();

        assign_middle_keys_range(left, combined_keys, 0, left_size);
        parent->_keys[left_child_index] = combined_keys[first_separator_index];
        assign_middle_keys_range(middle, combined_keys, first_separator_index + 1, second_separator_index);
        const tkey promoted_key = combined_keys[second_separator_index];
        assign_middle_keys_range(right, combined_keys, second_separator_index + 1, combined_keys.size());

        assign_middle_children_range(left, combined_children, 0, left_size + 1);
        assign_middle_children_range(middle, combined_children, left_size + 1, left_size + middle_size + 2);
        assign_middle_children_range(right, combined_children, left_size + middle_size + 2, combined_children.size());

        parent->_keys.insert(parent->_keys.begin() + static_cast<std::ptrdiff_t>(left_child_index + 1), promoted_key);
        parent->_pointers.insert(parent->_pointers.begin() + static_cast<std::ptrdiff_t>(left_child_index + 1), middle);
    }

    template <typename pair_type>
    std::pair<bsptree_node_term*, std::size_t> insert_new(pair_type&& data) {
        const tkey key = data.first;
        if (_root == nullptr) {
            auto* leaf = create_terminal_node();
            leaf->_data.push_back(std::forward<pair_type>(data));
            _root = leaf;
            _size = 1;
            return {leaf, 0};
        }

        path_type path;
        bsptree_node_base* current = _root;

        while (!is_terminal(current)) {
            auto* middle = as_middle(current);
            const std::size_t child_index = upper_bound_index(middle, key);
            path.emplace_back(middle, child_index);
            current = middle->_pointers[child_index];
        }

        auto* leaf = as_terminal(current);
        const std::size_t insert_index = lower_bound_index(leaf, key);
        leaf->_data.insert(leaf->_data.begin() + static_cast<std::ptrdiff_t>(insert_index), std::forward<pair_type>(data));
        ++_size;

        bsptree_node_base* current_node = leaf;
        bool current_is_leaf = true;

        while (current_is_leaf ? (as_terminal(current_node)->_data.size() > maximum_keys_in_node)
                               : (as_middle(current_node)->_keys.size() > maximum_keys_in_node)) {
            if (path.empty()) {
                if (current_is_leaf) {
                    auto* current_leaf = as_terminal(current_node);
                    const std::size_t split_index = current_leaf->_data.size() / 2;
                    auto* right_leaf = create_terminal_node();
                    right_leaf->_data.assign(current_leaf->_data.begin() + static_cast<std::ptrdiff_t>(split_index), current_leaf->_data.end());
                    current_leaf->_data.erase(current_leaf->_data.begin() + static_cast<std::ptrdiff_t>(split_index), current_leaf->_data.end());
                    right_leaf->_next = current_leaf->_next;
                    current_leaf->_next = right_leaf;
                    create_new_root(current_leaf, right_leaf->_data.front().first, right_leaf);
                } else {
                    auto* current_middle = as_middle(current_node);
                    const std::size_t middle_index = current_middle->_keys.size() / 2;
                    const tkey promoted_key = current_middle->_keys[middle_index];
                    auto* right_middle = create_middle_node();
                    right_middle->_keys.assign(current_middle->_keys.begin() + static_cast<std::ptrdiff_t>(middle_index + 1), current_middle->_keys.end());
                    right_middle->_pointers.assign(current_middle->_pointers.begin() + static_cast<std::ptrdiff_t>(middle_index + 1), current_middle->_pointers.end());
                    current_middle->_keys.erase(current_middle->_keys.begin() + static_cast<std::ptrdiff_t>(middle_index), current_middle->_keys.end());
                    current_middle->_pointers.erase(current_middle->_pointers.begin() + static_cast<std::ptrdiff_t>(middle_index + 1), current_middle->_pointers.end());
                    create_new_root(current_middle, promoted_key, right_middle);
                }
                break;
            }

            auto [parent, parent_index] = path.back();
            path.pop_back();

            if (redistribute_overflow(parent, parent_index, current_is_leaf)) {
                break;
            }

            if (parent->_pointers.size() > 1) {
                const std::size_t left_child_index =
                    parent_index + 1 < parent->_pointers.size()
                    ? parent_index
                    : parent_index - 1;

                if (current_is_leaf) {
                    split_two_leaves_into_three(parent, left_child_index);
                } else {
                    split_two_middles_into_three(parent, left_child_index);
                }

                current_node = parent;
                current_is_leaf = false;
                continue;
            }

            if (current_is_leaf) {
                auto* current_leaf = as_terminal(current_node);
                const std::size_t split_index = current_leaf->_data.size() / 2;
                auto* right_leaf = create_terminal_node();
                right_leaf->_data.assign(current_leaf->_data.begin() + static_cast<std::ptrdiff_t>(split_index), current_leaf->_data.end());
                current_leaf->_data.erase(current_leaf->_data.begin() + static_cast<std::ptrdiff_t>(split_index), current_leaf->_data.end());
                right_leaf->_next = current_leaf->_next;
                current_leaf->_next = right_leaf;
                parent->_keys.insert(parent->_keys.begin() + static_cast<std::ptrdiff_t>(parent_index), right_leaf->_data.front().first);
                parent->_pointers.insert(parent->_pointers.begin() + static_cast<std::ptrdiff_t>(parent_index + 1), right_leaf);
            } else {
                auto* current_middle = as_middle(current_node);
                const std::size_t middle_index = current_middle->_keys.size() / 2;
                const tkey promoted_key = current_middle->_keys[middle_index];
                auto* right_middle = create_middle_node();
                right_middle->_keys.assign(current_middle->_keys.begin() + static_cast<std::ptrdiff_t>(middle_index + 1), current_middle->_keys.end());
                right_middle->_pointers.assign(current_middle->_pointers.begin() + static_cast<std::ptrdiff_t>(middle_index + 1), current_middle->_pointers.end());
                current_middle->_keys.erase(current_middle->_keys.begin() + static_cast<std::ptrdiff_t>(middle_index), current_middle->_keys.end());
                current_middle->_pointers.erase(current_middle->_pointers.begin() + static_cast<std::ptrdiff_t>(middle_index + 1), current_middle->_pointers.end());
                parent->_keys.insert(parent->_keys.begin() + static_cast<std::ptrdiff_t>(parent_index), promoted_key);
                parent->_pointers.insert(parent->_pointers.begin() + static_cast<std::ptrdiff_t>(parent_index + 1), right_middle);
            }

            current_node = parent;
            current_is_leaf = false;
        }

        return locate_key(key);
    }

    template <typename pair_type>
    std::pair<bsptree_iterator, bool> insert_impl(pair_type&& data) {
        if (_root == nullptr) {
            auto* leaf = create_terminal_node();
            leaf->_data.push_back(std::forward<pair_type>(data));
            _root = leaf;
            _size = 1;
            return {make_iterator(leaf, 0), true};
        }

        const tkey key = data.first;
        auto [leaf, path] = locate_leaf_for_insert(key);
        const std::size_t index = lower_bound_index(leaf, key);

        if (index < leaf->_data.size() && keys_equal(leaf->_data[index].first, key)) {
            return {make_iterator(leaf, index), false};
        }

        auto [new_leaf, new_index] = insert_new(std::forward<pair_type>(data));
        ++_size;
        return {make_iterator(new_leaf, new_index), true};
    }

    void create_new_root(bsptree_node_base* left, const tkey& separator_key, bsptree_node_base* right) {
        auto* new_root = create_middle_node();
        new_root->_keys.push_back(separator_key);
        new_root->_pointers.push_back(left);
        new_root->_pointers.push_back(right);
        _root = new_root;
    }

    void refresh_separator_after_subtree_change(path_type& path, const bsptree_node_base* node) {
        if (node == nullptr) {
            return;
        }

        if (is_terminal(node) && as_terminal(node)->_data.empty()) {
            return;
        }

        const bsptree_node_base* current = node;
        for (auto it = path.rbegin(); it != path.rend(); ++it) {
            auto* parent = it->first;
            const std::size_t child_index = it->second;
            if (child_index > 0) {
                parent->_keys[child_index - 1] = separator_key_for_subtree(current);
            }
            current = parent;
        }
    }

    void refresh_separator_after_leaf_change(path_type& path, const bsptree_node_term* leaf) {
        refresh_separator_after_subtree_change(path, leaf);
    }

    [[nodiscard]] tkey separator_key_for_subtree(const bsptree_node_base* node) const {
        assert(node != nullptr);

        const bsptree_node_base* current = node;
        while (!is_terminal(current)) {
            current = as_middle(current)->_pointers.front();
        }

        const auto* leaf = as_terminal(current);
        assert(!leaf->_data.empty());
        return leaf->_data.front().first;
    }

    [[nodiscard]] bool erase_from_leaf(bsptree_node_term* leaf, std::size_t index) {
        if (leaf == nullptr || index >= leaf->_data.size()) {
            return false;
        }

        leaf->_data.erase(leaf->_data.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    bsptree_iterator erase_and_get_next(const tkey& key) {
        auto [leaf, path] = locate_leaf_for_insert(key);
        if (leaf == nullptr) {
            return make_end_iterator();
        }

        const std::size_t index = lower_bound_index(leaf, key);
        if (index >= leaf->_data.size() || !keys_equal(leaf->_data[index].first, key)) {
            return make_end_iterator();
        }

        const bool has_next_in_leaf = index + 1 < leaf->_data.size();
        const bool has_next_leaf = leaf->_next != nullptr;
        const bool has_next = has_next_in_leaf || has_next_leaf;
        const tkey next_key = has_next_in_leaf
            ? leaf->_data[index + 1].first
            : (has_next_leaf ? leaf->_next->_data.front().first : tkey{});

        if (!erase_from_leaf(leaf, index)) {
            return make_end_iterator();
        }

        --_size;

        if (leaf == _root) {
            if (leaf->_data.empty()) {
                destroy_node(_root);
                _root = nullptr;
            }
            return has_next ? lower_bound(next_key) : make_end_iterator();
        }

        if (!leaf->_data.empty()) {
            refresh_separator_after_leaf_change(path, leaf);
        }

        if (leaf->_data.size() < minimum_keys_in_node) {
            rebalance_after_erase(path, leaf);
        } else {
            collapse_root_if_needed();
        }

        return has_next ? lower_bound(next_key) : make_end_iterator();
    }

    void rebalance_after_erase(path_type& path, bsptree_node_base* node) {
        bsptree_node_base* current = node;

        while (current != _root) {
            const std::size_t key_count = is_terminal(current)
                ? as_terminal(current)->_data.size()
                : as_middle(current)->_keys.size();

            if (key_count >= minimum_keys_in_node) {
                refresh_separator_after_subtree_change(path, current);
                break;
            }

            if (!path.empty()) {
                auto* borrowed = borrow_from_left(path, current);
                if (borrowed != nullptr) {
                    current = borrowed;
                    refresh_separator_after_subtree_change(path, current);
                    break;
                }

                borrowed = borrow_from_right(path, current);
                if (borrowed != nullptr) {
                    current = borrowed;
                    refresh_separator_after_subtree_change(path, current);
                    break;
                }
            }

            if (path.empty()) {
                break;
            }

            const std::size_t child_index = path.back().second;
            if (child_index > 0) {
                merge_with_left(path, current);
            } else {
                merge_with_right(path, current);
            }

            current = path.back().first;
            path.pop_back();
        }

        collapse_root_if_needed();
    }

    [[nodiscard]] bsptree_node_base* borrow_from_left(path_type& path, bsptree_node_base* node) {
        if (path.empty()) {
            return nullptr;
        }

        auto [parent, child_index] = path.back();
        if (child_index == 0) {
            return nullptr;
        }

        bsptree_node_base* left_base = parent->_pointers[child_index - 1];

        if (is_terminal(node)) {
            auto* current = as_terminal(node);
            auto* left = as_terminal(left_base);
            if (left->_data.size() <= minimum_keys_in_node) {
                return nullptr;
            }

            current->_data.insert(current->_data.begin(), std::move(left->_data.back()));
            left->_data.pop_back();
            parent->_keys[child_index - 1] = current->_data.front().first;
            return node;
        }

        auto* current = as_middle(node);
        auto* left = as_middle(left_base);
        if (left->_keys.size() <= minimum_keys_in_node) {
            return nullptr;
        }

        bsptree_node_base* borrowed_child = left->_pointers.back();
        left->_pointers.pop_back();
        current->_pointers.insert(current->_pointers.begin(), borrowed_child);
        current->_keys.insert(current->_keys.begin(), separator_key_for_subtree(current->_pointers[1]));
        left->_keys.pop_back();
        parent->_keys[child_index - 1] = separator_key_for_subtree(current);
        return node;
    }

    [[nodiscard]] bsptree_node_base* borrow_from_right(path_type& path, bsptree_node_base* node) {
        if (path.empty()) {
            return nullptr;
        }

        auto [parent, child_index] = path.back();
        if (child_index + 1 >= parent->_pointers.size()) {
            return nullptr;
        }

        bsptree_node_base* right_base = parent->_pointers[child_index + 1];

        if (is_terminal(node)) {
            auto* current = as_terminal(node);
            auto* right = as_terminal(right_base);
            if (right->_data.size() <= minimum_keys_in_node) {
                return nullptr;
            }

            current->_data.push_back(std::move(right->_data.front()));
            right->_data.erase(right->_data.begin());
            parent->_keys[child_index] = right->_data.front().first;
            return node;
        }

        auto* current = as_middle(node);
        auto* right = as_middle(right_base);
        if (right->_keys.size() <= minimum_keys_in_node) {
            return nullptr;
        }

        bsptree_node_base* borrowed_child = right->_pointers.front();
        current->_pointers.push_back(borrowed_child);
        current->_keys.push_back(separator_key_for_subtree(borrowed_child));
        right->_pointers.erase(right->_pointers.begin());
        right->_keys.erase(right->_keys.begin());
        parent->_keys[child_index] = separator_key_for_subtree(right);
        return node;
    }

    void merge_with_left(path_type& path, bsptree_node_base* node) {
        assert(!path.empty());

        auto [parent, child_index] = path.back();
        assert(child_index > 0);

        bsptree_node_base* left_base = parent->_pointers[child_index - 1];

        if (is_terminal(node)) {
            auto* current = as_terminal(node);
            auto* left = as_terminal(left_base);
            left->_data.insert(left->_data.end(), current->_data.begin(), current->_data.end());
            left->_next = current->_next;
        } else {
            auto* current = as_middle(node);
            auto* left = as_middle(left_base);
            left->_keys.push_back(parent->_keys[child_index - 1]);
            left->_keys.insert(left->_keys.end(), current->_keys.begin(), current->_keys.end());
            left->_pointers.insert(left->_pointers.end(), current->_pointers.begin(), current->_pointers.end());
        }

        parent->_keys.erase(parent->_keys.begin() + static_cast<std::ptrdiff_t>(child_index - 1));
        parent->_pointers.erase(parent->_pointers.begin() + static_cast<std::ptrdiff_t>(child_index));
        destroy_node(node);
    }

    void merge_with_right(path_type& path, bsptree_node_base* node) {
        assert(!path.empty());

        auto [parent, child_index] = path.back();
        assert(child_index + 1 < parent->_pointers.size());

        bsptree_node_base* right_base = parent->_pointers[child_index + 1];

        if (is_terminal(node)) {
            auto* current = as_terminal(node);
            auto* right = as_terminal(right_base);
            current->_data.insert(current->_data.end(), right->_data.begin(), right->_data.end());
            current->_next = right->_next;
        } else {
            auto* current = as_middle(node);
            auto* right = as_middle(right_base);
            current->_keys.push_back(parent->_keys[child_index]);
            current->_keys.insert(current->_keys.end(), right->_keys.begin(), right->_keys.end());
            current->_pointers.insert(current->_pointers.end(), right->_pointers.begin(), right->_pointers.end());
        }

        parent->_keys.erase(parent->_keys.begin() + static_cast<std::ptrdiff_t>(child_index));
        parent->_pointers.erase(parent->_pointers.begin() + static_cast<std::ptrdiff_t>(child_index + 1));
        destroy_node(right_base);
    }

    void collapse_root_if_needed() {
        if (_root == nullptr) {
            return;
        }

        if (is_terminal(_root)) {
            if (as_terminal(_root)->_data.empty()) {
                destroy_node(_root);
                _root = nullptr;
            }
            return;
        }

        auto* root = as_middle(_root);
        if (root->_keys.empty() && !root->_pointers.empty()) {
            bsptree_node_base* new_root = root->_pointers.front();
            root->_pointers.clear();
            destroy_node(root);
            _root = new_root;
        }
    }

    [[nodiscard]] bsptree_iterator lower_bound_impl(const tkey& key) {
        if (_root == nullptr) {
            return make_end_iterator();
        }

        auto [leaf, path] = locate_leaf_for_read(key);
        static_cast<void>(path);
        if (leaf == nullptr) {
            return make_end_iterator();
        }

        const std::size_t index = lower_bound_index(leaf, key);
        if (index < leaf->_data.size()) {
            return make_iterator(const_cast<bsptree_node_term*>(leaf), index);
        }

        return leaf->_next == nullptr ? make_end_iterator() : make_iterator(leaf->_next, 0);
    }

    [[nodiscard]] bsptree_const_iterator lower_bound_impl(const tkey& key) const {
        if (_root == nullptr) {
            return make_end_iterator();
        }

        auto [leaf, path] = locate_leaf_for_read(key);
        static_cast<void>(path);
        if (leaf == nullptr) {
            return make_end_iterator();
        }

        const std::size_t index = lower_bound_index(leaf, key);
        if (index < leaf->_data.size()) {
            return make_const_iterator(leaf, index);
        }

        return leaf->_next == nullptr ? make_end_iterator() : make_const_iterator(leaf->_next, 0);
    }

    [[nodiscard]] bsptree_iterator upper_bound_impl(const tkey& key) {
        if (_root == nullptr) {
            return make_end_iterator();
        }

        auto [leaf, path] = locate_leaf_for_read(key);
        static_cast<void>(path);
        if (leaf == nullptr) {
            return make_end_iterator();
        }

        const std::size_t index = upper_bound_index(leaf, key);
        if (index < leaf->_data.size()) {
            return make_iterator(const_cast<bsptree_node_term*>(leaf), index);
        }

        return leaf->_next == nullptr ? make_end_iterator() : make_iterator(leaf->_next, 0);
    }

    [[nodiscard]] bsptree_const_iterator upper_bound_impl(const tkey& key) const {
        if (_root == nullptr) {
            return make_end_iterator();
        }

        auto [leaf, path] = locate_leaf_for_read(key);
        static_cast<void>(path);
        if (leaf == nullptr) {
            return make_end_iterator();
        }

        const std::size_t index = upper_bound_index(leaf, key);
        if (index < leaf->_data.size()) {
            return make_const_iterator(leaf, index);
        }

        return leaf->_next == nullptr ? make_end_iterator() : make_const_iterator(leaf->_next, 0);
    }

    [[nodiscard]] bsptree_iterator find_impl(const tkey& key) {
        auto [leaf, index] = locate_key(key);
        return leaf == nullptr ? make_end_iterator() : make_iterator(leaf, index);
    }

    [[nodiscard]] bsptree_const_iterator find_impl(const tkey& key) const {
        auto [leaf, index] = locate_key(key);
        return leaf == nullptr ? make_end_iterator() : make_const_iterator(leaf, index);
    }

public:
    explicit BSP_tree(const compare& cmp = compare(), allocator_type alloc = allocator_type())
        : compare(cmp),
          _allocator(alloc),
          _root(nullptr),
          _size(0)
    {}

    explicit BSP_tree(allocator_type alloc, const compare& comp = compare())
        : compare(comp),
          _allocator(alloc),
          _root(nullptr),
          _size(0)
    {}

    template <input_iterator_for_pair<tkey, tvalue> iterator>
    explicit BSP_tree(iterator begin, iterator end, const compare& cmp = compare(), allocator_type alloc = allocator_type())
        : BSP_tree(cmp, alloc)
    {
        for (; begin != end; ++begin) {
            insert(*begin);
        }
    }

    BSP_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), allocator_type alloc = allocator_type())
        : BSP_tree(cmp, alloc)
    {
        for (const auto& item : data) {
            insert(item);
        }
    }

    BSP_tree(const BSP_tree& other)
        : BSP_tree(static_cast<const compare&>(other), other.get_allocator().select_on_container_copy_construction())
    {
        for (auto it = other.cbegin(); it != other.cend(); ++it) {
            insert(*it);
        }
    }

    BSP_tree(BSP_tree&& other) noexcept
        : compare(static_cast<compare&&>(other)),
          _allocator(std::move(other._allocator)),
          _root(other._root),
          _size(other._size)
    {
        other._root = nullptr;
        other._size = 0;
    }

    BSP_tree& operator=(const BSP_tree& other) {
        if (this == &other) {
            return *this;
        }

        BSP_tree copy(other);
        swap(copy);
        return *this;
    }

    BSP_tree& operator=(BSP_tree&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        clear();
        static_cast<compare&>(*this) = static_cast<compare&&>(other);
        _allocator = std::move(other._allocator);
        _root = other._root;
        _size = other._size;
        other._root = nullptr;
        other._size = 0;
        return *this;
    }

    ~BSP_tree() noexcept {
        clear();
    }

    class bsptree_iterator final
    {
        BSP_tree* _owner;
        bsptree_node_term* _node;
        std::size_t _index;

    public:
        using value_type = tree_data_type_const;
        using reference = tree_data_type&;
        using pointer = tree_data_type*;
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using self = bsptree_iterator;

        friend class BSP_tree;
        friend class bsptree_const_iterator;

        explicit bsptree_iterator(BSP_tree* owner = nullptr, bsptree_node_term* node = nullptr, std::size_t index = 0) noexcept
            : _owner(owner),
              _node(node),
              _index(index)
        {}

        reference operator*() const noexcept {
            return _node->_data[_index];
        }

        pointer operator->() const noexcept {
            return &_node->_data[_index];
        }

        self& operator++() {
            if (_node == nullptr) {
                return *this;
            }

            ++_index;
            if (_index >= _node->_data.size()) {
                _node = _node->_next;
                _index = 0;
            }

            return *this;
        }

        self operator++(int) {
            self copy = *this;
            ++(*this);
            return copy;
        }

        bool operator==(const self& other) const noexcept {
            return _node == other._node && _index == other._index;
        }

        bool operator!=(const self& other) const noexcept {
            return !(*this == other);
        }

        std::size_t current_node_keys_count() const noexcept {
            return _node == nullptr ? 0 : _node->_data.size();
        }

        std::size_t index() const noexcept {
            return _index;
        }
    };

    class bsptree_const_iterator final
    {
        const BSP_tree* _owner;
        const bsptree_node_term* _node;
        std::size_t _index;

    public:
        using value_type = tree_data_type_const;
        using reference = const tree_data_type&;
        using pointer = const tree_data_type*;
        using iterator_category = std::forward_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using self = bsptree_const_iterator;

        friend class BSP_tree;
        friend class bsptree_iterator;

        explicit bsptree_const_iterator(const BSP_tree* owner = nullptr, const bsptree_node_term* node = nullptr, std::size_t index = 0) noexcept
            : _owner(owner),
              _node(node),
              _index(index)
        {}

        bsptree_const_iterator(const bsptree_iterator& it) noexcept
            : _owner(it._owner),
              _node(it._node),
              _index(it._index)
        {}

        reference operator*() const noexcept {
            return _node->_data[_index];
        }

        pointer operator->() const noexcept {
            return &_node->_data[_index];
        }

        self& operator++() {
            if (_node == nullptr) {
                return *this;
            }

            ++_index;
            if (_index >= _node->_data.size()) {
                _node = _node->_next;
                _index = 0;
            }

            return *this;
        }

        self operator++(int) {
            self copy = *this;
            ++(*this);
            return copy;
        }

        bool operator==(const self& other) const noexcept {
            return _node == other._node && _index == other._index;
        }

        bool operator!=(const self& other) const noexcept {
            return !(*this == other);
        }

        std::size_t current_node_keys_count() const noexcept {
            return _node == nullptr ? 0 : _node->_data.size();
        }

        std::size_t index() const noexcept {
            return _index;
        }
    };

    friend class bsptree_iterator;
    friend class bsptree_const_iterator;

    tvalue& at(const tkey& key) {
        auto [leaf, index] = locate_key(key);
        if (leaf == nullptr) {
            throw std::out_of_range("BSP_tree::at key not found");
        }
        return leaf->_data[index].second;
    }

    const tvalue& at(const tkey& key) const {
        auto [leaf, index] = locate_key(key);
        if (leaf == nullptr) {
            throw std::out_of_range("BSP_tree::at key not found");
        }
        return leaf->_data[index].second;
    }

    tvalue& operator[](const tkey& key) {
        auto [it, inserted] = emplace(key, tvalue{});
        static_cast<void>(inserted);
        return it->second;
    }

    tvalue& operator[](tkey&& key) {
        auto [it, inserted] = emplace(std::move(key), tvalue{});
        static_cast<void>(inserted);
        return it->second;
    }

    bsptree_iterator begin() {
        auto* leaf = leftmost_leaf();
        return leaf == nullptr ? make_end_iterator() : make_iterator(leaf, 0);
    }

    bsptree_iterator end() {
        return make_end_iterator();
    }

    bsptree_const_iterator begin() const {
        auto* leaf = leftmost_leaf();
        return leaf == nullptr ? make_end_iterator() : make_const_iterator(leaf, 0);
    }

    bsptree_const_iterator end() const {
        return make_end_iterator();
    }

    bsptree_const_iterator cbegin() const {
        return begin();
    }

    bsptree_const_iterator cend() const {
        return end();
    }

    std::size_t size() const noexcept {
        return _size;
    }

    bool empty() const noexcept {
        return _size == 0;
    }

    bsptree_iterator find(const tkey& key) {
        return find_impl(key);
    }

    bsptree_const_iterator find(const tkey& key) const {
        return find_impl(key);
    }

    bsptree_iterator lower_bound(const tkey& key) {
        return lower_bound_impl(key);
    }

    bsptree_const_iterator lower_bound(const tkey& key) const {
        return lower_bound_impl(key);
    }

    bsptree_iterator upper_bound(const tkey& key) {
        return upper_bound_impl(key);
    }

    bsptree_const_iterator upper_bound(const tkey& key) const {
        return upper_bound_impl(key);
    }

    bool contains(const tkey& key) const {
        return find(key) != cend();
    }

    void clear() noexcept {
        clear_subtree(_root);
        _root = nullptr;
        _size = 0;
    }

    std::pair<bsptree_iterator, bool> insert(const tree_data_type& data) {
        return insert_impl(data);
    }

    std::pair<bsptree_iterator, bool> insert(tree_data_type&& data) {
        return insert_impl(std::move(data));
    }

    template <typename ...Args>
    std::pair<bsptree_iterator, bool> emplace(Args&&... args) {
        return insert_impl(tree_data_type(std::forward<Args>(args)...));
    }

    bsptree_iterator insert_or_assign(const tree_data_type& data) {
        auto [it, inserted] = insert(data);
        if (!inserted) {
            it->second = data.second;
        }
        return it;
    }

    bsptree_iterator insert_or_assign(tree_data_type&& data) {
        auto [it, inserted] = insert(std::move(data));
        if (!inserted) {
            it->second = std::move(data.second);
        }
        return it;
    }

    template <typename ...Args>
    bsptree_iterator emplace_or_assign(Args&&... args) {
        tree_data_type data(std::forward<Args>(args)...);
        auto found = find(data.first);
        if (found != make_end_iterator()) {
            found->second = data.second;
            return found;
        }
        return insert(std::move(data)).first;
    }

    bsptree_iterator erase(bsptree_iterator pos) {
        if (pos == make_end_iterator()) {
            return make_end_iterator();
        }
        return erase_and_get_next(pos->first);
    }

    bsptree_iterator erase(bsptree_const_iterator pos) {
        return erase(bsptree_iterator(this, const_cast<bsptree_node_term*>(pos._node), pos._index));
    }

    bsptree_iterator erase(bsptree_iterator beg, bsptree_iterator en) {
        auto it = beg;
        while (it != en) {
            it = erase(it);
        }
        return it;
    }

    bsptree_iterator erase(bsptree_const_iterator beg, bsptree_const_iterator en) {
        return erase(
            bsptree_iterator(this, const_cast<bsptree_node_term*>(beg._node), beg._index),
            bsptree_iterator(this, const_cast<bsptree_node_term*>(en._node), en._index));
    }

    bsptree_iterator erase(const tkey& key) {
        auto found = find(key);
        if (found == make_end_iterator()) {
            return make_end_iterator();
        }
        return erase(found);
    }
};

template<std::input_iterator iterator, comparator<typename std::iterator_traits<iterator>::value_type::first_type> compare = std::less<typename std::iterator_traits<iterator>::value_type::first_type>,
    std::size_t t = 5, typename U>
BSP_tree(iterator begin, iterator end, const compare& cmp = compare(), pp_allocator<U> = pp_allocator<U>())
    -> BSP_tree<typename std::iterator_traits<iterator>::value_type::first_type, typename std::iterator_traits<iterator>::value_type::second_type, compare, t>;

template<typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5, typename U>
BSP_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), pp_allocator<U> = pp_allocator<U>())
    -> BSP_tree<tkey, tvalue, compare, t>;

#endif
