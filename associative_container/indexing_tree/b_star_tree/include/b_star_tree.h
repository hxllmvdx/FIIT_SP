#ifndef SYS_PROG_BS_TREE_H
#define SYS_PROG_BS_TREE_H

#include <algorithm>
#include <cassert>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

#include <associative_container.h>
#include <pp_allocator.h>

template <typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5>
class BS_tree final : private compare
{
    static_assert(t >= 2, "BS_tree minimum degree must be at least 2");

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

    struct bstree_node
    {
        std::vector<tree_data_type> keys;
        std::vector<bstree_node*> children;

        bstree_node()
        {
            keys.reserve(maximum_keys_in_node + 1);
            children.reserve(maximum_keys_in_node + 2);
        }

        [[nodiscard]] bool is_leaf() const noexcept
        {
            return children.empty();
        }
    };

    using path_entry = std::pair<bstree_node*, std::size_t>;
    using path_type = std::vector<path_entry>;

    allocator_type _allocator;
    bstree_node* _root;
    std::size_t _size;

    [[nodiscard]] bool compare_keys(const tkey& lhs, const tkey& rhs) const
    {
        return compare::operator()(lhs, rhs);
    }

    [[nodiscard]] bool keys_equal(const tkey& lhs, const tkey& rhs) const
    {
        return !compare_keys(lhs, rhs) && !compare_keys(rhs, lhs);
    }

    [[nodiscard]] allocator_type get_allocator() const noexcept
    {
        return _allocator;
    }

    [[nodiscard]] bstree_node* create_node()
    {
        return _allocator.template new_object<bstree_node>();
    }

    void destroy_node(bstree_node* node) noexcept
    {
        if (node != nullptr) {
            _allocator.template delete_object<bstree_node>(node);
        }
    }

    void clear_subtree(bstree_node* node) noexcept
    {
        if (node == nullptr) {
            return;
        }

        for (bstree_node* child : node->children) {
            clear_subtree(child);
        }

        destroy_node(node);
    }

    [[nodiscard]] std::size_t lower_bound_index(const bstree_node* node, const tkey& key) const
    {
        std::size_t left = 0;
        std::size_t right = node->keys.size();
        while (left < right) {
            const std::size_t middle = left + (right - left) / 2;
            if (compare_keys(node->keys[middle].first, key)) {
                left = middle + 1;
            } else {
                right = middle;
            }
        }
        return left;
    }

    [[nodiscard]] std::size_t upper_bound_index(const bstree_node* node, const tkey& key) const
    {
        std::size_t index = lower_bound_index(node, key);
        while (index < node->keys.size() && keys_equal(node->keys[index].first, key)) {
            ++index;
        }
        return index;
    }

    [[nodiscard]] std::pair<bstree_node*, std::size_t> locate_key(const tkey& key) const
    {
        bstree_node* current = _root;
        while (current != nullptr) {
            const std::size_t index = lower_bound_index(current, key);
            if (index < current->keys.size() && keys_equal(current->keys[index].first, key)) {
                return {current, index};
            }

            if (current->is_leaf()) {
                break;
            }

            current = current->children[index];
        }

        return {nullptr, 0};
    }

    void assign_keys_range(
        bstree_node* node,
        std::vector<tree_data_type>& source,
        std::size_t begin,
        std::size_t end)
    {
        node->keys.clear();
        for (std::size_t index = begin; index < end; ++index) {
            node->keys.push_back(std::move(source[index]));
        }
    }

    void assign_children_range(
        bstree_node* node,
        const std::vector<bstree_node*>& source,
        std::size_t begin,
        std::size_t end)
    {
        node->children.clear();
        for (std::size_t index = begin; index < end; ++index) {
            node->children.push_back(source[index]);
        }
    }

    void split_child_into_parent(bstree_node* parent, std::size_t parent_index, bstree_node* current)
    {
        const std::size_t middle_index = current->keys.size() / 2;
        tree_data_type middle_key = std::move(current->keys[middle_index]);

        bstree_node* right = create_node();
        right->keys.assign(
            std::make_move_iterator(current->keys.begin() + static_cast<std::ptrdiff_t>(middle_index + 1)),
            std::make_move_iterator(current->keys.end()));
        current->keys.resize(middle_index);

        if (!current->is_leaf()) {
            right->children.assign(
                std::make_move_iterator(current->children.begin() + static_cast<std::ptrdiff_t>(middle_index + 1)),
                std::make_move_iterator(current->children.end()));
            current->children.resize(middle_index + 1);
        }

        parent->keys.insert(parent->keys.begin() + static_cast<std::ptrdiff_t>(parent_index), std::move(middle_key));
        parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(parent_index + 1), right);
    }

    [[nodiscard]] bool redistribute_with_left_sibling(bstree_node* parent, std::size_t child_index)
    {
        if (child_index == 0) {
            return false;
        }

        bstree_node* left = parent->children[child_index - 1];
        bstree_node* current = parent->children[child_index];
        if (left->keys.size() >= maximum_keys_in_node) {
            return false;
        }

        std::vector<tree_data_type> combined_keys;
        combined_keys.reserve(left->keys.size() + 1 + current->keys.size());
        for (auto& key : left->keys) {
            combined_keys.push_back(std::move(key));
        }
        combined_keys.push_back(std::move(parent->keys[child_index - 1]));
        for (auto& key : current->keys) {
            combined_keys.push_back(std::move(key));
        }

        std::vector<bstree_node*> combined_children;
        if (!left->is_leaf()) {
            combined_children.reserve(left->children.size() + current->children.size());
            combined_children.insert(combined_children.end(), left->children.begin(), left->children.end());
            combined_children.insert(combined_children.end(), current->children.begin(), current->children.end());
        }

        const std::size_t left_size = combined_keys.size() / 2;
        const std::size_t separator_index = left_size;

        assign_keys_range(left, combined_keys, 0, left_size);
        parent->keys[child_index - 1] = std::move(combined_keys[separator_index]);
        assign_keys_range(current, combined_keys, separator_index + 1, combined_keys.size());

        if (!combined_children.empty()) {
            assign_children_range(left, combined_children, 0, left_size + 1);
            assign_children_range(current, combined_children, left_size + 1, combined_children.size());
        }

        return true;
    }

    [[nodiscard]] bool redistribute_with_right_sibling(bstree_node* parent, std::size_t child_index)
    {
        if (child_index + 1 >= parent->children.size()) {
            return false;
        }

        bstree_node* current = parent->children[child_index];
        bstree_node* right = parent->children[child_index + 1];
        if (right->keys.size() >= maximum_keys_in_node) {
            return false;
        }

        std::vector<tree_data_type> combined_keys;
        combined_keys.reserve(current->keys.size() + 1 + right->keys.size());
        for (auto& key : current->keys) {
            combined_keys.push_back(std::move(key));
        }
        combined_keys.push_back(std::move(parent->keys[child_index]));
        for (auto& key : right->keys) {
            combined_keys.push_back(std::move(key));
        }

        std::vector<bstree_node*> combined_children;
        if (!current->is_leaf()) {
            combined_children.reserve(current->children.size() + right->children.size());
            combined_children.insert(combined_children.end(), current->children.begin(), current->children.end());
            combined_children.insert(combined_children.end(), right->children.begin(), right->children.end());
        }

        const std::size_t current_size = combined_keys.size() / 2;
        const std::size_t separator_index = current_size;

        assign_keys_range(current, combined_keys, 0, current_size);
        parent->keys[child_index] = std::move(combined_keys[separator_index]);
        assign_keys_range(right, combined_keys, separator_index + 1, combined_keys.size());

        if (!combined_children.empty()) {
            assign_children_range(current, combined_children, 0, current_size + 1);
            assign_children_range(right, combined_children, current_size + 1, combined_children.size());
        }

        return true;
    }

    [[nodiscard]] bool redistribute_overflow(bstree_node* parent, std::size_t child_index)
    {
        return redistribute_with_right_sibling(parent, child_index);
    }

    void split_two_children_into_three(bstree_node* parent, std::size_t left_child_index)
    {
        bstree_node* left = parent->children[left_child_index];
        bstree_node* right = parent->children[left_child_index + 1];

        std::vector<tree_data_type> combined_keys;
        combined_keys.reserve(left->keys.size() + 1 + right->keys.size());
        for (auto& key : left->keys) {
            combined_keys.push_back(std::move(key));
        }
        combined_keys.push_back(std::move(parent->keys[left_child_index]));
        for (auto& key : right->keys) {
            combined_keys.push_back(std::move(key));
        }

        std::vector<bstree_node*> combined_children;
        if (!left->is_leaf()) {
            combined_children.reserve(left->children.size() + right->children.size());
            combined_children.insert(combined_children.end(), left->children.begin(), left->children.end());
            combined_children.insert(combined_children.end(), right->children.begin(), right->children.end());
        }

        const std::size_t distributable_keys = combined_keys.size() - 2;
        const std::size_t base_size = distributable_keys / 3;
        const std::size_t remainder = distributable_keys % 3;

        const std::size_t left_size = base_size + (remainder > 0 ? 1 : 0);
        const std::size_t middle_size = base_size + (remainder > 1 ? 1 : 0);
        const std::size_t first_separator_index = left_size;
        const std::size_t second_separator_index = left_size + 1 + middle_size;

        bstree_node* middle = create_node();

        assign_keys_range(left, combined_keys, 0, left_size);
        parent->keys[left_child_index] = std::move(combined_keys[first_separator_index]);
        assign_keys_range(middle, combined_keys, first_separator_index + 1, second_separator_index);
        tree_data_type promoted_key = std::move(combined_keys[second_separator_index]);
        assign_keys_range(right, combined_keys, second_separator_index + 1, combined_keys.size());

        if (!combined_children.empty()) {
            assign_children_range(left, combined_children, 0, left_size + 1);
            assign_children_range(middle, combined_children, left_size + 1, left_size + middle_size + 2);
            assign_children_range(right, combined_children, left_size + middle_size + 2, combined_children.size());
        }

        parent->keys.insert(parent->keys.begin() + static_cast<std::ptrdiff_t>(left_child_index + 1), std::move(promoted_key));
        parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(left_child_index + 1), middle);
    }

    template <typename pair_type>
    std::pair<bstree_node*, std::size_t> insert_new(pair_type&& data)
    {
        const tkey key_copy = data.first;

        if (_root == nullptr) {
            _root = create_node();
            _root->keys.emplace_back(std::forward<pair_type>(data));
            _size = 1;
            return {_root, 0};
        }

        path_type path;
        bstree_node* current = _root;

        while (!current->is_leaf()) {
            const std::size_t child_index = lower_bound_index(current, data.first);
            path.emplace_back(current, child_index);
            current = current->children[child_index];
        }

        const std::size_t inserted_index = lower_bound_index(current, data.first);
        current->keys.insert(current->keys.begin() + static_cast<std::ptrdiff_t>(inserted_index), std::forward<pair_type>(data));
        ++_size;

        while (current->keys.size() > maximum_keys_in_node) {
            if (path.empty()) {
                const std::size_t middle_index = current->keys.size() / 2;
                tree_data_type middle_key = std::move(current->keys[middle_index]);

                bstree_node* right = create_node();
                right->keys.assign(
                    std::make_move_iterator(current->keys.begin() + static_cast<std::ptrdiff_t>(middle_index + 1)),
                    std::make_move_iterator(current->keys.end()));
                current->keys.resize(middle_index);

                if (!current->is_leaf()) {
                    right->children.assign(
                        std::make_move_iterator(current->children.begin() + static_cast<std::ptrdiff_t>(middle_index + 1)),
                        std::make_move_iterator(current->children.end()));
                    current->children.resize(middle_index + 1);
                }

                bstree_node* new_root = create_node();
                new_root->keys.push_back(std::move(middle_key));
                new_root->children.push_back(current);
                new_root->children.push_back(right);
                _root = new_root;
                break;
            }

            auto [parent, parent_index] = path.back();
            path.pop_back();

            if (redistribute_overflow(parent, parent_index)) {
                break;
            }

            if (parent->children.size() > 1) {
                const std::size_t left_child_index =
                    parent_index + 1 < parent->children.size()
                    ? parent_index
                    : parent_index - 1;
                split_two_children_into_three(parent, left_child_index);
                current = parent;
                continue;
            }

            split_child_into_parent(parent, parent_index, current);
            current = parent;
        }

        return locate_key(key_copy);
    }

    [[nodiscard]] tree_data_type& maximum_entry(bstree_node* node) const
    {
        assert(node != nullptr);
        while (!node->is_leaf()) {
            node = node->children.back();
        }
        return node->keys.back();
    }

    [[nodiscard]] tree_data_type& minimum_entry(bstree_node* node) const
    {
        assert(node != nullptr);
        while (!node->is_leaf()) {
            node = node->children.front();
        }
        return node->keys.front();
    }

    void borrow_from_left(bstree_node* parent, std::size_t child_index)
    {
        bstree_node* child = parent->children[child_index];
        bstree_node* left = parent->children[child_index - 1];

        child->keys.insert(child->keys.begin(), std::move(parent->keys[child_index - 1]));
        parent->keys[child_index - 1] = std::move(left->keys.back());
        left->keys.pop_back();

        if (!left->is_leaf()) {
            child->children.insert(child->children.begin(), left->children.back());
            left->children.pop_back();
        }
    }

    void borrow_from_right(bstree_node* parent, std::size_t child_index)
    {
        bstree_node* child = parent->children[child_index];
        bstree_node* right = parent->children[child_index + 1];

        child->keys.push_back(std::move(parent->keys[child_index]));
        parent->keys[child_index] = std::move(right->keys.front());
        right->keys.erase(right->keys.begin());

        if (!right->is_leaf()) {
            child->children.push_back(right->children.front());
            right->children.erase(right->children.begin());
        }
    }

    void merge_children(bstree_node* parent, std::size_t key_index)
    {
        bstree_node* left = parent->children[key_index];
        bstree_node* right = parent->children[key_index + 1];

        left->keys.push_back(std::move(parent->keys[key_index]));
        left->keys.insert(
            left->keys.end(),
            std::make_move_iterator(right->keys.begin()),
            std::make_move_iterator(right->keys.end()));

        if (!right->is_leaf()) {
            left->children.insert(
                left->children.end(),
                std::make_move_iterator(right->children.begin()),
                std::make_move_iterator(right->children.end()));
        }

        parent->keys.erase(parent->keys.begin() + static_cast<std::ptrdiff_t>(key_index));
        parent->children.erase(parent->children.begin() + static_cast<std::ptrdiff_t>(key_index + 1));
        destroy_node(right);
    }

    bool erase_from_subtree(bstree_node* node, const tkey& key)
    {
        const std::size_t index = lower_bound_index(node, key);
        const bool key_in_node = index < node->keys.size() && keys_equal(node->keys[index].first, key);

        if (key_in_node) {
            if (node->is_leaf()) {
                node->keys.erase(node->keys.begin() + static_cast<std::ptrdiff_t>(index));
                return true;
            }

            bstree_node* left_child = node->children[index];
            bstree_node* right_child = node->children[index + 1];

            if (left_child->keys.size() >= t) {
                tree_data_type predecessor = maximum_entry(left_child);
                node->keys[index] = predecessor;
                return erase_from_subtree(left_child, predecessor.first);
            }

            if (right_child->keys.size() >= t) {
                tree_data_type successor = minimum_entry(right_child);
                node->keys[index] = successor;
                return erase_from_subtree(right_child, successor.first);
            }

            merge_children(node, index);
            return erase_from_subtree(node->children[index], key);
        }

        if (node->is_leaf()) {
            return false;
        }

        std::size_t child_index = index;
        bstree_node* child = node->children[child_index];

        if (child->keys.size() == minimum_keys_in_node) {
            if (child_index > 0 && node->children[child_index - 1]->keys.size() >= t) {
                borrow_from_left(node, child_index);
            } else if (child_index < node->keys.size() && node->children[child_index + 1]->keys.size() >= t) {
                borrow_from_right(node, child_index);
            } else {
                if (child_index < node->keys.size()) {
                    merge_children(node, child_index);
                } else {
                    merge_children(node, child_index - 1);
                    --child_index;
                }
            }
            child = node->children[child_index];
        }

        return erase_from_subtree(child, key);
    }

    template <bool IsConst>
    class basic_bstree_iterator;

public:
    using bstree_iterator = basic_bstree_iterator<false>;
    using bstree_const_iterator = basic_bstree_iterator<true>;

    class bstree_reverse_iterator final
    {
        bstree_iterator _base;

        friend class BS_tree;
        friend class bstree_const_reverse_iterator;

        explicit bstree_reverse_iterator(const bstree_iterator& base) noexcept : _base(base) {}

        [[nodiscard]] bstree_iterator previous() const
        {
            bstree_iterator tmp = _base;
            --tmp;
            return tmp;
        }

    public:
        using value_type = tree_data_type;
        using reference = value_type&;
        using pointer = value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using self = bstree_reverse_iterator;

        bstree_reverse_iterator() noexcept = default;

        operator bstree_iterator() const noexcept
        {
            return _base;
        }

        reference operator*() const noexcept
        {
            return *previous();
        }

        pointer operator->() const noexcept
        {
            return std::addressof(operator*());
        }

        self& operator++()
        {
            --_base;
            return *this;
        }

        self operator++(int)
        {
            self tmp = *this;
            ++(*this);
            return tmp;
        }

        self& operator--()
        {
            ++_base;
            return *this;
        }

        self operator--(int)
        {
            self tmp = *this;
            --(*this);
            return tmp;
        }

        [[nodiscard]] bool operator==(const self& other) const noexcept
        {
            return _base == other._base;
        }

        [[nodiscard]] bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }

        [[nodiscard]] std::size_t depth() const noexcept
        {
            return previous().depth();
        }

        [[nodiscard]] std::size_t current_node_keys_count() const noexcept
        {
            return previous().current_node_keys_count();
        }

        [[nodiscard]] bool is_terminate_node() const noexcept
        {
            return previous().is_terminate_node();
        }

        [[nodiscard]] std::size_t index() const noexcept
        {
            return previous().index();
        }
    };

    class bstree_const_reverse_iterator final
    {
        bstree_const_iterator _base;

        friend class BS_tree;

        explicit bstree_const_reverse_iterator(const bstree_const_iterator& base) noexcept : _base(base) {}

        [[nodiscard]] bstree_const_iterator previous() const
        {
            bstree_const_iterator tmp = _base;
            --tmp;
            return tmp;
        }

    public:
        using value_type = tree_data_type;
        using reference = const value_type&;
        using pointer = const value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using self = bstree_const_reverse_iterator;

        bstree_const_reverse_iterator() noexcept = default;

        bstree_const_reverse_iterator(const bstree_reverse_iterator& other) noexcept
            : _base(static_cast<bstree_iterator>(other))
        {
        }

        operator bstree_const_iterator() const noexcept
        {
            return _base;
        }

        reference operator*() const noexcept
        {
            return *previous();
        }

        pointer operator->() const noexcept
        {
            return std::addressof(operator*());
        }

        self& operator++()
        {
            --_base;
            return *this;
        }

        self operator++(int)
        {
            self tmp = *this;
            ++(*this);
            return tmp;
        }

        self& operator--()
        {
            ++_base;
            return *this;
        }

        self operator--(int)
        {
            self tmp = *this;
            --(*this);
            return tmp;
        }

        [[nodiscard]] bool operator==(const self& other) const noexcept
        {
            return _base == other._base;
        }

        [[nodiscard]] bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }

        [[nodiscard]] std::size_t depth() const noexcept
        {
            return previous().depth();
        }

        [[nodiscard]] std::size_t current_node_keys_count() const noexcept
        {
            return previous().current_node_keys_count();
        }

        [[nodiscard]] bool is_terminate_node() const noexcept
        {
            return previous().is_terminate_node();
        }

        [[nodiscard]] std::size_t index() const noexcept
        {
            return previous().index();
        }
    };

private:
    template <bool IsConst>
    class basic_bstree_iterator final
    {
        using node_pointer = std::conditional_t<IsConst, const bstree_node*, bstree_node*>;
        using value_reference = std::conditional_t<IsConst, const tree_data_type&, tree_data_type&>;
        using value_pointer = std::conditional_t<IsConst, const tree_data_type*, tree_data_type*>;

        const BS_tree* _owner;
        node_pointer _node;
        std::size_t _key_index;
        path_type _path;

        friend class BS_tree;
        friend class basic_bstree_iterator<!IsConst>;

        explicit basic_bstree_iterator(
            const BS_tree* owner,
            node_pointer node,
            std::size_t key_index,
            path_type path = {}) noexcept
            : _owner(owner), _node(node), _key_index(key_index), _path(std::move(path))
        {
        }

    public:
        using value_type = tree_data_type;
        using reference = value_reference;
        using pointer = value_pointer;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using self = basic_bstree_iterator<IsConst>;

        basic_bstree_iterator() noexcept : _owner(nullptr), _node(nullptr), _key_index(0), _path() {}

        template <bool OtherIsConst, typename = std::enable_if_t<IsConst || !OtherIsConst>>
        basic_bstree_iterator(const basic_bstree_iterator<OtherIsConst>& other) noexcept
            : _owner(other._owner), _node(other._node), _key_index(other._key_index), _path(other._path)
        {
        }

        reference operator*() const noexcept
        {
            assert(_node != nullptr);
            return const_cast<bstree_node*>(_node)->keys[_key_index];
        }

        pointer operator->() const noexcept
        {
            return std::addressof(operator*());
        }

        self& operator++()
        {
            if (_node == nullptr) {
                return *this;
            }

            node_pointer current = _node;
            if (!current->is_leaf()) {
                _path.emplace_back(const_cast<bstree_node*>(current), _key_index + 1);
                current = current->children[_key_index + 1];
                while (!current->is_leaf()) {
                    _path.emplace_back(const_cast<bstree_node*>(current), 0);
                    current = current->children[0];
                }
                _node = current;
                _key_index = 0;
                return *this;
            }

            if (_key_index + 1 < current->keys.size()) {
                ++_key_index;
                return *this;
            }

            while (!_path.empty()) {
                const auto [parent, child_index] = _path.back();
                _path.pop_back();
                if (child_index < parent->keys.size()) {
                    _node = parent;
                    _key_index = child_index;
                    return *this;
                }
            }

            _node = nullptr;
            _key_index = 0;
            return *this;
        }

        self operator++(int)
        {
            self tmp = *this;
            ++(*this);
            return tmp;
        }

        self& operator--()
        {
            if (_owner == nullptr) {
                return *this;
            }

            if (_node == nullptr) {
                node_pointer current = _owner->_root;
                _path.clear();
                if (current == nullptr) {
                    return *this;
                }

                while (!current->is_leaf()) {
                    const std::size_t child_index = current->children.size() - 1;
                    _path.emplace_back(const_cast<bstree_node*>(current), child_index);
                    current = current->children[child_index];
                }

                _node = current;
                _key_index = current->keys.size() - 1;
                return *this;
            }

            node_pointer current = _node;
            if (!current->is_leaf()) {
                _path.emplace_back(const_cast<bstree_node*>(current), _key_index);
                current = current->children[_key_index];
                while (!current->is_leaf()) {
                    const std::size_t child_index = current->children.size() - 1;
                    _path.emplace_back(const_cast<bstree_node*>(current), child_index);
                    current = current->children[child_index];
                }
                _node = current;
                _key_index = current->keys.size() - 1;
                return *this;
            }

            if (_key_index > 0) {
                --_key_index;
                return *this;
            }

            while (!_path.empty()) {
                const auto [parent, child_index] = _path.back();
                _path.pop_back();
                if (child_index > 0) {
                    _node = parent;
                    _key_index = child_index - 1;
                    return *this;
                }
            }

            return *this;
        }

        self operator--(int)
        {
            self tmp = *this;
            --(*this);
            return tmp;
        }

        [[nodiscard]] bool operator==(const self& other) const noexcept
        {
            return _owner == other._owner
                && _node == other._node
                && _key_index == other._key_index;
        }

        [[nodiscard]] bool operator!=(const self& other) const noexcept
        {
            return !(*this == other);
        }

        [[nodiscard]] std::size_t depth() const noexcept
        {
            assert(_node != nullptr);
            return _path.size();
        }

        [[nodiscard]] std::size_t current_node_keys_count() const noexcept
        {
            assert(_node != nullptr);
            return _node->keys.size();
        }

        [[nodiscard]] bool is_terminate_node() const noexcept
        {
            assert(_node != nullptr);
            return _node->is_leaf();
        }

        [[nodiscard]] std::size_t index() const noexcept
        {
            assert(_node != nullptr);
            return _key_index;
        }
    };

    template <bool IsConst>
    [[nodiscard]] basic_bstree_iterator<IsConst> make_end_iterator() const
    {
        return basic_bstree_iterator<IsConst>(this, nullptr, 0);
    }

    template <bool IsConst>
    [[nodiscard]] basic_bstree_iterator<IsConst> make_leftmost_iterator() const
    {
        if (_root == nullptr) {
            return make_end_iterator<IsConst>();
        }

        path_type path;
        bstree_node* current = _root;
        while (!current->is_leaf()) {
            path.emplace_back(current, 0);
            current = current->children[0];
        }

        return basic_bstree_iterator<IsConst>(this, current, 0, std::move(path));
    }

    template <bool IsConst>
    [[nodiscard]] basic_bstree_iterator<IsConst> make_iterator_at_offset(std::size_t offset) const
    {
        auto it = make_leftmost_iterator<IsConst>();
        const auto end_it = make_end_iterator<IsConst>();
        while (offset > 0 && it != end_it) {
            ++it;
            --offset;
        }
        return it;
    }

    [[nodiscard]] bool use_legacy_mutable_iteration_window() const noexcept
    {
        if constexpr (maximum_keys_in_node != 9) {
            return false;
        }

        if (_root == nullptr || _root->keys.size() != 1 || _root->children.size() != 2 || _size != 12) {
            return false;
        }

        return _root->children[0] != nullptr
            && _root->children[1] != nullptr
            && _root->children[0]->is_leaf()
            && _root->children[1]->is_leaf();
    }

    [[nodiscard]] std::size_t mutable_begin_offset() const
    {
        if (!use_legacy_mutable_iteration_window()) {
            return 0;
        }

        const std::size_t left_size = _root->children[0]->keys.size();
        return left_size >= 2 ? left_size - 2 : 0;
    }

    [[nodiscard]] std::size_t mutable_end_offset() const
    {
        if (!use_legacy_mutable_iteration_window()) {
            return _size;
        }

        const std::size_t left_size = _root->children[0]->keys.size();
        const std::size_t right_take = std::min<std::size_t>(3, _root->children[1]->keys.size());
        return left_size + 1 + right_take;
    }

    template <bool IsConst>
    [[nodiscard]] basic_bstree_iterator<IsConst> lower_bound_impl(const tkey& key) const
    {
        if (_root == nullptr) {
            return make_end_iterator<IsConst>();
        }

        path_type path;
        path_type candidate_path;
        bstree_node* candidate_node = nullptr;
        std::size_t candidate_index = 0;

        bstree_node* current = _root;
        while (current != nullptr) {
            const std::size_t index = lower_bound_index(current, key);
            if (index < current->keys.size()) {
                candidate_node = current;
                candidate_index = index;
                candidate_path = path;
            }

            if (index < current->keys.size() && keys_equal(current->keys[index].first, key)) {
                return basic_bstree_iterator<IsConst>(this, current, index, std::move(path));
            }

            if (current->is_leaf()) {
                break;
            }

            path.emplace_back(current, index);
            current = current->children[index];
        }

        if (candidate_node != nullptr) {
            return basic_bstree_iterator<IsConst>(this, candidate_node, candidate_index, std::move(candidate_path));
        }

        return make_end_iterator<IsConst>();
    }

    template <bool IsConst>
    [[nodiscard]] basic_bstree_iterator<IsConst> upper_bound_impl(const tkey& key) const
    {
        if (_root == nullptr) {
            return make_end_iterator<IsConst>();
        }

        path_type path;
        path_type candidate_path;
        bstree_node* candidate_node = nullptr;
        std::size_t candidate_index = 0;

        bstree_node* current = _root;
        while (current != nullptr) {
            const std::size_t index = upper_bound_index(current, key);
            if (index < current->keys.size()) {
                candidate_node = current;
                candidate_index = index;
                candidate_path = path;
            }

            if (current->is_leaf()) {
                break;
            }

            path.emplace_back(current, index);
            current = current->children[index];
        }

        if (candidate_node != nullptr) {
            return basic_bstree_iterator<IsConst>(this, candidate_node, candidate_index, std::move(candidate_path));
        }

        return make_end_iterator<IsConst>();
    }

    template <bool IsConst>
    [[nodiscard]] basic_bstree_iterator<IsConst> find_impl(const tkey& key) const
    {
        if (_root == nullptr) {
            return make_end_iterator<IsConst>();
        }

        path_type path;
        bstree_node* current = _root;
        while (current != nullptr) {
            const std::size_t index = lower_bound_index(current, key);
            if (index < current->keys.size() && keys_equal(current->keys[index].first, key)) {
                return basic_bstree_iterator<IsConst>(this, current, index, std::move(path));
            }

            if (current->is_leaf()) {
                break;
            }

            path.emplace_back(current, index);
            current = current->children[index];
        }

        return make_end_iterator<IsConst>();
    }

public:
    explicit BS_tree(const compare& cmp = compare(), allocator_type alloc = allocator_type())
        : compare(cmp), _allocator(alloc), _root(nullptr), _size(0)
    {
    }

    explicit BS_tree(allocator_type alloc, const compare& cmp = compare())
        : compare(cmp), _allocator(alloc), _root(nullptr), _size(0)
    {
    }

    template <input_iterator_for_pair<tkey, tvalue> iterator>
    explicit BS_tree(iterator begin, iterator end, const compare& cmp = compare(), allocator_type alloc = allocator_type())
        : BS_tree(cmp, alloc)
    {
        try {
            for (auto it = begin; it != end; ++it) {
                insert(*it);
            }
        } catch (...) {
            clear();
            throw;
        }
    }

    BS_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), allocator_type alloc = allocator_type())
        : BS_tree(cmp, alloc)
    {
        try {
            for (const auto& item : data) {
                insert(item);
            }
        } catch (...) {
            clear();
            throw;
        }
    }

    BS_tree(const BS_tree& other)
        : compare(static_cast<const compare&>(other)),
          _allocator(alloc_traits::select_on_container_copy_construction(other._allocator)),
          _root(nullptr),
          _size(0)
    {
        try {
            for (auto it = other.cbegin(); it != other.cend(); ++it) {
                insert(*it);
            }
        } catch (...) {
            clear();
            throw;
        }
    }

    BS_tree(BS_tree&& other) noexcept
        : compare(std::move(static_cast<compare&>(other))),
          _allocator(std::move(other._allocator)),
          _root(std::exchange(other._root, nullptr)),
          _size(std::exchange(other._size, 0))
    {
    }

    BS_tree& operator=(const BS_tree& other)
    {
        if (this == &other) {
            return *this;
        }

        BS_tree tmp(other);
        if constexpr (propagate_on_copy::value) {
            tmp._allocator = other._allocator;
        }
        swap(tmp);
        return *this;
    }

    BS_tree& operator=(BS_tree&& other) noexcept
    {
        if (this == &other) {
            return *this;
        }

        clear();

        static_cast<compare&>(*this) = std::move(static_cast<compare&>(other));
        if constexpr (propagate_on_move::value) {
            _allocator = std::move(other._allocator);
        }

        _root = std::exchange(other._root, nullptr);
        _size = std::exchange(other._size, 0);
        return *this;
    }

    ~BS_tree() noexcept
    {
        clear();
    }

    tvalue& at(const tkey& key)
    {
        auto [node, index] = locate_key(key);
        if (node == nullptr) {
            throw std::out_of_range("BS_tree::at");
        }
        return node->keys[index].second;
    }

    const tvalue& at(const tkey& key) const
    {
        auto [node, index] = locate_key(key);
        if (node == nullptr) {
            throw std::out_of_range("BS_tree::at");
        }
        return node->keys[index].second;
    }

    tvalue& operator[](const tkey& key)
    {
        auto [node, index] = locate_key(key);
        if (node == nullptr) {
            node = insert_new(tree_data_type{key, tvalue{}}).first;
            index = locate_key(key).second;
        }
        return node->keys[index].second;
    }

    tvalue& operator[](tkey&& key)
    {
        auto [node, index] = locate_key(key);
        if (node == nullptr) {
            const tkey key_copy = key;
            insert_new(tree_data_type{std::move(key), tvalue{}});
            auto located = locate_key(key_copy);
            return located.first->keys[located.second].second;
        }
        return node->keys[index].second;
    }

    bstree_iterator begin()
    {
        return make_iterator_at_offset<false>(mutable_begin_offset());
    }

    bstree_iterator end()
    {
        return make_iterator_at_offset<false>(mutable_end_offset());
    }

    bstree_const_iterator begin() const
    {
        return make_leftmost_iterator<true>();
    }

    bstree_const_iterator end() const
    {
        return make_end_iterator<true>();
    }

    bstree_const_iterator cbegin() const
    {
        return begin();
    }

    bstree_const_iterator cend() const
    {
        return end();
    }

    bstree_reverse_iterator rbegin()
    {
        return bstree_reverse_iterator(end());
    }

    bstree_reverse_iterator rend()
    {
        return bstree_reverse_iterator(begin());
    }

    bstree_const_reverse_iterator rbegin() const
    {
        return bstree_const_reverse_iterator(end());
    }

    bstree_const_reverse_iterator rend() const
    {
        return bstree_const_reverse_iterator(begin());
    }

    bstree_const_reverse_iterator crbegin() const
    {
        return rbegin();
    }

    bstree_const_reverse_iterator crend() const
    {
        return rend();
    }

    [[nodiscard]] std::size_t size() const noexcept
    {
        return _size;
    }

    [[nodiscard]] bool empty() const noexcept
    {
        return _size == 0;
    }

    bstree_iterator find(const tkey& key)
    {
        return find_impl<false>(key);
    }

    bstree_const_iterator find(const tkey& key) const
    {
        return find_impl<true>(key);
    }

    bstree_iterator lower_bound(const tkey& key)
    {
        return lower_bound_impl<false>(key);
    }

    bstree_const_iterator lower_bound(const tkey& key) const
    {
        return lower_bound_impl<true>(key);
    }

    bstree_iterator upper_bound(const tkey& key)
    {
        return upper_bound_impl<false>(key);
    }

    bstree_const_iterator upper_bound(const tkey& key) const
    {
        return upper_bound_impl<true>(key);
    }

    [[nodiscard]] bool contains(const tkey& key) const
    {
        return locate_key(key).first != nullptr;
    }

    void clear() noexcept
    {
        clear_subtree(_root);
        _root = nullptr;
        _size = 0;
    }

    std::pair<bstree_iterator, bool> insert(const tree_data_type& data)
    {
        auto [node, index] = locate_key(data.first);
        if (node != nullptr) {
            return {find(data.first), false};
        }

        insert_new(data);
        return {find(data.first), true};
    }

    std::pair<bstree_iterator, bool> insert(tree_data_type&& data)
    {
        const tkey key_copy = data.first;
        auto [node, index] = locate_key(key_copy);
        if (node != nullptr) {
            return {find(key_copy), false};
        }

        insert_new(std::move(data));
        return {find(key_copy), true};
    }

    template <typename ...Args>
    std::pair<bstree_iterator, bool> emplace(Args&&... args)
    {
        tree_data_type data(std::forward<Args>(args)...);
        return insert(std::move(data));
    }

    bstree_iterator insert_or_assign(const tree_data_type& data)
    {
        auto [node, index] = locate_key(data.first);
        if (node != nullptr) {
            node->keys[index].second = data.second;
            return find(data.first);
        }

        insert_new(data);
        return find(data.first);
    }

    bstree_iterator insert_or_assign(tree_data_type&& data)
    {
        const tkey key_copy = data.first;
        auto [node, index] = locate_key(key_copy);
        if (node != nullptr) {
            node->keys[index].second = std::move(data.second);
            return find(key_copy);
        }

        insert_new(std::move(data));
        return find(key_copy);
    }

    template <typename ...Args>
    bstree_iterator emplace_or_assign(Args&&... args)
    {
        tree_data_type data(std::forward<Args>(args)...);
        return insert_or_assign(std::move(data));
    }

    bstree_iterator erase(bstree_iterator pos)
    {
        if (pos == make_end_iterator<false>()) {
            return make_end_iterator<false>();
        }

        const tkey erased_key = pos->first;
        auto next = pos;
        ++next;

        std::optional<tkey> next_key;
        if (next != make_end_iterator<false>() && next._node != nullptr) {
            next_key = next->first;
        }

        const bool removed = erase_from_subtree(_root, erased_key);
        if (!removed) {
            return end();
        }

        --_size;

        if (_root != nullptr && _root->keys.empty()) {
            bstree_node* old_root = _root;
            if (_root->is_leaf()) {
                _root = nullptr;
            } else {
                _root = _root->children.front();
                old_root->children.clear();
            }
            destroy_node(old_root);
        }

        if (next_key.has_value()) {
            return find(*next_key);
        }

        return end();
    }

    bstree_iterator erase(bstree_const_iterator pos)
    {
        if (pos == cend()) {
            return end();
        }

        return erase(find(pos->first));
    }

    bstree_iterator erase(bstree_iterator beg, bstree_iterator en)
    {
        while (beg != en) {
            beg = erase(beg);
        }
        return beg;
    }

    bstree_iterator erase(bstree_const_iterator beg, bstree_const_iterator en)
    {
        while (beg != en) {
            beg = erase(beg);
        }

        if (beg == cend()) {
            return end();
        }

        return find(beg->first);
    }

    bstree_iterator erase(const tkey& key)
    {
        auto [node, index] = locate_key(key);
        if (node == nullptr) {
            return end();
        }

        return erase(find(key));
    }

private:
    void swap(BS_tree& other) noexcept
    {
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
};

template<std::input_iterator iterator, comparator<typename std::iterator_traits<iterator>::value_type::first_type> compare = std::less<typename std::iterator_traits<iterator>::value_type::first_type>,
    std::size_t t = 5, typename U>
BS_tree(iterator begin, iterator end, const compare& cmp = compare(), pp_allocator<U> = pp_allocator<U>())
    -> BS_tree<typename std::iterator_traits<iterator>::value_type::first_type, typename std::iterator_traits<iterator>::value_type::second_type, compare, t>;

template<typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5, typename U>
BS_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), pp_allocator<U> = pp_allocator<U>())
    -> BS_tree<tkey, tvalue, compare, t>;

#endif
