#include "../include/QCDTherm.hpp"
#include "../include/GetEq.hpp"
#include "../include/leptonTherm.hpp"
#include "../include/JEL.hpp"
#include <iostream>
#include <string>

int main() {
    initializeJELTables();
    try {
        std::cout << "Testing JEL massless..." << std::endl;
        std::cout << "jelb::PTot(0, 150, 0, 2) = " << jelb::PTot(0, 150, 0, 2) << std::endl;
        std::cout << "jelb::eTot(0, 150, 0, 2) = " << jelb::eTot(0, 150, 0, 2) << std::endl;
        std::cout << "jelf::PTot(0, 150, 0, 1) = " << jelf::PTot(0, 150, 0, 1) << std::endl;
        std::cout << "jelf::eTot(0, 150, 0, 1) = " << jelf::eTot(0, 150, 0, 1) << std::endl;
        std::cout << "lepton::plep(0, 0, 0, 0, 150) = " << lepton::plep(0, 0, 0, 0, 150) << std::endl;
        std::cout << "lepton::elep(0, 0, 0, 0, 150) = " << lepton::elep(0, 0, 0, 0, 150) << std::endl;

        for (int eos = 0; eos <= 4; ++eos) {
            std::cout << "\nTesting EoS = " << eos << std::endl;
            QCD::setEoS(eos, "./", 3);
            double p = QCD::pQCD(0.01, 0.0, 150.0, 3);
            double e = QCD::eQCD(0.01, 0.0, 150.0, 3);
            std::cout << "QCD::pQCD = " << p << ", QCD::eQCD = " << e << std::endl;
            double pTot = GetEq::TotalP(0.01, 0.0, 0.0, 0.0, 0.0, 150.0, 3);
            double eTot = GetEq::TotalE(0.01, 0.0, 0.0, 0.0, 0.0, 150.0, 3);
            std::cout << "GetEq::TotalP = " << pTot << ", GetEq::TotalE = " << eTot << std::endl;
            QCD::cleanup();
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Caught exception: " << e.what() << std::endl;
        return 1;
    }
}

