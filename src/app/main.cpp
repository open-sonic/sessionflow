#include "core/version.hpp"

#include <iostream>

int main()
{
    std::cout << "sessionflow " << sessionflow::version() << '\n';
    return 0;
}
