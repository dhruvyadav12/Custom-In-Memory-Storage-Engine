#include "datastructures/avl_tree.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <random>
#include <vector>

void test_basic_insertion_and_search() {
    std::cout << "[Test] AVL Tree: Insertion & Search... ";
    storage::AVLTree<int, std::string> tree;

    assert(tree.empty());
    assert(tree.size() == 0);

    assert(tree.insert(10, "ten"));
    assert(tree.insert(20, "twenty"));
    assert(tree.insert(5, "five"));
    assert(tree.insert(15, "fifteen"));

    assert(tree.size() == 4);
    assert(tree.contains(10));
    assert(tree.contains(20));
    assert(tree.contains(5));
    assert(tree.contains(15));
    assert(!tree.contains(99));

    assert(*tree.find(10) == "ten");
    assert(*tree.find(20) == "twenty");

    // Test update
    assert(!tree.insert(10, "TEN"));
    assert(*tree.find(10) == "TEN");
    assert(tree.size() == 4);

    std::cout << "PASSED\n";
}

void test_avl_balance_and_height() {
    std::cout << "[Test] AVL Tree: Balance & Height Invariant... ";
    storage::AVLTree<int, int> tree;

    // Insert 1 to 100 in strictly ascending order (would degenerate a regular BST into a linked list)
    for (int i = 1; i <= 100; ++i) {
        tree.insert(i, i * 10);
    }

    assert(tree.size() == 100);
    // An AVL tree of 100 nodes has maximum theoretical height around 8 or 9 (strictly < 1.44 * log2(100) + 1 ~ 10)
    int h = tree.height();
    assert(h <= 9);

    // Verify in-order traversal is sorted
    auto in_order = tree.in_order();
    assert(in_order.size() == 100);
    for (size_t i = 0; i < in_order.size(); ++i) {
        assert(in_order[i].first == static_cast<int>(i + 1));
        assert(in_order[i].second == static_cast<int>((i + 1) * 10));
    }

    std::cout << "PASSED (Height=" << h << " for 100 nodes)\n";
}

void test_deletion_and_rebalancing() {
    std::cout << "[Test] AVL Tree: Deletion & Rebalancing... ";
    storage::AVLTree<int, int> tree;

    for (int i = 1; i <= 20; ++i) {
        tree.insert(i, i);
    }

    // Remove leaves, nodes with 1 child, and nodes with 2 children
    assert(tree.remove(5));
    assert(!tree.contains(5));
    assert(tree.size() == 19);

    assert(tree.remove(10));
    assert(!tree.contains(10));
    assert(tree.size() == 18);

    assert(tree.remove(1));
    assert(!tree.contains(1));
    assert(tree.size() == 17);

    // In-order traversal must still remain strictly sorted
    auto in_order = tree.in_order();
    assert(in_order.size() == 17);
    for (size_t i = 1; i < in_order.size(); ++i) {
        assert(in_order[i - 1].first < in_order[i].first);
    }

    std::cout << "PASSED\n";
}

void test_range_query() {
    std::cout << "[Test] AVL Tree: Range Query... ";
    storage::AVLTree<int, std::string> tree;

    for (int i = 10; i <= 100; i += 10) {
        tree.insert(i, "val_" + std::to_string(i));
    }

    // Query range [25, 75] -> expected: 30, 40, 50, 60, 70
    auto range = tree.range_query(25, 75);
    assert(range.size() == 5);
    assert(range[0].first == 30);
    assert(range[1].first == 40);
    assert(range[2].first == 50);
    assert(range[3].first == 60);
    assert(range[4].first == 70);

    std::cout << "PASSED\n";
}

int main() {
    std::cout << "=== Running AVL Tree Unit Tests ===\n";
    test_basic_insertion_and_search();
    test_avl_balance_and_height();
    test_deletion_and_rebalancing();
    test_range_query();
    std::cout << "ALL AVL Tree Tests Passed Successfully!\n";
    return 0;
}
