#define _cleanup_(f) __attribute__((cleanup(f)))
#define DEFINE_POINTER_CLEANUP_FUNC(type, func) \
  static inline void func##p(type** p) { \
    if (*p != nullptr) { \
      func(*p); \
      *p = nullptr; \
    } \
  }
