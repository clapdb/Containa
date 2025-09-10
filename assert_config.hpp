#pragma once

#include <cassert>
#include <iostream>
#include <string>

#ifdef NDEBUG
#define Assert(condition, message) ((void)0)
#else
#define Assert(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "Assertion failed: " << (message) << "\n"; \
            std::cerr << "File: " << __FILE__ << ", Line: " << __LINE__ << "\n"; \
            std::abort(); \
        } \
    } while (0)
#endif