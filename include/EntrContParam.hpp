#ifndef ENTRCONTPARAM_HPP
#define ENTRCONTPARAM_HPP

#include <string>
#include <vector>

namespace EntropyContoursParam {

/**
 * @brief Per-(muB, muQ) contour state.
 *
 * Holds the precomputed contour arrays evaluated on the T0 grid for a fixed
 * (muB, muQ) point (with muS = 0 enforced). Reuse this struct to query the
 * thermodynamic functions at multiple temperatures along the same contour
 * without recomputing.
 *
 * Density values are stored as dimensionless n/T^3 and entropy as s/T^3,
 * indexed on the T0 grid. T_phys[i] is the physical temperature corresponding
 * to T0g[i] under the contour T = T0 + 0.5 * a2(T0) * mu^2.
 */
struct ContourValues {
  double muB = 0.0;
  double muQ = 0.0;

  // Per-T0 grid arrays (size = N_T0).
  std::vector<double> T_phys; // physical T(T0) along contour
  std::vector<double> dTdT0;  // Jacobian dT/dT0 (for stability check)
  std::vector<double> nB_T3;  // n_B / T^3
  std::vector<double> nS_T3;  // n_S / T^3
  std::vector<double> nQ_T3;  // n_Q / T^3
  std::vector<double> s_T3;   // s   / T^3
  std::vector<double> P_T4;   // P   / T^4 (used for Maxwell construction)
};

/**
 * @brief Initialize the EntropyContours equation of state.
 *
 * Loads the lattice susceptibilities (chi2B, chi2Q, chi2S, chi11BS, chi11BQ,
 * chi11SQ) and the entropy spline from @p chisDir, plus the HRG anchor slice
 * from @p hrgPath (file format identical to the QvdW-HRG file consumed by
 * main.cpp's --CT mode). Must be called before any other function.
 *
 * @param chisDir Directory containing Chi*_30-2000.dT1 files and
 *                entro_2013_hrg+extrap.spln. Default: "chis".
 * @param hrgPath Path to the (muB, muQ)-plane HRG file at T = 80 MeV.
 *                Default: "HRG/muB-muQ-plane_QvdW_interacting_pions_filled_B.dat"
 *                (the NaN-filled file produced by mimic/fill_hrg.py).
 * @param useHRG  If false, skip HRG anchoring: pressure is set with a
 *                P_base[0]=0 convention and densities are not shifted
 *                (matches main.cpp's non-CT mode). Default: true.
 */
// Note: the implementation file requires the PDG list path (for HRG::initialize)
// and the meson/baryon excluded volumes (for HRG::setExcludedVolumes), so the
// 5-argument form below mirrors that of EntrCont::initialize.
void initialize(const std::string &chisDir,
                const std::string &pdgListPath,
                double b_meson_fm3,
                double b_baryon_fm3,
                bool useHRG = true);

/** Free internal buffers. */
void cleanup();

/** True iff initialize() has been called and not yet cleaned up. */
bool isInitialized();

/**
 * @brief Evaluate the contour arrays at a given (muB, muQ) point.
 *
 * The strangeness chemical potential muS is taken to be 0 (strangeness
 * neutrality), matching main.cpp's --CT mode. The returned struct can be
 * passed to the density/entropy functions to evaluate at multiple
 * temperatures without redoing the contour computation.
 *
 * @param muB Baryon chemical potential (MeV).
 * @param muQ Electric charge chemical potential (MeV).
 */
ContourValues evalContour(double muB, double muQ);

// ============================================================================
// Thermodynamic functions taking a precomputed ContourValues (efficient).
// All assume muS = 0. The (muB, muQ) parameters must match those used to
// build the ContourValues; they are accepted for API symmetry with LatticeQCD.
// Returns dimensional density in MeV^3 (or MeV^3 for entropy density too).
// ============================================================================

/** Entropy density s (MeV^3). */
double sQCD(double muB, double muQ, double T, const ContourValues &c);

/** Baryon density n_B (MeV^3). */
double BarDens(double muB, double muQ, double T, const ContourValues &c);

/** Electric charge density n_Q (MeV^3). */
double QCDcharge(double muB, double muQ, double T, const ContourValues &c);

/** Strangeness density n_S (MeV^3). */
double StrDens(double muB, double muQ, double T, const ContourValues &c);

// ============================================================================
// Convenience overloads (build the contour internally - less efficient if
// multiple observables are needed at the same (muB, muQ)).
// ============================================================================

double sQCD(double muB, double muQ, double T);
double BarDens(double muB, double muQ, double T);
double QCDcharge(double muB, double muQ, double T);
double StrDens(double muB, double muQ, double T);

} // namespace EntropyContoursParam

#endif // ENTRCONT_HPP
