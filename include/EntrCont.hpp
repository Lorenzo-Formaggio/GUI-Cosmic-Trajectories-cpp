#ifndef ENTRCONT_HPP
#define ENTRCONT_HPP

#include "ContourEoSCore.hpp"

#include <string>

/**
 * @file EntrCont.hpp
 * @brief Constant-entropy-density contour equation of state on the *tabulated*
 *        lattice susceptibilities.
 *
 * Same contour algebra as EntrContParam -- both use `ContourEoSCore.hpp`, the
 * port of `scontours::EquationOfState` from s_contours_c-dev4-tristan, and both
 * anchor on the QvdW-HRG of `HRG.hpp` at Tlow = 80 MeV. The two models differ
 * in exactly one thing, their mu = 0 lattice input:
 *
 *   EntrCont      (this file)  -- cubic splines through the tabulated
 *                                 Wuppertal-Budapest susceptibilities in
 *                                 `EntroContourEoS/chis/` and the entropy
 *                                 density in `entro_2013_hrg+extrap.spln`
 *   EntrContParam              -- the reference's closed-form fits to the same
 *                                 data, with exact T-derivatives
 *
 * So comparing eos = 3 against eos = 4 in the GUI isolates the effect of the
 * lattice input representation, which is the only reason to keep both. The
 * closed-form fits are good to about 200 MeV; the tabulated input here follows
 * the lattice data to 800 MeV, so this is the model to use above the crossover
 * (a cosmic trajectory spends most of its range there).
 *
 * Two caveats specific to the tabulated input, both consequences of the files:
 *   - the entropy table stops at 800 MeV while the susceptibilities run to
 *     2000 MeV; above the end of a table the splines are continued linearly
 *     (C^1), which for s/T^3 happens to track the Stefan-Boltzmann approach
 *     closely, but it is an extrapolation
 *   - alpha2' needs a second T-derivative of the lattice input, which the
 *     closed-form model has exactly and a spline through data has only
 *     approximately; the data is Gaussian-smoothed before splining to keep that
 *     second derivative usable
 *
 * All temperatures and chemical potentials in MeV; pressure in MeV^4, densities
 * and entropy density in MeV^3.
 */

namespace EntropyContours {

/** Thermodynamics at one resolved contour anchor; see ContourEoSCore.hpp. */
using AnchorResult = ContourEoS::AnchorResult;

/**
 * @brief Per-direction contour state; see ContourEoS::Contour.
 *
 * The same type backs EntropyContoursParam::ContourValues, but the contours are
 * not interchangeable between the two models: each carries a pointer to the
 * engine that built it.
 */
using ContourValues = ContourEoS::Contour;

/**
 * @brief Initialize the equation of state.
 *
 * @param chisDir      Directory holding `Chi{2B,2S,2Q,11BS,11BQ,11SQ}_30-2000.dT1`
 *                     and `entro_2013_hrg+extrap.spln`.
 * @param pdgListPath  Thermal-FIST particle list for the QvdW-HRG boundary;
 *                     empty falls back to Thermal-FIST's bundled PDG2020 list.
 * @param b_meson_fm3  Ignored: mesons are non-interacting in the QvdW-HRG.
 * @param b_baryon_fm3 QvdW baryon eigenvolume b [fm^3] (3.42 in the paper).
 * @param useHRG       If false, skip the QvdW-HRG boundary: the pressure uses
 *                     the bare contour convention P(Tlow, mu) = p0(Tlow) and the
 *                     densities get no boundary contribution.
 */
void initialize(const std::string &chisDir,
                const std::string &pdgListPath,
                double b_meson_fm3 = 1.0,
                double b_baryon_fm3 = 3.42,
                bool useHRG = true);

void cleanup();
bool isInitialized();

/** Low-temperature boundary temperature Tlow [MeV] (80, as in the paper). */
double referenceTemperature();

/**
 * @brief Build the contour for a given chemical potential point.
 *
 * Performs exactly one QvdW-HRG solve (at the seam), which dominates the cost --
 * cache the result and reuse it for every observable and temperature.
 */
ContourValues evalContour(double muB, double muQ, double muS);
/** muS = 0 (strangeness-neutral cosmic-trajectory case), as before. */
ContourValues evalContour(double muB, double muQ);

// ============================================================================
// Thermodynamic functions taking a precomputed ContourValues (efficient).
// The (muB, muQ) parameters are accepted for API symmetry and are ignored; the
// point is the one stored in the ContourValues.
// ============================================================================

double sQCD(double muB, double muQ, double T, const ContourValues &c);
double BarDens(double muB, double muQ, double T, const ContourValues &c);
double QCDcharge(double muB, double muQ, double T, const ContourValues &c);
double StrDens(double muB, double muQ, double T, const ContourValues &c);
double pQCD(double muB, double muQ, double T, const ContourValues &c);
/** Energy density eps = T s - P + sum_X mu_X n_X (MeV^4). */
double eQCD(double muB, double muQ, double T, const ContourValues &c);

// ============================================================================
// Convenience overloads (build the contour internally - much less efficient if
// several observables are needed at the same point).
// ============================================================================

double sQCD(double muB, double muQ, double T);
double BarDens(double muB, double muQ, double T);
double QCDcharge(double muB, double muQ, double T);
double StrDens(double muB, double muQ, double T);
double pQCD(double muB, double muQ, double T);
double eQCD(double muB, double muQ, double T);

} // namespace EntropyContours

#endif // ENTRCONT_HPP
