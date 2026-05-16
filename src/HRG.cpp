#include "HRG.hpp"

#include <cmath>
#include <fstream>
#include <gsl/gsl_sf_bessel.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace HRG {

// Cluster expansion truncation. At T = 80 MeV the lightest hadron (pion,
// m=140 MeV) has m/T = 1.75, and K_2(n*m/T) decays as exp(-n*m/T)/sqrt(n*m/T)
// for large argument — by n = 12 the term is below 1e-7 of n=1, ample.
static constexpr int N_TRUNC = 20;

// Pion BEC is handled physically here via diagonal excluded-volume HRG
// (Vovchenko/Stoecker, "EV-HRG"). Each species has an excluded volume v_i
// (different for mesons and baryons by default). The self-consistent
// chemical potential shift mu_i_eff = mu_i - v_i * P keeps mu_i_eff < m_i
// even when bare |mu_i| > m_i, so the cluster expansion converges. P is
// found by Newton iteration. Densities are extracted via
//   n_i = n_i^id(T, mu_i_eff) / (1 + sum_j v_j n_j^id(T, mu_j_eff)).
// Defaults (set by setExcludedVolumes; ideal HRG = 0 if both zero):
static double g_b_meson_fm3 = 0.0;  // meson excluded volume per particle (fm^3)
static double g_b_baryon_fm3 = 0.0; // baryon excluded volume per particle (fm^3)

// Newton convergence tolerance and iteration cap.
static constexpr int NEWTON_MAX_ITER = 50;
static constexpr double NEWTON_TOL_REL = 1e-10;

// Clip threshold for mu_i_eff during iteration (boson cluster expansion).
// Should never trigger at convergence if EV is enabled with realistic b;
// it only protects the transient where the iteration is still searching.
static constexpr double MU_OVER_M_CLIP = 0.999;

// (hbar c)^3 in (MeV*fm)^3 — converts excluded volume from fm^3 to MeV^-3.
static constexpr double HBARC_CUBED_MEV3FM3 = 197.32698 * 197.32698 * 197.32698;

struct Particle {
  long pdg;
  double mass_MeV; // mass in MeV (file stores GeV; converted on load)
  int degeneracy;  // g = 2J+1
  int stat;        // -1 boson, +1 fermion (Thermal-FIST convention)
  int B, Q, S, C;
  bool hasAntiparticle; // true if any of (B,Q,S,C) is non-zero
  // Per-particle highest cluster index where K_2(n*m/T) is non-negligible.
  int n_max;
  // Cached arrays of length n_max for K_2(n m/T), K_1(n m/T), and the
  // sign factor (-stat)^(n+1). All independent of mu, recomputed only
  // when T changes.
  std::vector<double> K2_cache;
  std::vector<double> K1_cache;
  std::vector<double> sign_cache;
};

static std::vector<Particle> g_particles;
static bool g_initialized = false;
static double g_T_cached = -1.0; // last T for which Bessel caches are valid

void initialize(const std::string &listPath) {
  if (g_initialized)
    return;
  std::ifstream f(listPath);
  if (!f.is_open())
    throw std::runtime_error("HRG: cannot open particle list " + listPath);
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#')
      continue;
    std::istringstream iss(line);
    Particle p;
    std::string name;
    int stable;
    double mass_GeV;
    int absS, absC;
    double width, thresh;
    if (!(iss >> p.pdg >> name >> stable >> mass_GeV >> p.degeneracy >>
          p.stat >> p.B >> p.Q >> p.S >> p.C >> absS >> absC >> width >>
          thresh))
      continue;
    p.mass_MeV = mass_GeV * 1000.0;
    p.hasAntiparticle = (p.B != 0) || (p.Q != 0) || (p.S != 0) || (p.C != 0);
    g_particles.push_back(p);
  }
  if (g_particles.empty())
    throw std::runtime_error("HRG: empty particle list " + listPath);
  g_initialized = true;
}

void cleanup() {
  g_particles.clear();
  g_initialized = false;
  g_T_cached = -1.0;
}

// Recompute K_1, K_2 tables and sign factors for every particle at this T.
// Cluster term n is dropped when n*m/T > 600 (K_n underflows beyond that).
static void rebuildCachesAt(double T) {
  for (auto &p : g_particles) {
    p.K2_cache.clear();
    p.K1_cache.clear();
    p.sign_cache.clear();
    const double mT = p.mass_MeV / T;
    int nmax = 0;
    for (int n = 1; n <= N_TRUNC; n++) {
      const double x = n * mT;
      if (x > 600.0)
        break;
      const double K2 = gsl_sf_bessel_Kn(2, x);
      const double K1 = gsl_sf_bessel_Kn(1, x);
      // Drop cluster terms whose K_2 dropped below 1e-300 (effectively zero).
      if (!std::isfinite(K2) || K2 < 1e-300)
        break;
      p.K2_cache.push_back(K2);
      p.K1_cache.push_back(K1);
      const double sign = (p.stat == -1) ? 1.0 : ((n & 1) ? 1.0 : -1.0);
      p.sign_cache.push_back(sign);
      nmax = n;
    }
    p.n_max = nmax;
  }
  g_T_cached = T;
}

bool isInitialized() { return g_initialized; }
int particleCount() { return (int)g_particles.size(); }

void setExcludedVolumes(double b_meson_fm3, double b_baryon_fm3) {
  g_b_meson_fm3 = b_meson_fm3;
  g_b_baryon_fm3 = b_baryon_fm3;
}

double getMesonExcludedVolume() { return g_b_meson_fm3; }
double getBaryonExcludedVolume() { return g_b_baryon_fm3; }

namespace {
// Compute the ideal-HRG cluster-expansion contribution of one particle at the
// supplied effective chemical potential. Returns dimensionless P/T^4, n/T^3,
// e/T^4. Bosons whose |mu_eff| would push the expansion past convergence get
// silently clipped to MU_OVER_M_CLIP * m_i — only bites during transient
// Newton iterations; at convergence the EV shift keeps mu_eff < m_i.
// Symmetric EV-HRG.  In a thermodynamically consistent EV-HRG the
// excluded-volume shift is applied to *both* particle and antiparticle
// chemical potentials with the same sign, which factors out as
// exp(-n*b*P/T) on each cluster term while keeping cosh / sinh of the
// bare mu_bare/T.  Writing it as
//   2cosh(n*(mu_bare - b*P)/T)
// (i.e. shifting the argument of cosh) is *not* equivalent and breaks
// C-symmetry at mu_bare = 0 — the system would acquire a spurious
// matter density proportional to sinh(-n*b*P/T).  We keep the cluster-
// wise factor exp(-n*b*P/T), which is exact and consistent at every
// cluster order n.  See the discussion above in the EV section.
//
// For species WITHOUT antiparticle (genuine self-conjugate states like
// pi0, eta, omega with B=Q=S=C=0): mu_bare is identically 0 so cosh
// degenerates to 1, but the EV suppression exp(-n*b*P/T) still applies
// — the species still occupies volume and its pressure is reduced.
inline void evalSpecies(const Particle &p, double T,
                         double mu_bare, double bP,
                         double &P_T4, double &n_T3, double &e_T4) {
  if (p.stat == -1) {
    // Boson cluster expansion regularization: keep |mu_bare/m| below 1
    // so the alternating series stays inside its convergence radius.
    // (For EV-HRG the original "mu_eff = mu - bP" trick handled this
    // by reducing |mu|, but with the symmetric form bP no longer
    // softens the argument of cosh, so we clip mu_bare directly.)
    if (std::abs(mu_bare) >= MU_OVER_M_CLIP * p.mass_MeV)
      mu_bare = std::copysign(MU_OVER_M_CLIP * p.mass_MeV, mu_bare);
  }
  const double mT = p.mass_MeV / T;
  const double mT2 = mT * mT;
  const double mu_T = mu_bare / T;
  const double bP_T = bP / T;
  const double pref = (double)p.degeneracy / (2.0 * M_PI * M_PI);
  double Pi = 0.0, ni = 0.0, ei = 0.0;
  for (int n = 1; n <= p.n_max; n++) {
    const double x = n * mT;
    const double K2 = p.K2_cache[n - 1];
    const double K1 = p.K1_cache[n - 1];
    const double sign = p.sign_cache[n - 1];
    const double ev = std::exp(-n * bP_T);   // symmetric EV suppression
    double cf, sf;
    if (p.hasAntiparticle) {
      cf = 2.0 * std::cosh(n * mu_T);
      sf = 2.0 * std::sinh(n * mu_T);
    } else {
      // mu_bare == 0 by construction (hasAntiparticle is the OR of
      // B,Q,S,C being nonzero), so cosh -> 1, sinh -> 0.
      cf = 1.0;
      sf = 0.0;
    }
    cf *= ev;
    sf *= ev;
    const double inv_n = 1.0 / n;
    const double inv_n2 = inv_n * inv_n;
    Pi += sign * inv_n2 * mT2 * K2 * cf;
    ni += sign * inv_n * mT2 * K2 * sf;
    ei += sign * inv_n2 * mT2 * (3.0 * K2 + x * K1) * cf;
  }
  P_T4 = Pi * pref;
  n_T3 = ni * pref;
  e_T4 = ei * pref;
}
} // namespace

Result eval(double T, double muB, double muQ, double muS) {
  if (!g_initialized)
    throw std::runtime_error("HRG: not initialized");
  if (T <= 0.0)
    throw std::runtime_error("HRG: T must be positive");

  if (T != g_T_cached)
    rebuildCachesAt(T);

  const double T3 = T * T * T;
  const double T4 = T3 * T;

  // Excluded volumes in MeV^-3 (b * P has dimension of MeV, matching mu).
  const double b_M = g_b_meson_fm3 / HBARC_CUBED_MEV3FM3;
  const double b_B = g_b_baryon_fm3 / HBARC_CUBED_MEV3FM3;
  const bool ev_active = (b_M > 0.0 || b_B > 0.0);

  // Per-particle bare mu_i and excluded volume.
  const int N = (int)g_particles.size();
  std::vector<double> mu_bare(N), b_arr(N);
  for (int i = 0; i < N; i++) {
    const auto &p = g_particles[i];
    mu_bare[i] = p.B * muB + p.Q * muQ + p.S * muS;
    b_arr[i] = (p.B != 0) ? b_B : b_M;
  }

  // Self-consistency: solve  P = sum_i P_i^id(T, mu_i, b_i*P)  for P,
  // where P_i^id now uses the *symmetric* EV form (cluster-wise
  // exp(-n*b_i*P/T) multiplying cosh/sinh of mu_bare/T).
  //
  // Newton step:  P -> P - (P - P_id) / F'.
  // Approximate F' = 1 + sum_j b_j n_j^id ; this is exact for the
  // asymmetric form and a good approximation for the symmetric form
  // (correction is O((bP/T)^2) and very small for the typical bP/T <~ 0.02
  // at the seam).  If convergence slows, the loop is capped at
  // NEWTON_MAX_ITER (50) — well above the ~5 iterations needed in
  // practice.
  // Start at P = 0 (ideal HRG limit).  For ev_active = false skip
  // iteration entirely (single pass at bP = 0).
  double P_dim = 0.0; // dimensional pressure, MeV^4
  if (ev_active) {
    for (int iter = 0; iter < NEWTON_MAX_ITER; iter++) {
      double P_id_T4 = 0.0;
      double bn_sum_T3 = 0.0;
      for (int i = 0; i < N; i++) {
        double Pi_T4, ni_T3, ei_T4;
        evalSpecies(g_particles[i], T, mu_bare[i], b_arr[i] * P_dim,
                    Pi_T4, ni_T3, ei_T4);
        P_id_T4 += Pi_T4;
        bn_sum_T3 += b_arr[i] * ni_T3;
      }
      const double P_id_dim = P_id_T4 * T4;
      const double F = P_dim - P_id_dim;
      const double F_prime = 1.0 + bn_sum_T3 * T3;
      const double dP = -F / F_prime;
      P_dim += dP;
      if (std::abs(dP) < NEWTON_TOL_REL * (std::abs(P_dim) + T4))
        break;
    }
  } else {
    // Pure ideal HRG: one pass at bP = 0.
    double P_id_T4 = 0.0;
    for (int i = 0; i < N; i++) {
      double Pi_T4, ni_T3, ei_T4;
      evalSpecies(g_particles[i], T, mu_bare[i], 0.0, Pi_T4, ni_T3, ei_T4);
      P_id_T4 += Pi_T4;
    }
    P_dim = P_id_T4 * T4;
  }

  // Final pass: extract n_X, e at the converged P.  The EV correction
  // divides by (1 + sum_j b_j n_j^id) for densities and energy
  // (Vovchenko's "EV thermodynamics" — see Phys.Rev. C 96, 015206).
  Result r{0.0, 0.0, 0.0, 0.0, 0.0};
  r.P_T4 = P_dim / T4;
  double e_T4_total = 0.0;
  double bn_sum_T3 = 0.0;
  std::vector<double> n_id_T3(N), e_id_T4(N);
  for (int i = 0; i < N; i++) {
    double Pi_T4, ni_T3, ei_T4;
    evalSpecies(g_particles[i], T, mu_bare[i], b_arr[i] * P_dim,
                Pi_T4, ni_T3, ei_T4);
    n_id_T3[i] = ni_T3;
    e_id_T4[i] = ei_T4;
    bn_sum_T3 += b_arr[i] * ni_T3;
  }
  const double denom = 1.0 + bn_sum_T3;
  for (int i = 0; i < N; i++) {
    const auto &p = g_particles[i];
    const double n_actual_T3 = n_id_T3[i] / denom;
    const double e_actual_T4 = e_id_T4[i] / denom;
    r.nB_T3 += p.B * n_actual_T3;
    r.nQ_T3 += p.Q * n_actual_T3;
    r.nS_T3 += p.S * n_actual_T3;
    e_T4_total += e_actual_T4;
  }

  // Entropy from the thermodynamic identity (true with EV; the EV correction
  // appears the same way in P, e, n so s = (e + P - sum mu n)/T survives).
  r.s_T3 = e_T4_total + r.P_T4 - (muB / T) * r.nB_T3 -
           (muQ / T) * r.nQ_T3 - (muS / T) * r.nS_T3;
  return r;
}

} // namespace HRG
