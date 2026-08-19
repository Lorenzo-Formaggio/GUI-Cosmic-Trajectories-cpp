#include "../include/leptonTherm.hpp"

namespace lepton {

// electron sector net density (e+nue)
double ne(double muQ, double munue, double T) {
  return jelf::nNet(munue - muQ, T, me, ge) + jelf::nNet(munue, T, m_nue, gnu);
};

// muon sector net density (mu+numu)
double nmu(double muQ, double munumu, double T) {
  return jelf::nNet(munumu - muQ, T, mmu, gmu) +
         jelf::nNet(munumu, T, m_numu, gnu);
};

// tauon sector net density (tau+nutau)
double ntau(double muQ, double mnutau, double T) {
  return jelf::nNet(mnutau - muQ, T, mtau, gtau) +
         jelf::nNet(mnutau, T, m_nutau, gnu);
};

// Total lepton charge density
double Qlep(double muQ, double munue, double munumu, double mnutau, double T) {
  return -jelf::nNet(munue - muQ, T, me, ge) -
         jelf::nNet(munumu - muQ, T, mmu, gmu) -
         jelf::nNet(mnutau - muQ, T, mtau, gtau);
};

// Total lepton+photons entropy density
double slep(double muQ, double munue, double munumu, double mnutau, double T) {
  return jelf::sTot(munue - muQ, T, me, ge) + jelf::sTot(munue, T, m_nue, gnu) +
         jelf::sTot(munumu - muQ, T, mmu, gmu) +
         jelf::sTot(munumu, T, m_numu, gnu) +
         jelf::sTot(mnutau - muQ, T, mtau, gtau) +
         jelf::sTot(mnutau, T, m_nutau, gnu) + jelb::sTot(0, T, 0, gphoton);
};

// Total lepton+photons pressure
double plep(double muQ, double munue, double munumu, double mnutau, double T) {
  return jelf::PTot(munue - muQ, T, me, ge) + jelf::PTot(munue, T, m_nue, gnu) +
         jelf::PTot(munumu - muQ, T, mmu, gmu) +
         jelf::PTot(munumu, T, m_numu, gnu) +
         jelf::PTot(mnutau - muQ, T, mtau, gtau) +
         jelf::PTot(mnutau, T, m_nutau, gnu) + jelb::PTot(0, T, 0, gphoton);
};

// Total lepton+photons energy density
double elep(double muQ, double munue, double munumu, double mnutau, double T) {
  return jelf::eTot(munue - muQ, T, me, ge) + jelf::eTot(munue, T, m_nue, gnu) +
         jelf::eTot(munumu - muQ, T, mmu, gmu) +
         jelf::eTot(munumu, T, m_numu, gnu) +
         jelf::eTot(mnutau - muQ, T, mtau, gtau) +
         jelf::eTot(mnutau, T, m_nutau, gnu) + jelb::eTot(0, T, 0, gphoton);
};
} // namespace lepton