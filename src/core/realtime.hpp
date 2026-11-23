#pragma once

// Marks a function as real-time safe. Clang 20+ rejects calls that allocate, lock or block,
// and RealtimeSanitizer checks the same at run time. Other compilers ignore it.
#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(clang::nonblocking)
#define SESSIONFLOW_NONBLOCKING [[clang::nonblocking]]
#endif
#endif

#ifndef SESSIONFLOW_NONBLOCKING
#define SESSIONFLOW_NONBLOCKING
#endif
