#include "include/QCDTherm.hpp"
#include <iostream>
#include <string>

int main() {
    try {
        std::cout << "Testing QCD initialization..." << std::endl;
        QCD::setEoS(1, "./", 3);
        std::cout << "QCD initialized successfully!" << std::endl;
        
        std::cout << "Testing BarDens calculation..." << std::endl;
        double nB = QCD::BarDens(0.01, 0.0, 150.0, 3);
        std::cout << "nB (T=150, muB=0.01) = " << nB << std::endl;
        
        QCD::cleanup();
        std::cout << "QCD cleanup successful!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Caught exception: " << e.what() << std::endl;
        return 1;
    }
}
