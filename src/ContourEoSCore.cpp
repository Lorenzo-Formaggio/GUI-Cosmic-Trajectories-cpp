#include "ContourEoSCore.hpp"

#include "HRG.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

namespace ContourEoS {

namespace {

const double NaN = std::numeric_limits<double>::quiet_NaN();

/* Gauss-Legendre nodes/weights on [-1, 1] (Numerical Recipes' gauleg). The
 * reference integrates p0(T) = int_0^T s0 dT' spectrally rather than by a
 * trapezoid over the anchor grid; s0 is smooth, so 64 nodes are exact to
 * round-off. */
void gaussLegendre(int n, std::vector<double> &x, std::vector<double> &w) {
  const double eps = 3.0e-15;
  x.assign(n, 0.0);
  w.assign(n, 0.0);
  const int m = (n + 1) / 2;
  for (int i = 0; i < m; ++i) {
    double z = std::cos(M_PI * (i + 0.75) / (n + 0.5));
    double pp = 0.0, z1 = 0.0;
    do {
      double p1 = 1.0, p2 = 0.0;
      for (int j = 0; j < n; ++j) {
        const double p3 = p2;
        p2 = p1;
        p1 = ((2.0 * j + 1.0) * z * p2 - j * p3) / (j + 1);
      }
      pp = n * (z * p1 - p2) / (z * z - 1.0);
      z1 = z;
      z = z1 - p1 / pp;
    } while (std::fabs(z - z1) > eps);
    x[i] = -z;
    x[n - 1 - i] = z;
    w[i] = 2.0 / ((1.0 - z * z) * pp * pp);
    w[n - 1 - i] = w[i];
  }
}

/* Angular weights of the generalized susceptibility [Eq. (12)], in pair order
 * (BB, QQ, SS, BQ, BS, QS). */
void angularWeights(const Dir &d, double w[6]) {
  w[0] = d.nB * d.nB;
  w[1] = d.nQ * d.nQ;
  w[2] = d.nS * d.nS;
  w[3] = 2.0 * d.nB * d.nQ;
  w[4] = 2.0 * d.nB * d.nS;
  w[5] = 2.0 * d.nQ * d.nS;
}

Dir directionOf(const Contour &c) {
  Dir d;
  d.nB = c.nBhat;
  d.nQ = c.nQhat;
  d.nS = c.nShat;
  return d;
}

} // namespace

int pairIndex(int i, int j) {
  const int a = i < j ? i : j;
  const int b = i < j ? j : i;
  if (a == 0 && b == 0) return 0;
  if (a == 1 && b == 1) return 1;
  if (a == 2 && b == 2) return 2;
  if (a == 0 && b == 1) return 3;
  if (a == 0 && b == 2) return 4;
  return 5; /* (1,2) */
}

// ============================================================================
// Engine construction: the anchor grid and the direction-independent lattice
// tables on it. Only the angular combination depends on the chemical-potential
// direction, so these are built once and every contour is then O(gridPoints) of
// cheap arithmetic.
// ============================================================================
Engine::Engine(const LatticeSource &source)
    : Engine(source, Options()) {}

Engine::Engine(const LatticeSource &source, const Options &opts)
    : src_(&source), opts_(opts) {
  if (opts_.gridPoints < 2 || opts_.quadNodes < 1 || opts_.maxIter < 1 ||
      !(opts_.T0Max > opts_.T0Min) || !(opts_.Tlow > 0.0))
    throw std::invalid_argument("ContourEoS::Engine: invalid options");

  gaussLegendre(opts_.quadNodes, glNodes_, glWeights_);

  const int n = opts_.gridPoints;
  T0g_.resize(n);
  const double dT0 = (opts_.T0Max - opts_.T0Min) / (n - 1);
  for (int i = 0; i < n; ++i)
    T0g_[i] = opts_.T0Min + dT0 * i;

  s0_.resize(n);
  s0p_.resize(n);
  s0pp_.resize(n);
  p0_.resize(n);
  for (int p = 0; p < 6; ++p)
    for (int o = 0; o < 3; ++o)
      chi_[p][o].resize(n);

  for (int i = 0; i < n; ++i) {
    const double T0 = T0g_[i];
    LatticeAt L;
    src_->at(T0, L);
    s0_[i] = L.s0[0];
    s0p_[i] = L.s0[1];
    s0pp_[i] = L.s0[2];
    p0_[i] = pressureMu0(T0);
    for (int p = 0; p < 6; ++p)
      for (int o = 0; o < 3; ++o)
        chi_[p][o][i] = L.chi[p][o];
  }
}

double Engine::pressureMu0(double T) const {
  if (T <= 0.0)
    return 0.0;
  const double half = 0.5 * T;
  double sum = 0.0;
  for (std::size_t k = 0; k < glNodes_.size(); ++k)
    sum += glWeights_[k] * src_->entropyDensity(half * (glNodes_[k] + 1.0));
  return half * sum;
}

// ============================================================================
// Contour algebra.
// ============================================================================
void Engine::contourMap(const Dir &d, double mu, double T0, double &Tphys,
                        double &slope) const {
  LatticeAt L;
  src_->at(T0, L);
  double w[6];
  angularWeights(d, w);
  double X[3] = {0.0, 0.0, 0.0};
  for (int o = 0; o < 3; ++o)
    for (int p = 0; p < 6; ++p)
      X[o] += w[p] * L.chi[p][o];
  /* alpha2  = -d(T0^2 X2)/dT0 / s0'(T0)                             [Eq. 15]
   * alpha2' = (N' - alpha2 s0'') / s0'   (quotient Leibniz rule) */
  const double N0 = -(2.0 * T0 * X[0] + T0 * T0 * X[1]);
  const double N1 = -(2.0 * X[0] + 4.0 * T0 * X[1] + T0 * T0 * X[2]);
  const double a2 = N0 / L.s0[1];
  const double a2p = (N1 - a2 * L.s0[2]) / L.s0[1];
  const double half = 0.5 * mu * mu;
  Tphys = T0 + half * a2;
  slope = 1.0 + half * a2p;
}

/* Everything the EoS needs at one anchor T0, from a single lattice evaluation:
 *   Tphys = T_s(T0, mu),  slope = dT_s/dT0,
 *   s0    = s0(T0)                                (entropy is constant along a
 *                                                  constant-s contour)
 *   G     = p0(T0) + mu^2/2 (s0 alpha2 + T0^2 X2) (contour pressure term)
 *   q_X   = 1/2 T0^2 W_X, W_X = 2 sum_j mu_j chi_Xj(T0)  (contour charge term)
 * The EoS is then P = P_ref + G(T0) - G(T0low) and
 * n_X = n_X,ref + q_X(T0) - q_X(T0low). */
Engine::Anchor Engine::anchorState(const Dir &d, double mu, double T0) const {
  LatticeAt L;
  src_->at(T0, L);
  double w[6];
  angularWeights(d, w);
  double X[3] = {0.0, 0.0, 0.0};
  for (int o = 0; o < 3; ++o)
    for (int p = 0; p < 6; ++p)
      X[o] += w[p] * L.chi[p][o];
  const double N0 = -(2.0 * T0 * X[0] + T0 * T0 * X[1]);
  const double N1 = -(2.0 * X[0] + 4.0 * T0 * X[1] + T0 * T0 * X[2]);
  const double a2 = N0 / L.s0[1];
  const double a2p = (N1 - a2 * L.s0[2]) / L.s0[1];
  const double half = 0.5 * mu * mu;

  Anchor A{};
  A.T0 = T0;
  A.Tphys = T0 + half * a2;
  A.slope = 1.0 + half * a2p;
  A.s0 = L.s0[0];
  A.G = pressureMu0(T0) + half * (L.s0[0] * a2 + T0 * T0 * X[0]);
  const double n[3] = {d.nB, d.nQ, d.nS};
  for (int Xc = 0; Xc < 3; ++Xc) {
    double s = 0.0;
    for (int j = 0; j < 3; ++j)
      s += n[j] * L.chi[pairIndex(Xc, j)][0];
    A.q[Xc] = 0.5 * T0 * T0 * (2.0 * mu * s);
  }
  return A;
}

/* Safeguarded Newton-bisection (Numerical Recipes' rtsafe) on
 * f(T0) = T_s(T0, mu) - T over a bracket, evaluating f and f' together and
 * starting from a supplied guess. Falls back to bisection whenever a Newton
 * step would leave the bracket, so it converges even where f' -> 0 (the
 * spinodal fold / critical point), exactly like the reference solver.
 *
 * fLo is f at `lo`, already known to the caller from the grid bracket or the
 * geometric expansion -- one lattice evaluation saved per call. */
double Engine::solveAnchor(const Dir &d, double mu, double Ttarget, double lo,
                           double hi, double fLo, double guess) const {
  if (fLo == 0.0)
    return lo;
  double xl, xh;
  if (fLo < 0.0) {
    xl = lo;
    xh = hi;
  } else {
    xl = hi;
    xh = lo;
  }
  const double a = std::min(lo, hi), b = std::max(lo, hi);
  double rts = (guess > a && guess < b) ? guess : 0.5 * (lo + hi);
  double dxold = std::fabs(hi - lo);
  double dx = dxold;
  double Tv, df;
  contourMap(d, mu, rts, Tv, df);
  double fv = Tv - Ttarget;
  for (int it = 0; it < opts_.maxIter; ++it) {
    if ((((rts - xh) * df - fv) * ((rts - xl) * df - fv) > 0.0) ||
        (std::fabs(2.0 * fv) > std::fabs(dxold * df))) {
      dxold = dx; /* bisection step */
      dx = 0.5 * (xh - xl);
      rts = xl + dx;
      if (xl == rts)
        return rts;
    } else {
      dxold = dx; /* Newton step */
      dx = fv / df;
      const double temp = rts;
      rts -= dx;
      if (temp == rts)
        return rts;
    }
    if (std::fabs(dx) < opts_.tol)
      return rts;
    contourMap(d, mu, rts, Tv, df);
    fv = Tv - Ttarget;
    if (fv < 0.0)
      xl = rts;
    else
      xh = rts;
  }
  return rts;
}

double Engine::anchorT0(const Dir &d, double T, double mu) const {
  if (mu <= 0.0)
    return T;
  auto fAt = [&](double T0) {
    double Tv, sl;
    contourMap(d, mu, T0, Tv, sl);
    return Tv - T;
  };

  /* alpha2 < 0, so the physical anchor sits above T: expand the offset
   * geometrically, keeping `a` as the last point with f <= 0. Same bracketing
   * as scontours::EquationOfState::anchorT0. */
  const double maxOffset = 2000.0;
  double a = T, fa = fAt(a);
  if (fa == 0.0)
    return a;

  double b = a, fb = fa;
  if (fa < 0.0) {
    double off = 1.0;
    while (fb <= 0.0 && off <= maxOffset) {
      a = b;
      fa = fb;
      b = T + off;
      fb = fAt(b);
      off *= 2.0;
    }
  } else { /* unexpected (alpha2 >= 0): the root lies below T */
    double off = 1.0;
    while (fa >= 0.0 && off <= maxOffset) {
      b = a;
      fb = fa;
      a = T - off;
      fa = fAt(a);
      off *= 2.0;
    }
  }
  if (fa * fb > 0.0)
    return b; /* no sign change (degenerate); best effort */
  const double guess = (fb != fa) ? a - fa * (b - a) / (fb - fa) : 0.5 * (a + b);
  return solveAnchor(d, mu, T, a, b, fa, guess);
}

// ============================================================================
// Contour construction.
// ============================================================================
Contour Engine::build(double muB, double muQ, double muS, bool useHRG) const {
  Contour c;
  c.engine = this;
  c.muB = muB;
  c.muQ = muQ;
  c.muS = muS;

  const double mu = std::sqrt(muB * muB + muQ * muQ + muS * muS);
  c.mu = mu;
  Dir dir;
  if (mu > 0.0) {
    dir.nB = muB / mu;
    dir.nQ = muQ / mu;
    dir.nS = muS / mu;
  }
  c.nBhat = dir.nB;
  c.nQhat = dir.nQ;
  c.nShat = dir.nS;

  const int n = opts_.gridPoints;
  c.T0g = T0g_;
  c.T_phys.assign(n, 0.0);
  c.dTdT0.assign(n, 0.0);
  c.nB_T3.assign(n, 0.0);
  c.nS_T3.assign(n, 0.0);
  c.nQ_T3.assign(n, 0.0);
  c.s_T3.assign(n, 0.0);
  c.P_T4.assign(n, 0.0);

  const double half = 0.5 * mu * mu;
  const double nhat[3] = {dir.nB, dir.nQ, dir.nS};
  double w[6];
  angularWeights(dir, w);

  /* Contour map and the raw contour terms on the anchor grid, from the
   * precomputed direction-independent tables. */
  std::vector<double> G(n);
  std::vector<double> q[3] = {std::vector<double>(n), std::vector<double>(n),
                              std::vector<double>(n)};
  for (int i = 0; i < n; ++i) {
    const double T0 = T0g_[i];

    double X[3] = {0.0, 0.0, 0.0};
    for (int o = 0; o < 3; ++o)
      for (int p = 0; p < 6; ++p)
        X[o] += w[p] * chi_[p][o][i];

    const double N0 = -(2.0 * T0 * X[0] + T0 * T0 * X[1]);
    const double N1 = -(2.0 * X[0] + 4.0 * T0 * X[1] + T0 * T0 * X[2]);
    const double a2 = N0 / s0p_[i];
    const double a2p = (N1 - a2 * s0pp_[i]) / s0p_[i];

    c.T_phys[i] = T0 + half * a2;
    c.dTdT0[i] = 1.0 + half * a2p;
    G[i] = p0_[i] + half * (s0_[i] * a2 + T0 * T0 * X[0]);

    for (int Xc = 0; Xc < 3; ++Xc) {
      double s = 0.0;
      for (int j = 0; j < 3; ++j)
        s += nhat[j] * chi_[pairIndex(Xc, j)][0][i];
      q[Xc][i] = 0.5 * T0 * T0 * (2.0 * mu * s);
    }
  }

  /* Low-temperature boundary: the anchor of Tlow on this contour, the contour
   * terms there, and the QvdW-HRG values (exactly one model solve). */
  c.T0low = anchorT0(dir, opts_.Tlow, mu);
  const Anchor low = anchorState(dir, mu, c.T0low);
  c.Glow = low.G;
  for (int X = 0; X < 3; ++X)
    c.qlow[X] = low.q[X];

  c.anchored = true;
  if (useHRG) {
    const double T3 = opts_.Tlow * opts_.Tlow * opts_.Tlow;
    const double T4 = T3 * opts_.Tlow;
    try {
      const HRG::Result r =
          HRG::eval(opts_.Tlow, mu * dir.nB, mu * dir.nQ, mu * dir.nS);
      if (std::isfinite(r.P_T4) && std::isfinite(r.nB_T3) &&
          std::isfinite(r.nQ_T3) && std::isfinite(r.nS_T3)) {
        c.Pref = r.P_T4 * T4;
        c.nref[0] = r.nB_T3 * T3;
        c.nref[1] = r.nQ_T3 * T3;
        c.nref[2] = r.nS_T3 * T3;
      } else {
        c.anchored = false;
      }
    } catch (const std::exception &) {
      c.anchored = false;
    }
  } else {
    /* bare-contour boundary: P(Tlow, mu) = p0(Tlow), zero boundary densities
     * (scontours::EquationOfState's default reference). */
    c.Pref = pressureMu0(opts_.Tlow);
  }

  /* Assemble the grid arrays. These are for branch detection and plotting; the
   * accessors re-evaluate in closed form at the refined anchor. */
  for (int i = 0; i < n; ++i) {
    const double Tp = c.T_phys[i];
    if (!c.anchored || !(Tp > 0.0)) {
      c.P_T4[i] = c.nB_T3[i] = c.nQ_T3[i] = c.nS_T3[i] = c.s_T3[i] = NaN;
      continue;
    }
    const double Tp3 = Tp * Tp * Tp;
    const double Tp4 = Tp3 * Tp;
    c.P_T4[i] = (c.Pref + G[i] - c.Glow) / Tp4;
    c.nB_T3[i] = (c.nref[0] + q[0][i] - c.qlow[0]) / Tp3;
    c.nQ_T3[i] = (c.nref[1] + q[1][i] - c.qlow[1]) / Tp3;
    c.nS_T3[i] = (c.nref[2] + q[2][i] - c.qlow[2]) / Tp3;
    c.s_T3[i] = s0_[i] / Tp3;
  }

  return c;
}

// ============================================================================
// T_phys -> T0 inversion.
//
// The contour map folds past the critical point, so one physical T can have
// several anchors. Brackets are found on the grid; each is then refined by the
// same safeguarded Newton solve the reference uses (warm-started from the grid,
// which puts it within ~1e-3 MeV, so it converges in one or two steps), and the
// thermodynamics is evaluated in closed form at the refined anchor -- the grid
// spacing never enters the returned numbers.
// ============================================================================
AnchorResult Engine::evaluateAtAnchor(const Contour &c, double T0) const {
  const Dir d = directionOf(c);
  const Anchor A = anchorState(d, c.mu, T0);
  AnchorResult r;
  r.T0 = A.T0;
  r.jac = A.slope;
  r.P = c.Pref + A.G - c.Glow;
  r.nB = c.nref[0] + A.q[0] - c.qlow[0];
  r.nQ = c.nref[1] + A.q[1] - c.qlow[1];
  r.nS = c.nref[2] + A.q[2] - c.qlow[2];
  r.s = A.s0; /* entropy is constant along a constant-s contour */
  r.valid = true;
  return r;
}

bool Engine::invert(const Contour &c, double T_target, AnchorResult &out) const {
  if (c.cachedT == T_target && c.cached.valid) { /* memoized last inversion */
    out = c.cached;
    return true;
  }
  if (!c.anchored || c.T_phys.empty())
    return false;

  const Dir d = directionOf(c);
  std::vector<AnchorResult> branches;
  const int n = static_cast<int>(c.T_phys.size());
  for (int k = 0; k < n - 1; ++k) {
    const double d0 = c.T_phys[k] - T_target;
    const double d1 = c.T_phys[k + 1] - T_target;
    /* Non-finite ends are not a sign change. This has to be tested explicitly:
     * every comparison against NaN is false, so `d0 * d1 >= 0.0` would fall
     * through and hand the solver a NaN bracket, which then burns its full
     * iteration budget producing a NaN branch. */
    if (!std::isfinite(d0) || !std::isfinite(d1))
      continue;
    if (d0 == 0.0) { /* exact hit on a grid anchor */
      const AnchorResult b = evaluateAtAnchor(c, c.T0g[k]);
      if (std::isfinite(b.P) && std::isfinite(b.jac))
        branches.push_back(b);
      continue;
    }
    if (d0 * d1 >= 0.0) /* d1 == 0 is picked up as d0 of the next bracket */
      continue;
    /* linear warm start inside the bracket, then the exact Newton solve */
    const double guess = c.T0g[k] + (d0 / (d0 - d1)) * (c.T0g[k + 1] - c.T0g[k]);
    const double T0 =
        solveAnchor(d, c.mu, T_target, c.T0g[k], c.T0g[k + 1], d0, guess);
    const AnchorResult b = evaluateAtAnchor(c, T0);
    if (std::isfinite(b.P) && std::isfinite(b.jac))
      branches.push_back(b);
  }
  if (branches.empty())
    return false;

  /* Several branches: take a mechanically stable one (positive contour slope)
   * and, among those, the highest pressure -- the Gibbs-stable phase. Falling
   * back to the highest pressure regardless of stability keeps a usable answer
   * inside the spinodal region, where no branch of this contour is stable. */
  const AnchorResult *best = nullptr;
  for (const auto &b : branches)
    if (b.jac > 0.0 && (!best || b.P > best->P))
      best = &b;
  if (!best)
    for (const auto &b : branches)
      if (!best || b.P > best->P)
        best = &b;
  if (!best)
    return false;
  out = *best;
  c.cachedT = T_target;
  c.cached = out;
  return true;
}

} // namespace ContourEoS
