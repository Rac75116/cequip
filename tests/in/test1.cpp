#include "test1_include1.hpp"  // IWYU pragma: keep
#include NEXT_INCLUDE
#include <iostream>

#include "test1_include3.hpp"

int main() {
    dump_hello();
    std::cout << __FILE__ << ' ' << __DATE__ << ' ' << __TIME__ << ' ' << __LINE__ << std::endl;
    test_function1();
    test_function2();
    return 0;
}
