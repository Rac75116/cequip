#include "test1_include1.hpp"
#include NEXT_INCLUDE
#include "test1_include3.hpp"
#include <iostream>

int main() {
    dump_hello();
    std::cout << __FILE__ << ' ' << __DATE__ << ' ' << __TIME__ << ' ' << __LINE__ << std::endl;
    test_function1();
    test_function2();
    return 0;
}
