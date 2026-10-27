
#include "Loop.h"
#include <exception>
#include <iostream>
int main(int argc, char** argv) {
    try {
        if (Loop::Init(argc, argv)) Loop::Run();
        Loop::Shutdown();
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << "Launcher failed: " << error.what() << std::endl;
        Loop::Shutdown();
        return 1;
    }
}
