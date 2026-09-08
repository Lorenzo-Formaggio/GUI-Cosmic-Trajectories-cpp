#ifndef QCDTHERM_HPP
#define QCDTHERM_HPP

#include "JEL.hpp"
#include <string>

namespace QCD {

// Quark masses (MeV)
constexpr double mu = 2.3;
constexpr double md = 4.8;
constexpr double mc = 1275.0;
constexpr double ms = 95.0;

// Quark and gluon degeneracy
constexpr double gq = 6.0;
constexpr double ggluon = 16.0;

/**
 * @brief Set the equation of state type.
 * @param eos         0 = free QGP, 1 = lattice QCD, 2 = Interpolated Table,
 *                    3 = Entropy Contour (tabulated chis, QvdW-HRG seam),
 *                    4 = Entropy Contour Param (parametrized chis, QvdW-HRG seam),
 *                    5 = Entropy Contour with the Gibbs mixed phase,
 *                    6 = Entropy Contour Param with the Gibbs mixed phase,
 *                    7 = Entropy Contour Param-Legacy with the Gibbs mixed phase
 *                        (all six susceptibility fits, no isospin relations)
 *                    (for 5, 6 and 7 the "muB" argument of every function below
 *                    is the stretched coordinate x of GibbsMixedPhase.hpp)
 * @param dataPath    Base path (typically the working directory) for EoS data files
 * @param nf          Number of active quark flavors (3 or 4)
 * @param interpType  Lattice QCD interpolation order (0=Cubic Spline, 1=Linear, 2=Akima, 3=Steffen)
 */
void setEoS(int eos, const std::string &dataPath = "",
            int nf = 3, int interpType = 0);

/**
 * @brief Get current EoS type.
 */
int getEoS();

/**
 * @brief Cleanup resources (call at program end).
 */
void cleanup();

/** True for the Gibbs mixed-phase variants (eos = 5, 6, 7). */
bool isGibbs(int eos);

/**
 * For eos = 5, 6 and 7 the trajectory solver's first unknown is the stretched
 * coordinate x of GibbsMixedPhase.hpp, not mu_B. These map it back: the
 * physical mu_B, and the dilute-phase volume fraction (NaN outside the mixed
 * phase). For every other EoS they are the identity and NaN.
 */
double physicalMuB(double x, double muQ, double T);
double phaseFraction(double x, double muQ, double T);
/** Inverse map for a homogeneous state at physical mu_B (identity unless 5/6). */
double coordinateFromMuB(double muB, double muQ, double T);

// Baryon density (3 or 4 flavors based on nf)
// Note: nf is ignored when using lattice QCD EoS (always 3 flavors)
double BarDens(double muB, double muQ, double T, int nf);

// Total QCD charge density
double QCDcharge(double muB, double muQ, double T, int nf);

// Total QCD entropy density
double sQCD(double muB, double muQ, double T, int nf);

// Total QCD pressure
double pQCD(double muB, double muQ, double T, int nf);

// Total QCD energy density
double eQCD(double muB, double muQ, double T, int nf);

} // namespace QCD

#endif // QCDTHERM_HPP
