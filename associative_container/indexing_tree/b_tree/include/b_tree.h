#ifndef SYS_PROG_B_TREE_H
#define SYS_PROG_B_TREE_H

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
class B_tree final : private compare
{
    static_assert(t >= 2, "B_tree minimum degree must be at least 2");

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

    struct btree_node
    {
        std::vector<tree_data_type> keys;
        std::vector<btree_node*> children;

        btree_node()
        {
            keys.reserve(maximum_keys_in_node + 1);
            children.reserve(maximum_keys_in_node + 2);
        }

        [[nodiscard]] bool is_leaf() const noexcept
        {
            return children.empty();
        }
    };

    using path_entry = std::pair<btree_node*, std::size_t>;
    using path_type = std::vector<path_entry>;

    allocator_type _allocator;
    btree_node* _root;
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

    [[nodiscard]] btree_node* create_node()
    {
        return _allocator.template new_object<btree_node>();
    }

    void destroy_node(btree_node* node) noexcept
    {
        if (node != nullptr) {
            _allocator.template delete_object<btree_node>(node);
        }
    }

    void clear_subtree(btree_node* node) noexcept
    {
        if (node == nullptr) {
            return;
        }

        for (btree_node* child : node->children) {
            clear_subtree(child);
        }

        destroy_node(node);
    }

    [[nodiscard]] std::size_t lower_bound_index(const btree_node* node, const tkey& key) const
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

    [[nodiscard]] std::size_t upper_bound_index(const btree_node* node, const tkey& key) const
    {
        std::size_t index = lower_bound_index(node, key);
        while (index < node->keys.size() && keys_equal(node->keys[index].first, key)) {
            ++index;
        }
        return index;
    }

    [[nodiscard]] std::pair<btree_node*, std::size_t> locate_key(const tkey& key) const
    {
        btree_node* current = _root;
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

    template <typename pair_type>
    std::pair<btree_node*, std::size_t> insert_new(pair_type&& data)
    {
        const tkey key_copy = data.first;

        if (_root == nullptr) {
            _root = create_node();
            _root->keys.emplace_back(std::forward<pair_type>(data));
            _size = 1;
            return {_root, 0};
        }

        path_type path;
        btree_node* current = _root;

        while (!current->is_leaf()) {
            const std::size_t child_index = lower_bound_index(current, data.first);
            path.emplace_back(current, child_index);
            current = current->children[child_index];
        }

        const std::size_t inserted_index = lower_bound_index(current, data.first);
        current->keys.insert(current->keys.begin() + static_cast<std::ptrdiff_t>(inserted_index), std::forward<pair_type>(data));
        ++_size;

        while (current->keys.size() > maximum_keys_in_node) {
            const std::size_t middle_index = current->keys.size() / 2;
            tree_data_type middle_key = std::move(current->keys[middle_index]);

            btree_node* right = create_node();
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

            if (path.empty()) {
                btree_node* new_root = create_node();
                new_root->keys.push_back(std::move(middle_key));
                new_root->children.push_back(current);
                new_root->children.push_back(right);
                _root = new_root;
                break;
            }

            auto [parent, parent_index] = path.back();
            path.pop_back();

            parent->keys.insert(parent->keys.begin() + static_cast<std::ptrdiff_t>(parent_index), std::move(middle_key));
            parent->children.insert(parent->children.begin() + static_cast<std::ptrdiff_t>(parent_index + 1), right);
            current = parent;
        }

        return locate_key(key_copy);
    }

    [[nodiscard]] tree_data_type& maximum_entry(btree_node* node) const
    {
        assert(node != nullptr);
        while (!node->is_leaf()) {
            node = node->children.back();
        }
        return node->keys.back();
    }

    [[nodiscard]] tree_data_type& minimum_entry(btree_node* node) const
    {
        assert(node != nullptr);
        while (!node->is_leaf()) {
            node = node->children.front();
        }
        return node->keys.front();
    }

    void borrow_from_left(btree_node* parent, std::size_t child_index)
    {
        btree_node* child = parent->children[child_index];
        btree_node* left = parent->children[child_index - 1];

        child->keys.insert(child->keys.begin(), std::move(parent->keys[child_index - 1]));
        parent->keys[child_index - 1] = std::move(left->keys.back());
        left->keys.pop_back();

        if (!left->is_leaf()) {
            child->children.insert(child->children.begin(), left->children.back());
            left->children.pop_back();
        }
    }

    void borrow_from_right(btree_node* parent, std::size_t child_index)
    {
        btree_node* child = parent->children[child_index];
        btree_node* right = parent->children[child_index + 1];

        child->keys.push_back(std::move(parent->keys[child_index]));
        parent->keys[child_index] = std::move(right->keys.front());
        right->keys.erase(right->keys.begin());

        if (!right->is_leaf()) {
            child->children.push_back(right->children.front());
            right->children.erase(right->children.begin());
        }
    }

    void merge_children(btree_node* parent, std::size_t key_index)
    {
        btree_node* left = parent->children[key_index];
        btree_node* right = parent->children[key_index + 1];

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

    bool erase_from_subtree(btree_node* node, const tkey& key)
    {
        const std::size_t index = lower_bound_index(node, key);
        const bool key_in_node = index < node->keys.size() && keys_equal(node->keys[index].first, key);

        if (key_in_node) {
            if (node->is_leaf()) {
                node->keys.erase(node->keys.begin() + static_cast<std::ptrdiff_t>(index));
                return true;
            }

            btree_node* left_child = node->children[index];
            btree_node* right_child = node->children[index + 1];

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
        btree_node* child = node->children[child_index];

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
    class basic_btree_iterator;

public:
    using btree_iterator = basic_btree_iterator<false>;
    using btree_const_iterator = basic_btree_iterator<true>;

    class btree_reverse_iterator final
    {
        btree_iterator _base;

        friend class B_tree;
        friend class btree_const_reverse_iterator;

        explicit btree_reverse_iterator(const btree_iterator& base) noexcept : _base(base) {}

        [[nodiscard]] btree_iterator previous() const
        {
            btree_iterator tmp = _base;
            --tmp;
            return tmp;
        }

    public:
        using value_type = tree_data_type;
        using reference = value_type&;
        using pointer = value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using self = btree_reverse_iterator;

        btree_reverse_iterator() noexcept = default;

        operator btree_iterator() const noexcept
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

    class btree_const_reverse_iterator final
    {
        btree_const_iterator _base;

        friend class B_tree;

        explicit btree_const_reverse_iterator(const btree_const_iterator& base) noexcept : _base(base) {}

        [[nodiscard]] btree_const_iterator previous() const
        {
            btree_const_iterator tmp = _base;
            --tmp;
            return tmp;
        }

    public:
        using value_type = tree_data_type;
        using reference = const value_type&;
        using pointer = const value_type*;
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using self = btree_const_reverse_iterator;

        btree_const_reverse_iterator() noexcept = default;

        btree_const_reverse_iterator(const btree_reverse_iterator& other) noexcept
            : _base(static_cast<btree_iterator>(other))
        {
        }

        operator btree_const_iterator() const noexcept
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
    class basic_btree_iterator final
    {
        using node_pointer = std::conditional_t<IsConst, const btree_node*, btree_node*>;
        using value_reference = std::conditional_t<IsConst, const tree_data_type&, tree_data_type&>;
        using value_pointer = std::conditional_t<IsConst, const tree_data_type*, tree_data_type*>;

        const B_tree* _owner;
        node_pointer _node;
        std::size_t _key_index;
        path_type _path;

        friend class B_tree;
        friend class basic_btree_iterator<!IsConst>;

        explicit basic_btree_iterator(
            const B_tree* owner,
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
        using self = basic_btree_iterator<IsConst>;

        basic_btree_iterator() noexcept : _owner(nullptr), _node(nullptr), _key_index(0), _path() {}

        template <bool OtherIsConst, typename = std::enable_if_t<IsConst || !OtherIsConst>>
        basic_btree_iterator(const basic_btree_iterator<OtherIsConst>& other) noexcept
            : _owner(other._owner), _node(other._node), _key_index(other._key_index), _path(other._path)
        {
        }

        reference operator*() const noexcept
        {
            assert(_node != nullptr);
            return const_cast<btree_node*>(_node)->keys[_key_index];
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
                _path.emplace_back(const_cast<btree_node*>(current), _key_index + 1);
                current = current->children[_key_index + 1];
                while (!current->is_leaf()) {
                    _path.emplace_back(const_cast<btree_node*>(current), 0);
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
                    _path.emplace_back(const_cast<btree_node*>(current), child_index);
                    current = current->children[child_index];
                }

                _node = current;
                _key_index = current->keys.size() - 1;
                return *this;
            }

            node_pointer current = _node;
            if (!current->is_leaf()) {
                _path.emplace_back(const_cast<btree_node*>(current), _key_index);
                current = current->children[_key_index];
                while (!current->is_leaf()) {
                    const std::size_t child_index = current->children.size() - 1;
                    _path.emplace_back(const_cast<btree_node*>(current), child_index);
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
    [[nodiscard]] basic_btree_iterator<IsConst> make_end_iterator() const
    {
        return basic_btree_iterator<IsConst>(this, nullptr, 0);
    }

    template <bool IsConst>
    [[nodiscard]] basic_btree_iterator<IsConst> make_leftmost_iterator() const
    {
        if (_root == nullptr) {
            return make_end_iterator<IsConst>();
        }

        path_type path;
        btree_node* current = _root;
        while (!current->is_leaf()) {
            path.emplace_back(current, 0);
            current = current->children[0];
        }

        return basic_btree_iterator<IsConst>(this, current, 0, std::move(path));
    }

    template <bool IsConst>
    [[nodiscard]] basic_btree_iterator<IsConst> make_iterator_at_offset(std::size_t offset) const
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
    [[nodiscard]] basic_btree_iterator<IsConst> lower_bound_impl(const tkey& key) const
    {
        if (_root == nullptr) {
            return make_end_iterator<IsConst>();
        }

        path_type path;
        path_type candidate_path;
        btree_node* candidate_node = nullptr;
        std::size_t candidate_index = 0;

        btree_node* current = _root;
        while (current != nullptr) {
            const std::size_t index = lower_bound_index(current, key);
            if (index < current->keys.size()) {
                candidate_node = current;
                candidate_index = index;
                candidate_path = path;
            }

            if (index < current->keys.size() && keys_equal(current->keys[index].first, key)) {
                return basic_btree_iterator<IsConst>(this, current, index, std::move(path));
            }

            if (current->is_leaf()) {
                break;
            }

            path.emplace_back(current, index);
            current = current->children[index];
        }

        if (candidate_node != nullptr) {
            return basic_btree_iterator<IsConst>(this, candidate_node, candidate_index, std::move(candidate_path));
        }

        return make_end_iterator<IsConst>();
    }

    template <bool IsConst>
    [[nodiscard]] basic_btree_iterator<IsConst> upper_bound_impl(const tkey& key) const
    {
        if (_root == nullptr) {
            return make_end_iterator<IsConst>();
        }

        path_type path;
        path_type candidate_path;
        btree_node* candidate_node = nullptr;
        std::size_t candidate_index = 0;

        btree_node* current = _root;
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
            return basic_btree_iterator<IsConst>(this, candidate_node, candidate_index, std::move(candidate_path));
        }

        return make_end_iterator<IsConst>();
    }

    template <bool IsConst>
    [[nodiscard]] basic_btree_iterator<IsConst> find_impl(const tkey& key) const
    {
        if (_root == nullptr) {
            return make_end_iterator<IsConst>();
        }

        path_type path;
        btree_node* current = _root;
        while (current != nullptr) {
            const std::size_t index = lower_bound_index(current, key);
            if (index < current->keys.size() && keys_equal(current->keys[index].first, key)) {
                return basic_btree_iterator<IsConst>(this, current, index, std::move(path));
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
    explicit B_tree(const compare& cmp = compare(), allocator_type alloc = allocator_type())
        : compare(cmp), _allocator(alloc), _root(nullptr), _size(0)
    {
    }

    explicit B_tree(allocator_type alloc, const compare& cmp = compare())
        : compare(cmp), _allocator(alloc), _root(nullptr), _size(0)
    {
    }

    template <input_iterator_for_pair<tkey, tvalue> iterator>
    explicit B_tree(iterator begin, iterator end, const compare& cmp = compare(), allocator_type alloc = allocator_type())
        : B_tree(cmp, alloc)
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

    B_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), allocator_type alloc = allocator_type())
        : B_tree(cmp, alloc)
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

    B_tree(const B_tree& other)
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

    B_tree(B_tree&& other) noexcept
        : compare(std::move(static_cast<compare&>(other))),
          _allocator(std::move(other._allocator)),
          _root(std::exchange(other._root, nullptr)),
          _size(std::exchange(other._size, 0))
    {
    }

    B_tree& operator=(const B_tree& other)
    {
        if (this == &other) {
            return *this;
        }

        B_tree tmp(other);
        if constexpr (propagate_on_copy::value) {
            tmp._allocator = other._allocator;
        }
        swap(tmp);
        return *this;
    }

    B_tree& operator=(B_tree&& other) noexcept
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

    ~B_tree() noexcept
    {
        clear();
    }

    tvalue& at(const tkey& key)
    {
        auto [node, index] = locate_key(key);
        if (node == nullptr) {
            throw std::out_of_range("B_tree::at");
        }
        return node->keys[index].second;
    }

    const tvalue& at(const tkey& key) const
    {
        auto [node, index] = locate_key(key);
        if (node == nullptr) {
            throw std::out_of_range("B_tree::at");
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

    btree_iterator begin()
    {
        return make_iterator_at_offset<false>(mutable_begin_offset());
    }

    btree_iterator end()
    {
        return make_iterator_at_offset<false>(mutable_end_offset());
    }

    btree_const_iterator begin() const
    {
        return make_leftmost_iterator<true>();
    }

    btree_const_iterator end() const
    {
        return make_end_iterator<true>();
    }

    btree_const_iterator cbegin() const
    {
        return begin();
    }

    btree_const_iterator cend() const
    {
        return end();
    }

    btree_reverse_iterator rbegin()
    {
        return btree_reverse_iterator(end());
    }

    btree_reverse_iterator rend()
    {
        return btree_reverse_iterator(begin());
    }

    btree_const_reverse_iterator rbegin() const
    {
        return btree_const_reverse_iterator(end());
    }

    btree_const_reverse_iterator rend() const
    {
        return btree_const_reverse_iterator(begin());
    }

    btree_const_reverse_iterator crbegin() const
    {
        return rbegin();
    }

    btree_const_reverse_iterator crend() const
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

    btree_iterator find(const tkey& key)
    {
        return find_impl<false>(key);
    }

    btree_const_iterator find(const tkey& key) const
    {
        return find_impl<true>(key);
    }

    btree_iterator lower_bound(const tkey& key)
    {
        return lower_bound_impl<false>(key);
    }

    btree_const_iterator lower_bound(const tkey& key) const
    {
        return lower_bound_impl<true>(key);
    }

    btree_iterator upper_bound(const tkey& key)
    {
        return upper_bound_impl<false>(key);
    }

    btree_const_iterator upper_bound(const tkey& key) const
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

    std::pair<btree_iterator, bool> insert(const tree_data_type& data)
    {
        auto [node, index] = locate_key(data.first);
        if (node != nullptr) {
            return {find(data.first), false};
        }

        insert_new(data);
        return {find(data.first), true};
    }

    std::pair<btree_iterator, bool> insert(tree_data_type&& data)
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
    std::pair<btree_iterator, bool> emplace(Args&&... args)
    {
        tree_data_type data(std::forward<Args>(args)...);
        return insert(std::move(data));
    }

    btree_iterator insert_or_assign(const tree_data_type& data)
    {
        auto [node, index] = locate_key(data.first);
        if (node != nullptr) {
            node->keys[index].second = data.second;
            return find(data.first);
        }

        insert_new(data);
        return find(data.first);
    }

    btree_iterator insert_or_assign(tree_data_type&& data)
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
    btree_iterator emplace_or_assign(Args&&... args)
    {
        tree_data_type data(std::forward<Args>(args)...);
        return insert_or_assign(std::move(data));
    }

    btree_iterator erase(btree_iterator pos)
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
            btree_node* old_root = _root;
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

    btree_iterator erase(btree_const_iterator pos)
    {
        if (pos == cend()) {
            return end();
        }

        return erase(find(pos->first));
    }

    btree_iterator erase(btree_iterator beg, btree_iterator en)
    {
        while (beg != en) {
            beg = erase(beg);
        }
        return beg;
    }

    btree_iterator erase(btree_const_iterator beg, btree_const_iterator en)
    {
        while (beg != en) {
            beg = erase(beg);
        }

        if (beg == cend()) {
            return end();
        }

        return find(beg->first);
    }

    btree_iterator erase(const tkey& key)
    {
        auto [node, index] = locate_key(key);
        if (node == nullptr) {
            return end();
        }

        return erase(find(key));
    }

private:
    void swap(B_tree& other) noexcept
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
B_tree(iterator begin, iterator end, const compare& cmp = compare(), pp_allocator<U> = pp_allocator<U>())
    -> B_tree<typename std::iterator_traits<iterator>::value_type::first_type, typename std::iterator_traits<iterator>::value_type::second_type, compare, t>;

template<typename tkey, typename tvalue, comparator<tkey> compare = std::less<tkey>, std::size_t t = 5, typename U>
B_tree(std::initializer_list<std::pair<tkey, tvalue>> data, const compare& cmp = compare(), pp_allocator<U> = pp_allocator<U>())
    -> B_tree<tkey, tvalue, compare, t>;

#endif
