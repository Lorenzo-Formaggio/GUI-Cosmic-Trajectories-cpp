#include "EntrCont.hpp"

#include "HRG.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace EntropyContours {

using Vec = std::vector<double>;
static const double NaN = std::numeric_limits<double>::quiet_NaN();

// ============================================================================
// Lattice input at mu = 0: cubic splines through the tabulated Wuppertal-
// Budapest susceptibilities and entropy density.
//
// Everything downstream of this input -- the contour algebra, the anchor solve,
// the QvdW-HRG seam -- lives in ContourEoSCore.hpp, shared with EntrContParam.
// ============================================================================
namespace {

/* Gaussian smoothing width, in samples of the 1 MeV lattice grid.
 *
 * alpha2' needs the SECOND T-derivative of the susceptibilities, and a cubic
 * spline through data has a piecewise-linear second derivative that tracks
 * whatever scatter the data carries. The previous implementation smoothed with
 * sigma = 2 while only needing the first derivative; with the second derivative
 * in play the width has to be chosen against the resulting jaggedness of the
 * contour slope dT/dT0. Measured at mu_B = 600 MeV (largest kink in dT/dT0
 * between adjacent grid points, and the induced shift in the thermodynamics at
 * T = 150 MeV):
 *
 *   sigma    largest kink    s/T^3(150)   P/T^4(150)
 *     1        5.8e-01         6.5086       0.86760
 *     2        1.3e-01         6.5046       0.86812
 *     4        2.4e-02         6.4919       0.87024
 *     8        1.5e-02         6.4559       0.87917
 *    16        2.9e-03         6.3928       0.91542
 *
 * sigma = 4 (i.e. 4 MeV, narrow against the ~40 MeV crossover width) buys a
 * 5x smoother second derivative for a 0.2% shift in the physics -- far inside
 * the lattice error bars. Beyond that the smoothing starts distorting the
 * crossover itself. */
constexpr double SMOOTH_SIGMA = 4.0;

/**
 * @brief Natural cubic spline with values and first two derivatives.
 *
 * Outside the knot range it continues linearly from the end knot (value and
 * first derivative continuous, second derivative zero). That matters here: the
 * entropy table ends at 800 MeV while the anchor grid runs to 2030 MeV, and
 * extrapolating a cubic that far is meaningless, whereas the linear
 * continuation of s/T^3 stays close to the Stefan-Boltzmann approach. The
 * previous implementation instead clamped the value while still taking the
 * end-knot slope for the derivative, which is not the derivative of the
 * function it was evaluating.
 */
class CubicSpline {
  Vec x_, a_, b_, c_, d_;
  int n_ = 0;

public:
  CubicSpline() = default;

  void build(const Vec &x, const Vec &y) {
    n_ = static_cast<int>(x.size());
    if (n_ < 3)
      throw std::runtime_error("EntropyContours: spline needs >= 3 knots");
    x_ = x;
    a_ = y;
    const int nm1 = n_ - 1;
    Vec h(nm1), alpha(n_, 0.0);
    for (int i = 0; i < nm1; i++)
      h[i] = x[i + 1] - x[i];
    for (int i = 1; i < nm1; i++)
      alpha[i] =
          3.0 * ((y[i + 1] - y[i]) / h[i] - (y[i] - y[i - 1]) / h[i - 1]);
    c_.assign(n_, 0.0);
    Vec l(n_, 1.0), mu(n_, 0.0), z(n_, 0.0);
    for (int i = 1; i < nm1; i++) {
      l[i] = 2.0 * (x[i + 1] - x[i - 1]) - h[i - 1] * mu[i - 1];
      mu[i] = h[i] / l[i];
      z[i] = (alpha[i] - h[i - 1] * z[i - 1]) / l[i];
    }
    for (int j = nm1 - 1; j >= 1; j--)
      c_[j] = z[j] - mu[j] * c_[j + 1];
    b_.resize(nm1);
    d_.resize(nm1);
    for (int i = 0; i < nm1; i++) {
      b_[i] = (y[i + 1] - y[i]) / h[i] - h[i] * (c_[i + 1] + 2.0 * c_[i]) / 3.0;
      d_[i] = (c_[i + 1] - c_[i]) / (3.0 * h[i]);
    }
  }

  double front() const { return x_.front(); }
  double back() const { return x_.back(); }

  /** Value and first two derivatives at xv, in one pass. */
  void eval(double xv, double &f, double &f1, double &f2) const {
    /* linear continuation outside the knots */
    if (xv < x_[0]) {
      const double s = b_[0]; /* spline slope at the first knot */
      f = a_[0] + s * (xv - x_[0]);
      f1 = s;
      f2 = 0.0;
      return;
    }
    if (xv > x_[n_ - 1]) {
      const int i = n_ - 2;
      const double h = x_[n_ - 1] - x_[i];
      const double s = b_[i] + 2.0 * c_[i] * h + 3.0 * d_[i] * h * h;
      f = a_[n_ - 1] + s * (xv - x_[n_ - 1]);
      f1 = s;
      f2 = 0.0;
      return;
    }
    int lo = 0, hi = n_ - 1;
    while (hi - lo > 1) {
      const int mid = (lo + hi) / 2;
      if (x_[mid] > xv)
        hi = mid;
      else
        lo = mid;
    }
    if (lo > n_ - 2)
      lo = n_ - 2;
    const double dx = xv - x_[lo];
    f = a_[lo] + dx * (b_[lo] + dx * (c_[lo] + dx * d_[lo]));
    f1 = b_[lo] + dx * (2.0 * c_[lo] + 3.0 * dx * d_[lo]);
    f2 = 2.0 * c_[lo] + 6.0 * d_[lo] * dx;
  }

  double value(double xv) const {
    double f, f1, f2;
    eval(xv, f, f1, f2);
    return f;
  }
};

Vec gaussianFilter1d(const Vec &data, double sigma) {
  const int radius = static_cast<int>(std::ceil(4.0 * sigma));
  const int n = static_cast<int>(data.size());
  Vec kernel(2 * radius + 1);
  double ksum = 0.0;
  for (int i = -radius; i <= radius; i++) {
    kernel[i + radius] = std::exp(-0.5 * i * i / (sigma * sigma));
    ksum += kernel[i + radius];
  }
  for (auto &k : kernel)
    k /= ksum;
  Vec out(n);
  for (int i = 0; i < n; i++) {
    double val = 0.0;
    for (int j = -radius; j <= radius; j++) {
      int idx = i + j; /* reflect at the edges */
      if (idx < 0)
        idx = -idx;
      if (idx >= n)
        idx = 2 * (n - 1) - idx;
      idx = std::max(0, std::min(n - 1, idx));
      val += data[idx] * kernel[j + radius];
    }
    out[i] = val;
  }
  return out;
}

/* The .dT1 / .spln tables are whitespace-separated: T, value, error, then the
 * bootstrap samples. Only the first two columns are used. */
void loadTable(const std::string &path, Vec &T, Vec &val) {
  std::ifstream f(path);
  if (!f.is_open())
    throw std::runtime_error("EntropyContours: cannot open " + path);
  T.clear();
  val.clear();
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#')
      continue;
    std::istringstream iss(line);
    double t, v;
    if (iss >> t >> v) {
      T.push_back(t);
      val.push_back(v);
    }
  }
  if (T.size() < 3)
    throw std::runtime_error("EntropyContours: too few rows in " + path);
}

/** The tabulated lattice input, as a ContourEoS::LatticeSource. */
class TabulatedLattice : public ContourEoS::LatticeSource {
public:
  explicit TabulatedLattice(const std::string &chisDir) {
    /* pair order: 0 BB, 1 QQ, 2 SS, 3 BQ, 4 BS, 5 QS */
    static const char *names[6] = {"Chi2B",   "Chi2Q",   "Chi2S",
                                   "Chi11BQ", "Chi11BS", "Chi11SQ"};
    Vec T, v;
    for (int p = 0; p < 6; ++p) {
      loadTable(chisDir + "/" + names[p] + "_30-2000.dT1", T, v);
      chi_[p].build(T, gaussianFilter1d(v, SMOOTH_SIGMA));
    }
    loadTable(chisDir + "/entro_2013_hrg+extrap.spln", T, v);
    sT3_.build(T, gaussianFilter1d(v, SMOOTH_SIGMA));
  }

  void at(double T, ContourEoS::LatticeAt &L) const override {
    for (int p = 0; p < 6; ++p)
      chi_[p].eval(T, L.chi[p][0], L.chi[p][1], L.chi[p][2]);
    /* dimensional entropy density s(T) = T^3 (s/T^3)(T) and its T-derivatives
     * by the Leibniz rule (scontours::EntropyDensity). */
    double f0, f1, f2;
    sT3_.eval(T, f0, f1, f2);
    L.s0[0] = T * T * T * f0;
    L.s0[1] = 3.0 * T * T * f0 + T * T * T * f1;
    L.s0[2] = 6.0 * T * f0 + 6.0 * T * T * f1 + T * T * T * f2;
  }

  double entropyDensity(double T) const override {
    return T * T * T * sT3_.value(T);
  }

private:
  CubicSpline chi_[6];
  CubicSpline sT3_;
};

bool g_initialized = false;
bool g_useHRG = true;
std::unique_ptr<TabulatedLattice> g_lattice;
std::unique_ptr<ContourEoS::Engine> g_engine;

/* Anchor grid: the susceptibility tables span 30-2000 MeV, and every anchor the
 * EoS can reach satisfies T0 >= T_phys >= Tlow = 80 MeV, so 60 MeV is a safe
 * lower end well inside the data. The upper end matches the previous
 * implementation's reach (the table end plus headroom); beyond 2000 MeV the
 * splines continue linearly. */
ContourEoS::Engine::Options engineOptions() {
  ContourEoS::Engine::Options o;
  o.Tlow = 80.0;
  o.T0Min = 60.0;
  o.T0Max = 2030.0;
  o.gridPoints = 1000;
  o.quadNodes = 64;
  return o;
}

/* The contour EoS is anchored at Tlow, so it is defined for T >= Tlow; below it
 * the seam model itself (HRG::eval) is the thermodynamics. */
bool reachableT(double T) {
  return !(g_useHRG && T < g_engine->options().Tlow);
}

bool lookup(double T, const ContourValues &c, ContourEoS::AnchorResult &r) {
  if (!g_initialized)
    throw std::runtime_error("EntropyContours not initialized");
  if (!reachableT(T))
    return false;
  return g_engine->invert(c, T, r);
}

} // namespace

// ============================================================================
// Initialization.
// ============================================================================
double referenceTemperature() {
  return g_engine ? g_engine->options().Tlow : engineOptions().Tlow;
}

void initialize(const std::string &chisDir, const std::string &pdgListPath,
                double b_meson_fm3, double b_baryon_fm3, bool useHRG) {
  if (g_initialized)
    return;

  g_lattice.reset(new TabulatedLattice(chisDir));
  g_engine.reset(new ContourEoS::Engine(*g_lattice, engineOptions()));

  g_useHRG = useHRG;
  if (g_useHRG) {
    if (!HRG::isInitialized())
      HRG::initialize(pdgListPath);
    /* b_meson has no counterpart in the QvdW-HRG (mesons are non-interacting);
     * b_baryon is the QvdW eigenvolume b. */
    HRG::setExcludedVolumes(b_meson_fm3, b_baryon_fm3);
  }

  g_initialized = true;
}

void cleanup() {
  if (!g_initialized)
    return;
  g_engine.reset();
  g_lattice.reset();
  /* HRG state is shared with EntrContParam; we don't tear it down here. */
  g_initialized = false;
}

bool isInitialized() { return g_initialized; }

// ============================================================================
// Contour evaluation.
// ============================================================================
ContourValues evalContour(double muB, double muQ, double muS) {
  if (!g_initialized)
    throw std::runtime_error("EntropyContours not initialized");
  return g_engine->build(muB, muQ, muS, g_useHRG);
}

ContourValues evalContour(double muB, double muQ) {
  return evalContour(muB, muQ, 0.0);
}

// ============================================================================
// Thermodynamic accessors.
// ============================================================================
double sQCD(double, double, double T, const ContourValues &c) {
  ContourEoS::AnchorResult r;
  if (!lookup(T, c, r) || !std::isfinite(r.s))
    return NaN;
  return r.s;
}

double BarDens(double, double, double T, const ContourValues &c) {
  ContourEoS::AnchorResult r;
  if (!lookup(T, c, r) || !std::isfinite(r.nB))
    return NaN;
  return r.nB;
}

double QCDcharge(double, double, double T, const ContourValues &c) {
  ContourEoS::AnchorResult r;
  if (!lookup(T, c, r) || !std::isfinite(r.nQ))
    return NaN;
  return r.nQ;
}

double StrDens(double, double, double T, const ContourValues &c) {
  ContourEoS::AnchorResult r;
  if (!lookup(T, c, r) || !std::isfinite(r.nS))
    return NaN;
  return r.nS;
}

double pQCD(double, double, double T, const ContourValues &c) {
  ContourEoS::AnchorResult r;
  if (!lookup(T, c, r) || !std::isfinite(r.P))
    return NaN;
  return r.P;
}

double eQCD(double, double, double T, const ContourValues &c) {
  ContourEoS::AnchorResult r;
  if (!lookup(T, c, r))
    return NaN;
  /* eps = T s - P + sum_X mu_X n_X */
  return T * r.s - r.P + c.muB * r.nB + c.muQ * r.nQ + c.muS * r.nS;
}

// ---- convenience overloads -------------------------------------------------

double sQCD(double muB, double muQ, double T) {
  const ContourValues c = evalContour(muB, muQ);
  return sQCD(muB, muQ, T, c);
}
double BarDens(double muB, double muQ, double T) {
  const ContourValues c = evalContour(muB, muQ);
  return BarDens(muB, muQ, T, c);
}
double QCDcharge(double muB, double muQ, double T) {
  const ContourValues c = evalContour(muB, muQ);
  return QCDcharge(muB, muQ, T, c);
}
double StrDens(double muB, double muQ, double T) {
  const ContourValues c = evalContour(muB, muQ);
  return StrDens(muB, muQ, T, c);
}
double pQCD(double muB, double muQ, double T) {
  const ContourValues c = evalContour(muB, muQ);
  return pQCD(muB, muQ, T, c);
}
double eQCD(double muB, double muQ, double T) {
  const ContourValues c = evalContour(muB, muQ);
  return eQCD(muB, muQ, T, c);
}

} // namespace EntropyContours
