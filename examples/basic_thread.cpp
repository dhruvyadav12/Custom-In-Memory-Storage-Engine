
#include <iostream>
#include <thread>

int main() {
    std::thread t([] {
        std::cout << "Thread is working!\n";
    });

    t.join();
    return 0;
}
