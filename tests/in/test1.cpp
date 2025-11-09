#include "test1_include1.hpp"
#include NEXT_INCLUDE
#include "test1_include3.hpp"

int main() {
    dump_hello();
    std::cout << __FILE__ << ' ' << __FILE_NAME__ << std::endl;
    test_function1();
    test_function2();
    return 0;
}
