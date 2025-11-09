#ifndef TEST1_INCLUDE3_HPP_1
#define TEST1_INCLUDE3_HPP_1
inline void test_function1() {}
#include __FILE__
#elifndef TEST1_INCLUDE3_HPP_2
#define TEST1_INCLUDE3_HPP_2
inline void test_function2() {}
#endif
