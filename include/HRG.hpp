#ifndef HRG_HPP
#define HRG_HPP

#include <array>
#include <string>

namespace HRG {

/**
 * @brief Quantum van der Waals hadron resonance gas (QvdW-HRG).
 *
 * This is the low-temperature ("seam") model of the constant-entropy-contour
 * equation of state, and it mirrors `scontours::QvdWHRG`
 * (s_contours_c-dev4-tristan, `src/QvdWReference.cpp`) one-to-one: the same
 * Thermal-FIST model class (`ThermalModelVDWFull`), the same interaction
 * matrices, the same defaults. Any number produced here should agree with the
 * reference implementation to round-off.
 *
 * Physics (V. Vovchenko, M.I. Gorenstein, H. Stoecker, PRL 118, 182301 (2017);
 * V. Vovchenko et al., PRC 96, 045202 (2017)):
 *   - van der Waals attraction `a` and eigenvolume `b` act between
 *     baryon-baryon and antibaryon-antibaryon pairs ONLY (B_i * B_j > 0);
 *     mesons and baryon-antibaryon pairs are non-interacting.
 *   - The shifted chemical potentials mu*_i = mu_i - sum_j b_ij P_j^id(T,mu*_j)
 *     + sum_j (a_ij + a_ji) n_j are solved self-consistently (Broyden).
 *   - Quantum statistics and finite resonance widths are on by default.
 *   - Optionally, pions and kaons are described by the ChPT-matched effective
 *     mass model, which regularizes pion/kaon Bose condensation at large
 *     |mu_Q| instead of letting the ideal Bose integrals diverge. This is what
 *     the paper's production setup (`eos_line --qvdw`) uses, and it replaces
 *     the excluded-volume regularization of the previous ideal-HRG code here.
 *
 * The previous ideal / excluded-volume HRG that this file used to implement is
 * gone: `setExcludedVolumes()` survives only as a compatibility shim mapping the
 * baryon eigenvolume onto the QvdW `b` (see below).
 *
 * Units, unchanged from the previous interface:
 *   T, mu : MeV
 *   returned ratios are dimensionless (P/T^4, n_X/T^3, s/T^3).
 *
 * Requires Thermal-FIST (`external/Thermal-FIST`, wired up by
 * gui/CMakeLists.txt). Built without it, initialize() throws.
 */

/** QvdW-HRG model configuration; defaults are `scontours::QvdWHRG::Params`
 *  except for the effective-mass pions/kaons, which default ON here to match
 *  the production boundary of the paper's EoS export (`eos_line --qvdw`, which
 *  sets useEMMPions = useEMMKaons = true) -- the cosmic-trajectory application
 *  runs at low T where pion condensation must be regularized. */
struct Params {
  /** PDG particle list; empty -> Thermal-FIST's bundled PDG2020 list.dat. */
  std::string particleList;
  double a = 329.0; /**< baryon-baryon attraction, MeV fm^3 */
  double b = 3.42;  /**< baryon-baryon eigenvolume, fm^3 */
  bool quantumStatistics = true;
  bool resonanceWidth = true;
  bool useEMMPions = true;
  double emmPionFPi = 0.133; /**< GeV */
  bool useEMMKaons = true;
  double emmKaonFKa = 0.160; /**< GeV */
};

/** Result struct: unchanged, so callers (EntrCont, EntrContParam) need no edit. */
struct Result {
  double P_T4;  // P / T^4
  double nB_T3; // n_B / T^3
  double nQ_T3; // n_Q / T^3
  double nS_T3; // n_S / T^3
  double s_T3;  // s   / T^3
};

/**
 * Build the QvdW-HRG model. @p listPath is a Thermal-FIST particle list
 * (the bundled `list-PDG2020.dat` under EntroContourEoS/HRG is exactly
 * Thermal-FIST's own PDG2020 list); empty, or a path that does not exist,
 * falls back to Thermal-FIST's bundled list.
 *
 * Constructing the model is the expensive part (particle list + 700x700
 * interaction matrices), so it is done once and reused for every eval().
 */
void initialize(const std::string &listPath = "");
void initialize(const Params &params);

void cleanup();
bool isInitialized();

/** Number of particle species loaded. Returns 0 if not initialized. */
int particleCount();

/** Current model configuration. */
const Params &parameters();

/**
 * Compatibility shim for the previous excluded-volume interface, kept so that
 * EntrCont.cpp / QCDTherm.cpp keep compiling unchanged.
 *
 * In the QvdW-HRG mesons are non-interacting, so @p b_meson_fm3 has no
 * counterpart and is ignored; @p b_baryon_fm3 is the QvdW eigenvolume `b`.
 * The existing call sites pass (1.0, 3.42), and 3.42 fm^3 is exactly the
 * paper's b -- so the shim reproduces the reference parameters.
 */
void setExcludedVolumes(double b_meson_fm3, double b_baryon_fm3);
double getMesonExcludedVolume();  /**< always 0 (mesons are ideal) */
double getBaryonExcludedVolume(); /**< the QvdW eigenvolume b, fm^3 */

/** Set the QvdW interaction parameters (a in MeV fm^3, b in fm^3) and rebuild
 *  the interaction matrices. Invalidates the evaluation cache. */
void setVdWParameters(double a_MeV_fm3, double b_fm3);

/**
 * Evaluate the QvdW-HRG at (T, muB, muQ, muS), all in MeV.
 *
 * ONE model solve per call: the vdW shifted chemical potentials are solved once
 * and P, n_B, n_Q, n_S and s are all read off that single solution. Solving the
 * QvdW equations is by far the dominant cost, so callers that need several
 * thermodynamic quantities at the same point must use this rather than separate
 * per-quantity entry points. Results are memoized on the exact
 * (T, muB, muQ, muS), which makes repeated queries at the same point free.
 */
Result eval(double T_MeV, double muB_MeV, double muQ_MeV, double muS_MeV);

/** Energy density in MeV^4 at the same point (uses the memoized solve). */
double energyDensity(double T_MeV, double muB_MeV, double muQ_MeV,
                     double muS_MeV);

/**
 * d^2 P / dmu_i dmu_j in MeV^2, indices {0:B, 1:Q, 2:S} -- Thermal-FIST's
 * analytic conserved-charge susceptibility matrix, the same quantity
 * `scontours::QvdWHRG::susceptibilitiesD2P` feeds to the EoS as the
 * low-temperature boundary curvature. Needs the fluctuation solve on top of
 * eval(), so it is a separate (more expensive) call.
 */
std::array<std::array<double, 3>, 3>
susceptibilitiesD2P(double T_MeV, double muB_MeV, double muQ_MeV,
                    double muS_MeV);

} // namespace HRG

#endif // HRG_HPP
