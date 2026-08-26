#ifndef ENTRCONTPARAM_HPP
#define ENTRCONTPARAM_HPP

#include "ContourEoSCore.hpp"

#include <string>

/**
 * @file EntrContParam.hpp
 * @brief Constant-entropy-density contour equation of state, matched to
 *        s_contours_c-dev4-tristan.
 *
 * The contour algebra itself lives in `ContourEoSCore.hpp`, shared with
 * EntrCont; this file supplies only the mu = 0 lattice input, which is what
 * distinguishes the two models. Here it is the closed-form Wuppertal-Budapest
 * fits of `scontours::LatticeInput` with the symbolically generated exact
 * T-derivatives, so the model reproduces s_contours_c-dev4-tristan's
 * `scontours::EquationOfState` numerically.
 *
 * VALIDITY: those parametrizations are fits to the crossover region and are
 * good up to about 200 MeV. Above that they are outside their fitted range --
 * the tanh entropy fit saturates at s/T^3 = 12.09, where the lattice data keeps
 * rising (14.7 at 300 MeV, 17.4 at 800 MeV). Nothing here clamps or warns,
 * because the reference does not either and the contour stays smooth; but above
 * ~200 MeV use EntrCont, whose tabulated input follows the data to 800 MeV.
 *
 * Differences from the previous implementation in this file, all of which were
 * genuine deviations from the reference:
 *   - the entropy density carried a spurious extra + X2 mu^2 T term on top of
 *     s0(T0); along a constant-s contour s is exactly s0(T0)
 *   - the charge densities used T_phys^2 where the contour result has T0^2,
 *     and the T0low subtraction was only approximated by the seam shift
 *   - the pressure integral was a 1000-point trapezoid of s0 dT/dT0 dT0 rather
 *     than the closed-form antiderivative + Gauss-Legendre p0
 *   - the lattice inputs were an outdated chi2Q fit, an entropy spline read
 *     from disk, and independently fitted cross susceptibilities; the reference
 *     uses the current Wuppertal-Budapest fits, a closed-form tanh entropy, and
 *     (by default) isospin-derived chi11BS / chi11BQ
 *   - the seam anchor was an ideal / excluded-volume HRG, not the QvdW-HRG
 *
 * All temperatures and chemical potentials in MeV; pressure in MeV^4, densities
 * and entropy density in MeV^3.
 */

namespace EntropyContoursParam {

/**
 * @brief Cross-susceptibility scheme for the lattice input.
 *
 * Mirrors `scontours::LatticeInput::CrossMode`. IsospinDerived is the default
 * (the 4D paper's production setup): only chi2B, chi2Q, chi2S and chi11QS are
 * fitted, and chi11BS = 2 chi11QS - chi2S, chi11BQ = (chi2B + chi11BS)/2.
 */
enum class CrossMode { Fitted, IsospinDerived };

/** Thermodynamics at one resolved contour anchor; see ContourEoSCore.hpp. */
using AnchorResult = ContourEoS::AnchorResult;

/**
 * @brief Per-direction contour state; see ContourEoS::Contour.
 *
 * The same type backs EntropyContours::ContourValues -- the two models share
 * the contour engine and differ only in their mu = 0 lattice input -- but the
 * contours are not interchangeable between them: each carries a pointer to the
 * engine that built it.
 */
using ContourValues = ContourEoS::Contour;

/**
 * @brief Initialize the equation of state.
 *
 * @param chisDir   Ignored, kept for source compatibility with the previous
 *                  signature (and with EntrCont's). The susceptibilities and
 *                  the entropy density are now closed-form Wuppertal-Budapest
 *                  fits compiled in, so no file is read.
 * @param pdgListPath  Thermal-FIST particle list for the QvdW-HRG boundary;
 *                  empty falls back to Thermal-FIST's bundled PDG2020 list.
 * @param b_meson_fm3  Ignored: mesons are non-interacting in the QvdW-HRG.
 * @param b_baryon_fm3 QvdW baryon eigenvolume b [fm^3] (3.42 in the paper).
 * @param useHRG    If false, skip the QvdW-HRG boundary: the pressure uses the
 *                  bare contour convention P(Tlow, mu) = p0(Tlow) and the
 *                  densities get no boundary contribution. Relative
 *                  thermodynamics (dP/dT, dP/dmu) is unaffected; the absolute
 *                  pressure and n_X are.
 */
void initialize(const std::string &chisDir,
                const std::string &pdgListPath,
                double b_meson_fm3,
                double b_baryon_fm3,
                bool useHRG = true);

/** Free internal buffers. */
void cleanup();

/** True iff initialize() has been called and not yet cleaned up. */
bool isInitialized();

/** Select the cross-susceptibility scheme (default: IsospinDerived, matching
 *  the reference). Must be called before initialize(); it changes the
 *  precomputed lattice tables. */
void setCrossMode(CrossMode mode);
CrossMode crossMode();

/** Low-temperature boundary temperature Tlow [MeV] (80, as in the paper).
 *  Temperatures below it are outside the domain of the contour EoS. */
double referenceTemperature();

/**
 * @brief Build the contour for a given chemical potential point.
 *
 * The direction is n_hat = (mu_B, mu_Q, mu_S)/mu and the radial coordinate is
 * mu = |(mu_B, mu_Q, mu_S)|. This performs exactly one QvdW-HRG solve (at the
 * seam), which dominates the cost -- cache the result and reuse it for every
 * observable and temperature at this point.
 */
ContourValues evalContour(double muB, double muQ, double muS);
/** muS = 0 (strangeness-neutral cosmic-trajectory case), as before. */
ContourValues evalContour(double muB, double muQ);

// ============================================================================
// Thermodynamic functions taking a precomputed ContourValues (efficient).
// The (muB, muQ) parameters are accepted for API symmetry with LatticeQCD and
// are ignored; the point is the one stored in the ContourValues.
// Dimensional results: MeV^3 for densities and entropy density, MeV^4 for P.
// ============================================================================

/** Entropy density s (MeV^3). */
double sQCD(double muB, double muQ, double T, const ContourValues &c);

/** Baryon density n_B (MeV^3). */
double BarDens(double muB, double muQ, double T, const ContourValues &c);

/** Electric charge density n_Q (MeV^3). */
double QCDcharge(double muB, double muQ, double T, const ContourValues &c);

/** Strangeness density n_S (MeV^3). */
double StrDens(double muB, double muQ, double T, const ContourValues &c);

/** QCD pressure P (MeV^4). */
double pQCD(double muB, double muQ, double T, const ContourValues &c);

/** Energy density eps = T s - P + sum_X mu_X n_X (MeV^4). */
double eQCD(double muB, double muQ, double T, const ContourValues &c);

// ============================================================================
// Convenience overloads (build the contour internally - much less efficient if
// several observables are needed at the same point, since each one redoes the
// QvdW-HRG seam solve unless it hits that model's cache).
// ============================================================================

double sQCD(double muB, double muQ, double T);
double BarDens(double muB, double muQ, double T);
double QCDcharge(double muB, double muQ, double T);
double StrDens(double muB, double muQ, double T);
double pQCD(double muB, double muQ, double T);
double eQCD(double muB, double muQ, double T);

} // namespace EntropyContoursParam

#endif // ENTRCONTPARAM_HPP
