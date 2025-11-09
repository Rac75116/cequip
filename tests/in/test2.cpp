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

int main() {
    test_function();
    return 0;
}
