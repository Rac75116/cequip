#include <iostream>

#define TEST_MACRO
#define ANOTHER_MACRO(x) (x * x)
#define CONDITIONAL_MACRO 1

#if 1
inline void test_function() { std::cout << "Test function executed." << std::endl; }
#else
inline void test_function() { std::cout << "This should not appear." << std::endl; }
#endif

// Test Comments
/* Test Comments */
/*
#include <should_not_be_included.hpp>
*/
/* copyright (c) 1024 */

int main() {
    test_function();
    int a = 12, b = 23;
    std::cout << a+/**/+b;
    return 0;
}
