#ifndef TEST3_INCLUDED
#define TEST3_INCLUDED
#include <iostream>
int func();
int main() {
    std::cout << func() << std::endl;
    return 0;
}
#include __FILE__
#else
int func() {
    return 1;
}
#endif
