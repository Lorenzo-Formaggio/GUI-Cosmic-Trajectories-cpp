#ifndef CONTOUREOSCORE_HPP
#define CONTOUREOSCORE_HPP

#include <limits>
#include <vector>

/**
 * @file ContourEoSCore.hpp
 * @brief The constant-entropy-density contour equation of state, shared by both
 *        Entropy Contour models.
 *
 * This is a port of `scontours::EquationOfState` (s_contours_c-dev4-tristan,
 * `include/scontours/EquationOfState.h`, `src/EquationOfState.cpp`). Along a
 * fixed direction n_hat = (n_B, n_Q, n_S) in chemical potential space, with
 * radial coordinate mu = |(mu_B, mu_Q, mu_S)| [arXiv:2601.08823 Sec. IV,
 * Eqs. (30), (37)]:
 *
 *   T_s(T0, mu) = T0 + mu^2/2 alpha2(T0)          (contour map)
 *   alpha2(T0)  = -d/dT0[T0^2 X2] / d/dT0[s0]     (Eq. 15)
 *   P(T, mu)    = P_ref(mu) + [ p0(T0) + mu^2/2 ( s0 alpha2 + T0^2 X2 ) ]
 *                                              |_{T0low(mu)}^{T0(T, mu)}
 *   s(T, mu)    = s0(T0)                          (exact, by construction)
 *   n_X(T, mu)  = n_X,ref + [ 1/2 T0^2 W_X ]_{T0low(mu)}^{T0(T, mu)},
 *                 W_X = 2 sum_j mu_j chi_Xj(T0)
 *
 * where X2 is the generalized susceptibility along n_hat, p0(T) = int_0^T s0 dT'
 * (Gauss-Legendre), and P_ref / n_X,ref are the QvdW-HRG pressure and charge
 * densities at (Tlow = 80 MeV, mu n_hat) from `HRG.hpp`.
 *
 * The ONLY thing that differs between the two Entropy Contour models is the
 * mu = 0 lattice input, which they supply through LatticeSource:
 *
 *   EntrCont      (eos = 3) -- cubic splines of the tabulated Wuppertal-Budapest
 *                              susceptibilities in `EntroContourEoS/chis/`;
 *                              follows the data to 800 MeV
 *   EntrContParam (eos = 4) -- the closed-form Wuppertal-Budapest fits of the
 *                              reference, with symbolically generated exact
 *                              T-derivatives; the fits are good to about
 *                              200 MeV
 *
 * Keeping the contour algebra in one place is the point of this header: the two
 * models are meant to differ in their lattice input and nothing else, and when
 * each carried its own copy of the algebra the copies drifted apart (and away
 * from the reference).
 *
 * All temperatures and chemical potentials in MeV; pressure in MeV^4, densities
 * and entropy density in MeV^3.
 */
namespace ContourEoS {

/** Unit direction in (mu_B, mu_Q, mu_S). All zero at mu = 0. */
struct Dir {
  double nB = 0.0, nQ = 0.0, nS = 0.0;
};

/** Susceptibility pair index for (i, j), i, j in {0:B, 1:Q, 2:S}:
 *  0 BB, 1 QQ, 2 SS, 3 BQ, 4 BS, 5 QS. */
int pairIndex(int i, int j);

/** Every mu = 0 lattice quantity the contour needs at one temperature, with
 *  T-derivatives up to second order (alpha2' needs X2'' and s0''). */
struct LatticeAt {
  double chi[6][3]; /**< [pair][T-derivative order], dimensionless */
  double s0[3];     /**< dimensional s0, s0', s0'' in MeV^3, MeV^2, MeV */
};

/**
 * @brief The mu = 0 lattice input -- the only difference between the two models.
 */
class LatticeSource {
public:
  virtual ~LatticeSource() = default;

  /** All six susceptibilities and the entropy density with their first two
   *  T-derivatives. Implementations should evaluate them in one pass: the
   *  contour solver calls this in its inner loop, and where cross
   *  susceptibilities are derived from others, evaluating element by element
   *  would redo the same work several times over. */
  virtual void at(double T, LatticeAt &out) const = 0;

  /** Dimensional entropy density s0(T) [MeV^3] alone. The p0 quadrature calls
   *  this 64 times per evaluation, so it must not drag the susceptibilities
   *  along. */
  virtual double entropyDensity(double T) const = 0;
};

/**
 * @brief Thermodynamics at one resolved contour anchor T0 (dimensional: MeV^4
 *        for P, MeV^3 for the densities and the entropy density).
 *
 * `jac` is the contour slope dT_s/dT0 = 1 + mu^2/2 alpha2'(T0): positive on the
 * mechanically stable branches, negative on the unstable middle branch, and
 * zero at the spinodals / the critical point.
 */
struct AnchorResult {
  double T0 = 0.0;
  double jac = 0.0;
  double P = 0.0;
  double nB = 0.0, nQ = 0.0, nS = 0.0;
  double s = 0.0;
  bool valid = false;
};

class Engine;

/**
 * @brief Per-direction contour state.
 *
 * Holds the contour sampled on the T0 anchor grid for one fixed
 * (mu_B, mu_Q, mu_S) direction and magnitude, plus the low-temperature boundary
 * constants. Reuse it to query several thermodynamic functions at several
 * temperatures without redoing the contour (and, more importantly, without
 * redoing the QvdW-HRG solve, which dominates the cost).
 *
 * The grid arrays exist so that the T -> T0 inversion can find *all* branches
 * (past the critical point the contour map folds and one T has several
 * anchors). The returned thermodynamics is not read off the grid: once a branch
 * is bracketed, T0 is refined by a safeguarded Newton solve and every quantity
 * is evaluated in closed form there -- so the grid spacing costs no accuracy.
 */
struct Contour {
  const Engine *engine = nullptr;

  double muB = 0.0, muQ = 0.0, muS = 0.0;

  /* radial coordinate and unit direction */
  double mu = 0.0;
  double nBhat = 0.0, nQhat = 0.0, nShat = 0.0;

  /* low-T boundary: anchor of Tlow on this contour, and the QvdW-HRG values */
  double T0low = 0.0;
  double Glow = 0.0;                /**< contour pressure term at T0low, MeV^4 */
  double qlow[3] = {0.0, 0.0, 0.0}; /**< contour charge terms at T0low, MeV^3 */
  double Pref = 0.0;                /**< P(Tlow, mu n_hat), MeV^4 */
  double nref[3] = {0.0, 0.0, 0.0}; /**< n_X(Tlow, mu n_hat), MeV^3 */
  bool anchored = false; /**< false -> the boundary could not be evaluated */

  /* Per-T0 grid arrays (size = Engine::options().gridPoints). */
  std::vector<double> T0g;    ///< anchor temperature grid
  std::vector<double> T_phys; ///< physical T(T0) along the contour
  std::vector<double> dTdT0;  ///< contour slope 1 + mu^2/2 alpha2'(T0)
  std::vector<double> nB_T3;  ///< n_B / T^3
  std::vector<double> nS_T3;  ///< n_S / T^3
  std::vector<double> nQ_T3;  ///< n_Q / T^3
  std::vector<double> s_T3;   ///< s   / T^3
  std::vector<double> P_T4;   ///< P   / T^4

  /* Memo of the last T -> T0 inversion. Callers ask for n_B, n_Q, n_S, s and P
   * at the same (mu, T) -- the trajectory solver does so once per residual and
   * once per Jacobian column -- and the inversion dominates the cost of a
   * lookup, so the first of those calls pays for all of them. Mutable: it is a
   * cache, not state, and the accessors take the contour by const reference. */
  mutable double cachedT = std::numeric_limits<double>::quiet_NaN();
  mutable AnchorResult cached;
};

/**
 * @brief The contour engine: anchor grid, quadrature, and the
 *        direction-independent lattice tables, built once from a LatticeSource.
 *
 * Holds a reference to the LatticeSource, which must outlive it.
 */
class Engine {
public:
  struct Options {
    double Tlow = 80.0; ///< MeV, low-T boundary (the paper's 80)
    int quadNodes = 64; ///< Gauss-Legendre nodes for p0(T)
    int gridPoints = 1000;
    /** Anchor grid range. Every anchor the EoS can reach satisfies
     *  T0 >= T_phys >= Tlow (alpha2 < 0 => T_phys < T0), so the lower end only
     *  needs to sit safely below Tlow -- and safely inside the range where the
     *  lattice input is well behaved. */
    double T0Min = 60.0, T0Max = 2030.0;
    double tol = 1e-9; ///< MeV, contour inversion tolerance
    int maxIter = 100;
  };

  /* Two overloads rather than a defaulted argument: `Options()` cannot be used
   * as a default argument for a nested type whose enclosing class is still
   * incomplete at that point. */
  explicit Engine(const LatticeSource &source);
  Engine(const LatticeSource &source, const Options &opts);

  const Options &options() const { return opts_; }

  /** p0(T) = int_0^T s0 dT' [MeV^4] by Gauss-Legendre. Only ever used as the
   *  difference p0(T0) - p0(T0low), so the convention below the lattice input's
   *  range cancels exactly. */
  double pressureMu0(double T) const;

  /** Contour map T_s(T0, mu) and its Jacobian dT_s/dT0, from one lattice
   *  evaluation. The inner loop of the inversion: no quadrature, no charge
   *  terms. */
  void contourMap(const Dir &d, double mu, double T0, double &Tphys,
                  double &slope) const;

  /** Contour anchor T0 solving T_s(T0, mu) = T on the physical branch, with no
   *  grid to bracket from (used for T0low). */
  double anchorT0(const Dir &d, double T, double mu) const;

  /** Build the contour at a chemical potential point. Performs exactly one
   *  QvdW-HRG solve (at the seam) when @p useHRG; otherwise the boundary is the
   *  bare-contour convention P(Tlow, mu) = p0(Tlow) with zero boundary
   *  densities, which is `scontours::EquationOfState`'s default reference. */
  Contour build(double muB, double muQ, double muS, bool useHRG) const;

  /** Resolve T -> T0 and evaluate the thermodynamics there. Returns false
   *  outside the domain or where no branch is bracketed. */
  bool invert(const Contour &c, double T, AnchorResult &out) const;

private:
  struct Anchor {
    double T0, Tphys, slope, s0, G, q[3];
  };
  Anchor anchorState(const Dir &d, double mu, double T0) const;
  AnchorResult evaluateAtAnchor(const Contour &c, double T0) const;
  double solveAnchor(const Dir &d, double mu, double Ttarget, double lo,
                     double hi, double fLo, double guess) const;

  const LatticeSource *src_;
  Options opts_;
  std::vector<double> glNodes_, glWeights_;
  std::vector<double> T0g_, s0_, s0p_, s0pp_, p0_;
  std::vector<double> chi_[6][3];
};

} // namespace ContourEoS

#endif // CONTOUREOSCORE_HPP
