#pragma once
#include <vector>
#include <cstdint>
class Mempool {
  std::vector<char> pool;
  std::vector<void*> free_list;
public:
  Mempool(size_t capacity = 1<<24) : pool(capacity) {}
  template<typename T> T* alloc() {
    if (!free_list.empty()) {
      T* ptr = static_cast<T*>(free_list.back()); free_list.pop_back(); return ptr;
    }
    return new T; // fallback
  }
  template<typename T> void free(T* ptr) { free_list.push_back(ptr); }
};
