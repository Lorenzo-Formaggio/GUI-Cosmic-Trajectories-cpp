#include "include/GetEq.hpp"
#include "include/JEL.hpp"
#include "include/QCDTherm.hpp"
#include "include/Solver.hpp"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <gsl/gsl_sf_bessel.h>

int main() {
    double le = -1e-10;
    double lmu = -1e-10;
    double ltau = -1e-10;
    double b = 8.6e-11; 
    int nf = 3;         

    std::cout << "Initializing Lattice QCD EoS (eos=1)..." << std::endl;
    try {
        QCD::setEoS(1, "/Users/lorenzoformaggio/Desktop/Gui-Cosmic-trajectories-cpp", nf);
    } catch (const std::exception& e) {
        std::cerr << "EoS initialization failed: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "Initializing JEL Tables..." << std::endl;
    initializeJELTables();

    // Particle params
    double m_S = 1885.0; double g_S = 1.0;
    double m_L = 1115.68; double g_L = 2.0;
    double m_p = 938.27; double g_p = 2.0;
    double m_n = 939.57; double g_n = 2.0;

    std::cout << "\n========================================================================\n";
    std::cout << std::setw(8) << "T[MeV]" 
              << std::setw(15) << "muB[MeV]" 
              << std::setw(15) << "muQ[MeV]" 
              << std::setw(15) << "nS/nB" 
              << std::setw(15) << "nLambda/nB" 
              << std::setw(15) << "nNucleon/nB" << "\n";
    std::cout << "------------------------------------------------------------------------\n";

    std::vector<double> guess = {0.001, 0.001, 0.001, 0.001, 0.001};

    for (double T = 100.0; T >= 30.0; T -= 5.0) {
        auto eq = GetEq::getEquations(T, le, lmu, ltau, b, nf);
        std::vector<double> target(5, 0.0);
        
        try {
            std::vector<double> sol = Solver::solveSystem(eq, target, guess, 1e-6, 100);
            guess = sol; // use as next guess
            
            double muB = sol[0];
            double muQ = sol[1];
            double muS = 0.0;
            double nB_tot = QCD::BarDens(muB, muQ, T, nf);
            
            auto mb_net_dens = [](double mu, double T, double m, double g) {
                double K2 = gsl_sf_bessel_Kn(2, m/T);
                return 2.0 * g / (2.0 * M_PI * M_PI) * m * m * T * K2 * std::sinh(mu / T);
            };

            // S (Boson): B=2, Q=0, S=-2
            double mu_S = 2.0*muB + 0.0*muQ - 2.0*muS;
            double dens_S = mb_net_dens(mu_S, T, m_S, g_S);
            
            // Lambda (Fermion): B=1, Q=0, S=-1
            double mu_L = 1.0*muB + 0.0*muQ - 1.0*muS;
            double dens_L = mb_net_dens(mu_L, T, m_L, g_L);
            
            // Proton: B=1, Q=1, S=0
            double mu_p_val = 1.0*muB + 1.0*muQ;
            double dens_p = mb_net_dens(mu_p_val, T, m_p, g_p);
            
            // Neutron: B=1, Q=0, S=0
            double mu_n_val = 1.0*muB + 0.0*muQ;
            double dens_n = mb_net_dens(mu_n_val, T, m_n, g_n);
            
            double dens_Nucleon = dens_p + dens_n;
            
            std::cout << std::fixed << std::setprecision(2) << std::setw(8) << T 
                      << std::scientific << std::setprecision(4)
                      << std::setw(15) << muB 
                      << std::setw(15) << muQ 
                      << std::setw(15) << dens_S/nB_tot 
                      << std::setw(15) << dens_L/nB_tot 
                      << std::setw(15) << dens_Nucleon/nB_tot << "\n";
            
        } catch(const std::exception& e) {
            std::cout << "T=" << T << " FAILED: " << e.what() << "\n";
        }
    }
    
    QCD::cleanup();
    return 0;
}
