#include <iostream>

#include "test1_include1.hpp"

#ifndef TEST1_INCLUDE2_HPP
#define TEST1_INCLUDE2_HPP

inline void dump_hello() { std::cout << HELLO_WORLD << std::endl; }

#endif
