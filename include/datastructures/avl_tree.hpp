#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace storage {

template <typename Key, typename Value, typename Compare = std::less<Key>>
class AVLTree {
public:
    struct Node {
        Key key;
        Value value;
        int height;
        Node* left;
        Node* right;

        Node(const Key& k, const Value& v)
            : key(k), value(v), height(1), left(nullptr), right(nullptr) {}
    };

    AVLTree() : root_(nullptr), size_(0) {}
    ~AVLTree() { clear(); }

    AVLTree(const AVLTree&) = delete;
    AVLTree& operator=(const AVLTree&) = delete;

    AVLTree(AVLTree&& other) noexcept : root_(other.root_), size_(other.size_) {
        other.root_ = nullptr;
        other.size_ = 0;
    }

    AVLTree& operator=(AVLTree&& other) noexcept {
        if (this != &other) {
            clear();
            root_ = other.root_;
            size_ = other.size_;
            other.root_ = nullptr;
            other.size_ = 0;
        }
        return *this;
    }

    size_t size() const { return size_; }
    bool empty() const { return size_ == 0; }
    int height() const { return get_height(root_); }

    void clear() {
        destroy(root_);
        root_ = nullptr;
        size_ = 0;
    }

    bool insert(const Key& key, const Value& value) {
        bool inserted = false;
        root_ = insert_node(root_, key, value, inserted);
        if (inserted) {
            ++size_;
        }
        return inserted;
    }

    bool remove(const Key& key) {
        bool removed = false;
        root_ = remove_node(root_, key, removed);
        if (removed) {
            --size_;
        }
        return removed;
    }

    Value* find(const Key& key) {
        Node* curr = root_;
        while (curr) {
            if (cmp_(key, curr->key)) {
                curr = curr->left;
            } else if (cmp_(curr->key, key)) {
                curr = curr->right;
            } else {
                return &curr->value;
            }
        }
        return nullptr;
    }

    const Value* find(const Key& key) const {
        Node* curr = root_;
        while (curr) {
            if (cmp_(key, curr->key)) {
                curr = curr->left;
            } else if (cmp_(curr->key, key)) {
                curr = curr->right;
            } else {
                return &curr->value;
            }
        }
        return nullptr;
    }

    bool contains(const Key& key) const {
        return find(key) != nullptr;
    }

    std::vector<std::pair<Key, Value>> in_order() const {
        std::vector<std::pair<Key, Value>> result;
        result.reserve(size_);
        in_order_traversal(root_, result);
        return result;
    }

    std::vector<std::pair<Key, Value>> range_query(const Key& min_key, const Key& max_key) const {
        std::vector<std::pair<Key, Value>> result;
        range_query_helper(root_, min_key, max_key, result);
        return result;
    }

private:
    Node* root_;
    size_t size_;
    Compare cmp_;

    static int get_height(Node* n) {
        return n ? n->height : 0;
    }

    static int get_balance(Node* n) {
        return n ? get_height(n->left) - get_height(n->right) : 0;
    }

    static void update_height(Node* n) {
        if (n) {
            n->height = 1 + std::max(get_height(n->left), get_height(n->right));
        }
    }

    Node* rotate_right(Node* y) {
        Node* x = y->left;
        Node* t2 = x->right;

        x->right = y;
        y->left = t2;

        update_height(y);
        update_height(x);

        return x;
    }

    Node* rotate_left(Node* x) {
        Node* y = x->right;
        Node* t2 = y->left;

        y->left = x;
        x->right = t2;

        update_height(x);
        update_height(y);

        return y;
    }

    Node* insert_node(Node* node, const Key& key, const Value& value, bool& inserted) {
        if (!node) {
            inserted = true;
            return new Node(key, value);
        }

        if (cmp_(key, node->key)) {
            node->left = insert_node(node->left, key, value, inserted);
        } else if (cmp_(node->key, key)) {
            node->right = insert_node(node->right, key, value, inserted);
        } else {
            // Key already exists, update value
            node->value = value;
            inserted = false;
            return node;
        }

        update_height(node);
        int balance = get_balance(node);

        // Left-Left Case
        if (balance > 1 && cmp_(key, node->left->key)) {
            return rotate_right(node);
        }

        // Right-Right Case
        if (balance < -1 && cmp_(node->right->key, key)) {
            return rotate_left(node);
        }

        // Left-Right Case
        if (balance > 1 && cmp_(node->left->key, key)) {
            node->left = rotate_left(node->left);
            return rotate_right(node);
        }

        // Right-Left Case
        if (balance < -1 && cmp_(key, node->right->key)) {
            node->right = rotate_right(node->right);
            return rotate_left(node);
        }

        return node;
    }

    Node* min_value_node(Node* node) {
        Node* curr = node;
        while (curr && curr->left) {
            curr = curr->left;
        }
        return curr;
    }

    Node* remove_node(Node* node, const Key& key, bool& removed) {
        if (!node) {
            removed = false;
            return nullptr;
        }

        if (cmp_(key, node->key)) {
            node->left = remove_node(node->left, key, removed);
        } else if (cmp_(node->key, key)) {
            node->right = remove_node(node->right, key, removed);
        } else {
            removed = true;
            if (!node->left || !node->right) {
                Node* temp = node->left ? node->left : node->right;
                if (!temp) {
                    temp = node;
                    node = nullptr;
                } else {
                    *node = *temp;
                }
                delete temp;
            } else {
                Node* temp = min_value_node(node->right);
                node->key = temp->key;
                node->value = temp->value;
                node->right = remove_node(node->right, temp->key, removed);
            }
        }

        if (!node) return nullptr;

        update_height(node);
        int balance = get_balance(node);

        // Left-Left
        if (balance > 1 && get_balance(node->left) >= 0) {
            return rotate_right(node);
        }

        // Left-Right
        if (balance > 1 && get_balance(node->left) < 0) {
            node->left = rotate_left(node->left);
            return rotate_right(node);
        }

        // Right-Right
        if (balance < -1 && get_balance(node->right) <= 0) {
            return rotate_left(node);
        }

        // Right-Left
        if (balance < -1 && get_balance(node->right) > 0) {
            node->right = rotate_right(node->right);
            return rotate_left(node);
        }

        return node;
    }

    void in_order_traversal(Node* node, std::vector<std::pair<Key, Value>>& result) const {
        if (!node) return;
        in_order_traversal(node->left, result);
        result.emplace_back(node->key, node->value);
        in_order_traversal(node->right, result);
    }

    void range_query_helper(Node* node, const Key& min_key, const Key& max_key,
                            std::vector<std::pair<Key, Value>>& result) const {
        if (!node) return;

        // If min_key < node->key, there could be elements in left subtree
        if (!cmp_(node->key, min_key)) {
            range_query_helper(node->left, min_key, max_key, result);
        }

        // If node->key is within [min_key, max_key]
        if (!cmp_(node->key, min_key) && !cmp_(max_key, node->key)) {
            result.emplace_back(node->key, node->value);
        }

        // If node->key < max_key, there could be elements in right subtree
        if (!cmp_(max_key, node->key)) {
            range_query_helper(node->right, min_key, max_key, result);
        }
    }

    void destroy(Node* node) {
        if (!node) return;
        destroy(node->left);
        destroy(node->right);
        delete node;
    }
};

} // namespace storage
