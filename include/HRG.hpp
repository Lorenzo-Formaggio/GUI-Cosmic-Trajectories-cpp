#ifndef HRG_HPP
#define HRG_HPP

#include <string>

namespace HRG {

/**
 * @brief Ideal hadron-resonance-gas thermodynamics, sums over a particle list.
 *
 * Conventions and particle-list format follow Thermal-FIST
 * (https://github.com/vlvovch/Thermal-FIST). Only particles are listed; the
 * antiparticle contribution is added implicitly via a 2*cosh(n*mu_i/T) factor
 * for any species with non-zero (B, Q, S, C). Cluster expansion is truncated
 * at N_TRUNC terms.
 *
 * All inputs/outputs are in units consistent with main.cpp / EntrCont.cpp:
 *   T, mu : MeV
 *   returned ratios are dimensionless (P/T^4, n_X/T^3, s/T^3).
 */

/** Result struct mirrors HRGSlice::lookup outputs. */
struct Result {
  double P_T4;  // P / T^4
  double nB_T3; // n_B / T^3
  double nQ_T3; // n_Q / T^3
  double nS_T3; // n_S / T^3
  double s_T3;  // s   / T^3
};

/**
 * Load the particle list. File format = Thermal-FIST PDG2020 list.dat:
 *   pdg name stable mass[GeV] degeneracy statistics B Q S C |S| |C| width thresh
 * Lines starting with '#' are comments. statistics = -1 (boson) or +1 (fermion).
 *
 * Default path: data/lattice/HRG/list-PDG2020.dat (relative to CWD).
 */
void initialize(const std::string &listPath =
                    "data/lattice/HRG/list-PDG2020.dat");

void cleanup();
bool isInitialized();

/**
 * Number of particles loaded. Returns 0 if not initialized.
 */
int particleCount();

/**
 * Enable diagonal excluded-volume HRG (Vovchenko/Stoecker EV-HRG):
 * each species has v_i = b_meson_fm3 (mesons, B=0) or b_baryon_fm3 (B!=0).
 * The chemical potential is shifted self-consistently as
 *   mu_i_eff = mu_i - v_i * P,
 * and the densities pick up a 1/(1 + sum_j v_j n_j^id) factor. This handles
 * pion BEC (and high-density saturation generally) physically.
 *
 * Set both to 0 (default) to recover ideal HRG. Typical Thermal-FIST
 * defaults: b_baryon ~ 1-3.4 fm^3 (radius 0.31-0.5 fm), b_meson 0 or ~1 fm^3
 * if pion repulsion is desired (the "interacting pions" model).
 */
void setExcludedVolumes(double b_meson_fm3, double b_baryon_fm3);
double getMesonExcludedVolume();
double getBaryonExcludedVolume();

/**
 * Evaluate ideal-HRG thermodynamics at (T, muB, muQ, muS).
 * T, mu in MeV. muS=0 for the cosmic-trajectory / strangeness-neutral case.
 */
Result eval(double T_MeV, double muB_MeV, double muQ_MeV, double muS_MeV);

} // namespace HRG

#endif // HRG_HPP
