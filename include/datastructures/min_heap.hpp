#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace storage {

template <typename T, typename Compare = std::less<T>>
class MinHeap {
public:
    explicit MinHeap(Compare comp = Compare()) : comp_(comp) {}

    void push(const T& val) {
        heap_.push_back(val);
        sift_up(heap_.size() - 1);
    }

    void push(T&& val) {
        heap_.push_back(std::move(val));
        sift_up(heap_.size() - 1);
    }

    void pop() {
        if (heap_.empty()) {
            throw std::out_of_range("MinHeap is empty");
        }
        std::swap(heap_.front(), heap_.back());
        heap_.pop_back();
        if (!heap_.empty()) {
            sift_down(0);
        }
    }

    const T& top() const {
        if (heap_.empty()) {
            throw std::out_of_range("MinHeap is empty");
        }
        return heap_.front();
    }

    bool empty() const { return heap_.empty(); }
    size_t size() const { return heap_.size(); }
    void clear() { heap_.clear(); }

    const std::vector<T>& data() const { return heap_; }

private:
    std::vector<T> heap_;
    Compare comp_;

    void sift_up(size_t index) {
        while (index > 0) {
            size_t parent = (index - 1) / 2;
            if (comp_(heap_[index], heap_[parent])) {
                std::swap(heap_[index], heap_[parent]);
                index = parent;
            } else {
                break;
            }
        }
    }

    void sift_down(size_t index) {
        size_t n = heap_.size();
        while (true) {
            size_t left = 2 * index + 1;
            size_t right = 2 * index + 2;
            size_t smallest = index;

            if (left < n && comp_(heap_[left], heap_[smallest])) {
                smallest = left;
            }
            if (right < n && comp_(heap_[right], heap_[smallest])) {
                smallest = right;
            }

            if (smallest != index) {
                std::swap(heap_[index], heap_[smallest]);
                index = smallest;
            } else {
                break;
            }
        }
    }
};

struct TTLEntry {
    std::string key;
    uint64_t expire_at_ms;
    uint64_t version;

    bool operator<(const TTLEntry& other) const {
        return expire_at_ms < other.expire_at_ms;
    }

    bool operator>(const TTLEntry& other) const {
        return expire_at_ms > other.expire_at_ms;
    }
};

struct TTLEntryComparator {
    bool operator()(const TTLEntry& a, const TTLEntry& b) const {
        return a.expire_at_ms < b.expire_at_ms;
    }
};

using TTLMinHeap = MinHeap<TTLEntry, TTLEntryComparator>;

} // namespace storage
