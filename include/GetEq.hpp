#ifndef GETEQ_HPP
#define GETEQ_HPP

#include "Solver.hpp"
#include <vector>

namespace GetEq {

/**
 * @brief Returns the total charge density (Lepton + QCD).
 */
double TotalQ(double muB, double muQ, double munue, double munumu,
              double mnutau, double T, int nf);

/**
 * @brief Returns the total entropy density (Lepton + QCD).
 */
double TotalS(double muB, double muQ, double munue, double munumu,
              double mnutau, double T, int nf);

/**
 * @brief Struct to hold calculated residual errors.
 */
struct ErrorValues {
  double err_b;
  double err_charge;
  double err_le;
  double err_lmu;
  double err_ltau;
};

/**
 * @brief Calculates relative and absolute errors for the solution.
 */
ErrorValues getErrors(const std::vector<double> &solution, double T, double le,
                      double lmu, double ltau, double b, int nf);

/**
 * @brief Returns the system of 5 equations for the lepton and QCD sectors.
 *
 * @param T Temperature (MeV)
 * @param le Electron asymmetry
 * @param lmu Muon asymmetry
 * @param ltau Tau asymmetry
 * @param b Baryon asymmetry
 * @param nf Number of quark flavors (3 or 4)
 * @return std::vector<Solver::SystemFunction> The vector of 5 functions.
 */
std::vector<Solver::SystemFunction>
getEquations(double T, double le, double lmu, double ltau, double b, int nf);

} // namespace GetEq

#endif // GETEQ_HPP
