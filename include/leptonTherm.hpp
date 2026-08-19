#ifndef LEPTONTHERM_HPP
#define LEPTONTHERM_HPP

#include "JEL.hpp"

namespace lepton {

// Constants
constexpr double me = 0.511;        // Electron mass (MeV)
constexpr double ge = 2.0;          // Electron degeneracy
constexpr double mmu = 105.6583755; // Muon mass (MeV)
constexpr double gmu = 2.0;         // Muon degeneracy
constexpr double mtau = 1.7768e3;   // Tau mass (MeV)
constexpr double gtau = 2.0;        // Tau degeneracy
constexpr double m_nue = 0.0;       // Neutrino masses
constexpr double m_numu = 0.0;      // Neutrino masses
constexpr double m_nutau = 0.0;     // Neutrino masses
constexpr double gnu = 1.0;         // Neutrino degeneracy
constexpr double gphoton = 2.0;     // Photon degeneracy

// Electron sector net density (e+nue)
double ne(double muQ, double munue, double T);

// Muon sector net density (mu+numu)
double nmu(double muQ, double munumu, double T);

// Tauon sector net density
double ntau(double muQ, double mnutau, double T);

// Total lepton charge density
double Qlep(double muQ, double munue, double munumu, double mnutau, double T);

// Total lepton+photons entropy density
double slep(double muQ, double munue, double munumu, double mnutau, double T);

// Total lepton+photons pressure
double plep(double muQ, double munue, double munumu, double mnutau, double T);

// Total lepton+photons energy density
double elep(double muQ, double munue, double munumu, double mnutau, double T);

} // namespace lepton

#endif // LEPTONTHERM_HPP
