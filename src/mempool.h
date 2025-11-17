#pragma once
#include <vector>
#include <cstdint>
template<typename T, size_t CAP = 1<<20>
class RingMempool {
  alignas(64) char pool[sizeof(T)*CAP];
  uint32_t head = 0, tail = 0;
public:
  T* alloc() {
    if (head != tail) {
      T* ptr = reinterpret_cast<T*>(pool + (head * sizeof(T)));
      head = (head + 1) % CAP;
      return ptr;
    }
    return nullptr; // full
  }
  void reset() { head = tail = 0; }
};
