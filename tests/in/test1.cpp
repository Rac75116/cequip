#include "test1_include1.hpp"
#include NEXT_INCLUDE
#include <iostream>

int main() {
    dump_hello();
    std::cout << __FILE__ << ' ' << __DATE__ << ' ' << __TIME__ << ' ' << __LINE__ << std::endl;
    return 0;
}
