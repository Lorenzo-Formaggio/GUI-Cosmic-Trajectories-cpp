#include "../include/GetEq.hpp"
#include "../include/QCDTherm.hpp"
#include "../include/leptonTherm.hpp"

namespace GetEq {

double TotalQ(double muB, double muQ, double munue, double munumu,
              double mnutau, double T, int nf) {
  return lepton::Qlep(muQ, munue, munumu, mnutau, T) +
         QCD::QCDcharge(muB, muQ, T, nf);
}

double TotalS(double muB, double muQ, double munue, double munumu,
              double mnutau, double T, int nf) {
  return lepton::slep(muQ, munue, munumu, mnutau, T) +
         QCD::sQCD(muB, muQ, T, nf);
}

double TotalP(double muB, double muQ, double munue, double munumu,
              double mnutau, double T, int nf) {
  return lepton::plep(muQ, munue, munumu, mnutau, T) +
         QCD::pQCD(muB, muQ, T, nf);
}

double TotalE(double muB, double muQ, double munue, double munumu,
              double mnutau, double T, int nf) {
  return lepton::elep(muQ, munue, munumu, mnutau, T) +
         QCD::eQCD(muB, muQ, T, nf);
}

std::vector<Solver::SystemFunction>
getEquations(double T, double le, double lmu, double ltau, double b, int nf) {
  std::vector<Solver::SystemFunction> functions;

  // 1) ne / s_tot = le
  functions.push_back([le, T, nf](const std::vector<double> &vars) {
    double muB = vars[0], muQ = vars[1], munue = vars[2], munumu = vars[3],
           mnutau = vars[4];
    return lepton::ne(muQ, munue, T) -
           le * TotalS(muB, muQ, munue, munumu, mnutau, T, nf);
  });

  // 2) nmu / s_tot = lmu
  functions.push_back([lmu, T, nf](const std::vector<double> &vars) {
    double muB = vars[0], muQ = vars[1], munue = vars[2], munumu = vars[3],
           mnutau = vars[4];
    return lepton::nmu(muQ, munumu, T) -
           lmu * TotalS(muB, muQ, munue, munumu, mnutau, T, nf);
  });

  // 3) ntau / s_tot = ltau
  functions.push_back([ltau, T, nf](const std::vector<double> &vars) {
    double muB = vars[0], muQ = vars[1], munue = vars[2], munumu = vars[3],
           mnutau = vars[4];
    return lepton::ntau(muQ, mnutau, T) -
           ltau * TotalS(muB, muQ, munue, munumu, mnutau, T, nf);
  });

  // 4) Total charge = 0
  functions.push_back([T, nf](const std::vector<double> &vars) {
    double muB = vars[0], muQ = vars[1], munue = vars[2], munumu = vars[3],
           mnutau = vars[4];
    return TotalQ(muB, muQ, munue, munumu, mnutau, T, nf);
  });

  // 5) Baryon number conservation: nB / s_tot = b
  functions.push_back([T, b, nf](const std::vector<double> &vars) {
    double muB = vars[0], muQ = vars[1], munue = vars[2], munumu = vars[3],
           mnutau = vars[4];
    return QCD::BarDens(muB, muQ, T, nf) -
           b * TotalS(muB, muQ, munue, munumu, mnutau, T, nf);
  });

  return functions;
}

ErrorValues getErrors(const std::vector<double> &solution, double T, double le,
                      double lmu, double ltau, double b, int nf) {
  double muB = solution[0], muQ = solution[1], munue = solution[2],
         munumu = solution[3], mnutau = solution[4];

  double s_tot = TotalS(muB, muQ, munue, munumu, mnutau, T, nf);

  ErrorValues err;
  err.err_b = (b == 0.0) ? (b - (QCD::BarDens(muB, muQ, T, nf) / s_tot)) : (b - (QCD::BarDens(muB, muQ, T, nf) / s_tot)) / b;
  err.err_charge = TotalQ(muB, muQ, munue, munumu, mnutau, T, nf);
  err.err_le = (le == 0.0) ? (le - (lepton::ne(muQ, munue, T) / s_tot)) : (le - (lepton::ne(muQ, munue, T) / s_tot)) / le;
  err.err_lmu = (lmu == 0.0) ? (lmu - (lepton::nmu(muQ, munumu, T) / s_tot)) : (lmu - (lepton::nmu(muQ, munumu, T) / s_tot)) / lmu;
  err.err_ltau = (ltau == 0.0) ? (ltau - (lepton::ntau(muQ, mnutau, T) / s_tot)) : (ltau - (lepton::ntau(muQ, mnutau, T) / s_tot)) / ltau;

  return err;
}

} // namespace GetEq
