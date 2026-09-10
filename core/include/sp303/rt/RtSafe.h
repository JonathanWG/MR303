#pragma once

// ---------------------------------------------------------------------------
// Real-time safety annotations.
//
// Any function that can be reached from the audio callback must be marked
// SP303_RT. With clang 20+ and -fsanitize=realtime this becomes an enforced
// contract: allocating, locking, or doing I/O inside a marked function aborts
// at runtime with a stack trace.
//
// On other compilers it degrades to documentation. That is fine - CI runs the
// clang build, so violations are caught there.
//
// Rule of thumb for anything marked SP303_RT:
//   NO  new / delete / malloc / free
//   NO  std::vector::push_back, resize, or any container growth
//   NO  mutex, condition_variable, or any blocking primitive
//   NO  file or network I/O
//   NO  std::string construction
//   NO  unbounded loops
// ---------------------------------------------------------------------------

#if defined(__clang__) && defined(SP303_RT_SANITIZE)
    #define SP303_RT [[clang::nonblocking]]
#else
    #define SP303_RT
#endif
