#include "EntrContParam.hpp"

#include "HRG.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace EntropyContoursParam {

using Vec = std::vector<double>;
static const double NaN = std::numeric_limits<double>::quiet_NaN();

// ============================================================================
// Configuration -- identical to EntrCont.
// ============================================================================
static constexpr int N_T0 = 1000;
static constexpr double T0_HEADROOM = 30.0;
static constexpr double T_HRG_MATCH = 80.0;
static constexpr double HBARC_GEVFM = 0.1973269;

// Lattice-emulating grid on which the analytic chi parametrizations are
// sampled before being fed to the spline pipeline. Matches the
// "_30-2000.dT1" file convention previously loaded from disk.
static constexpr double T_LAT_MIN = 30.0;
static constexpr double T_LAT_MAX = 2000.0;
static constexpr double T_LAT_DT = 1.0;

static constexpr double PI_CONST = 3.14159265358979323846;
static constexpr double SQRT2_CONST = 1.41421356237309504880;

// ============================================================================
// Analytic parametrizations of QCD susceptibilities, transcribed from
// Parameterization.c (4D_scontours_Parameterization_v2.3_CP_Finder.nb).
// T is in MeV; all returned chis are dimensionless.
// ============================================================================
static double michael_term1(double x, double mpi, double d0, double d1,
                            double d2) {
  double boltz =
      std::pow(2.0 * mpi / (PI_CONST * x), 1.5) * std::exp(-mpi / x);
  return d0 * boltz / (1.0 + std::pow(x / d1, d2));
}

static double michael_term2(double x, double d1, double d2, double d3,
                            double d5) {
  double expfac = std::exp(-std::pow(d5, 4.0) / std::pow(x, 4.0));
  return d3 * expfac / (1.0 + std::pow(x / d1, -d2));
}

static double chi2B_param(double T) {
  constexpr double mpi = 938.0 / 200.0;
  constexpr double d0 = 3.6076255940379123;
  constexpr double d1 = 0.7502148791172487;
  constexpr double d2 = 21.155345203652914;
  constexpr double d3 = 0.3305183902184058;
  constexpr double d5 = 0.7585835763532186;
  double x = T / 200.0;
  return michael_term1(x, mpi, d0, d1, d2) + michael_term2(x, d1, d2, d3, d5);
}

static double chi2Q_param(double T) {
  constexpr double mpi = 140.0 / 200.0;
 constexpr double d0 = 0.5600003526;
  constexpr double d1 = 0.7799273141;
  constexpr double d2 = 4.726057114;
  constexpr double d3 = 0.6995862169;
  constexpr double d5 = 0.0002225410308;
  double x = T / 200.0;
  return michael_term1(x, mpi, d0, d1, d2) + michael_term2(x, d1, d2, d3, d5);
}

static double chi2S_param(double T) {
  constexpr double mpi = 495.0 / 200.0;
  constexpr double d0 = 0.8322621087687887;
  constexpr double d1 = 0.8295810817249416;
  constexpr double d2 = 8.07331902668102;
  constexpr double d3 = 0.8595952510551099;
  constexpr double d5 = 0.6186329300539317;
  double x = T / 200.0;
  return michael_term1(x, mpi, d0, d1, d2) + michael_term2(x, d1, d2, d3, d5);
}

static double chi11BS_param(double T) {
  constexpr double mpi = 140.0 / 200.0;
  constexpr double d0 = 0.0003281619741080934;
  constexpr double d1 = 0.8574332020728931;
  constexpr double d2 = 9.692874886279938;
  constexpr double d3 = 0.25674721454206617;
  constexpr double d5 = 0.46193348422190794;
  double x = T / 200.0;
  return -(michael_term1(x, mpi, d0, d1, d2) +
           michael_term2(x, d1, d2, d3, d5));
}

static double chi11SQ_param(double T) {
  // chi11QS in Parameterization.c == chi11SQ here.
  constexpr double mpi = 495.0 / 200.0;
  constexpr double d0 = 0.3350268638607059;
  constexpr double d1 = 0.867722746742251;
  constexpr double d2 = 7.604312446947438;
  constexpr double d3 = 0.2808457840677243;
  constexpr double d5 = 0.414368;
  double x = T / 200.0;
  return michael_term1(x, mpi, d0, d1, d2) + michael_term2(x, d1, d2, d3, d5);
}

static double skew_normal(double T, double mu, double sig, double alp) {
  double z = (T - mu) / sig;
  double gauss =
      (2.0 / sig) * std::exp(-0.5 * z * z) / std::sqrt(2.0 * PI_CONST);
  double skew = 0.5 * (1.0 + std::erf(alp * z / SQRT2_CONST));
  return gauss * skew;
}

static double chi11BQ_param(double T) {
  constexpr double p0 = 0.35975712110963015;
  constexpr double p1 = 140.0;
  constexpr double p2 = 21.994844290144993;
  constexpr double p3 = 0.144163;
  constexpr double p4 = 1.8406766329515367;
  constexpr double p5 = 150.83855166636303;
  constexpr double p6 = 50.28303873202961;
  constexpr double p7 = 3.1967362601870666;
  return p0 * skew_normal(T, p1, p2, p3) + p4 * skew_normal(T, p5, p6, p7);
}

// ============================================================================
// Cubic spline (same implementation as EntrCont.cpp).
// ============================================================================
class CubicSpline {
  Vec x_, a_, b_, c_, d_;
  int n_ = 0;

public:
  CubicSpline() = default;
  void build(const Vec &x, const Vec &y) {
    n_ = (int)x.size();
    x_ = x;
    a_ = y;
    int nm1 = n_ - 1;
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
  double eval(double xv, int deriv = 0) const {
    if (n_ < 2)
      return 0.0;
    int lo = 0, hi = n_ - 2;
    if (xv <= x_[0])
      lo = 0;
    else if (xv >= x_[n_ - 1])
      lo = n_ - 2;
    else {
      while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (x_[mid] > xv)
          hi = mid;
        else
          lo = mid;
      }
    }
    double dx = xv - x_[lo];
    if (deriv == 0)
      return a_[lo] + b_[lo] * dx + c_[lo] * dx * dx + d_[lo] * dx * dx * dx;
    return b_[lo] + 2.0 * c_[lo] * dx + 3.0 * d_[lo] * dx * dx;
  }
  Vec eval_vec(const Vec &xs, int deriv = 0) const {
    Vec out(xs.size());
    for (size_t i = 0; i < xs.size(); i++)
      out[i] = eval(xs[i], deriv);
    return out;
  }
};

// ============================================================================
// Numerical helpers (same as EntrCont).
// ============================================================================
static Vec linspace(double lo, double hi, int n) {
  Vec v(n);
  double d = (n > 1) ? (hi - lo) / (n - 1) : 0.0;
  for (int i = 0; i < n; i++)
    v[i] = lo + d * i;
  return v;
}
static Vec np_gradient(const Vec &y, const Vec &x) {
  int n = (int)y.size();
  Vec g(n);
  if (n < 2)
    return g;
  g[0] = (y[1] - y[0]) / (x[1] - x[0]);
  for (int i = 1; i < n - 1; i++)
    g[i] = (y[i + 1] - y[i - 1]) / (x[i + 1] - x[i - 1]);
  g[n - 1] = (y[n - 1] - y[n - 2]) / (x[n - 1] - x[n - 2]);
  return g;
}
static Vec cumtrapz(const Vec &y, const Vec &x) {
  int n = (int)y.size();
  Vec r(n, 0.0);
  for (int i = 1; i < n; i++)
    r[i] = r[i - 1] + 0.5 * (y[i] + y[i - 1]) * (x[i] - x[i - 1]);
  return r;
}

// ============================================================================
// File loading (entropy spline only -- chis come from the analytic
// parametrizations at the top of this file).
// ============================================================================
struct Data2 {
  Vec col0, col1;
};
static Data2 load2(const std::string &path) {
  std::ifstream f(path);
  if (!f.is_open())
    throw std::runtime_error("EntropyContours: cannot open " + path);
  Data2 d;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#')
      continue;
    std::istringstream iss(line);
    double a, b;
    if (iss >> a >> b) {
      d.col0.push_back(a);
      d.col1.push_back(b);
    }
  }
  return d;
}

// ============================================================================
// Module state. No HRGSlice -- the seam anchor comes from HRG::eval (EV-HRG).
// ============================================================================
static bool g_initialized = false;
static bool g_useHRG = true;
static Vec g_T0g;
static Vec g_sp_vec, g_s0_vec;
static Vec g_c2B, g_c2S, g_c2Q, g_c11BS, g_c11BQ, g_c11SQ;
static Vec g_c2Bp, g_c2Sp, g_c2Qp, g_c11BSp, g_c11BQp, g_c11SQp;
static double g_T0_MIN = 0.0, g_T0_MAX = 0.0;
static double g_ent_T_min = 0.0, g_ent_T_max = 0.0;
static CubicSpline g_s0_spl;

// ============================================================================
// Initialization.
// ============================================================================
void initialize(const std::string &chisDir, const std::string &pdgListPath,
                double b_meson_fm3, double b_baryon_fm3, bool useHRG) {
  if (g_initialized)
    return;

  // Susceptibilities come from the analytic parametrizations above
  // (see Parameterization.c), sampled on a 30..2000 MeV / dT = 1 MeV grid
  // to match the original file convention. The spline machinery is kept
  // so that downstream value/derivative lookups are unchanged.
  int n_lat = (int)std::lround((T_LAT_MAX - T_LAT_MIN) / T_LAT_DT) + 1;
  Vec T_lat(n_lat);
  Vec v_chi2B(n_lat), v_chi2S(n_lat), v_chi2Q(n_lat);
  Vec v_chi11BS(n_lat), v_chi11BQ(n_lat), v_chi11SQ(n_lat);
  for (int i = 0; i < n_lat; i++) {
    double T = T_LAT_MIN + i * T_LAT_DT;
    T_lat[i] = T;
    v_chi2B[i] = chi2B_param(T);
    v_chi2S[i] = chi2S_param(T);
    v_chi2Q[i] = chi2Q_param(T);
    v_chi11BS[i] = chi11BS_param(T);
    v_chi11BQ[i] = chi11BQ_param(T);
    v_chi11SQ[i] = chi11SQ_param(T);
  }

  g_T0_MIN = T_lat.front();
  g_T0_MAX = T_lat.back() + T0_HEADROOM;

  // No Gaussian smoothing: the parametrizations are already smooth analytic
  // functions, so feeding them through gaussian_filter1d would just distort
  // the seam without removing any noise.
  auto make_spline = [&](const Vec &T, const Vec &y) {
    CubicSpline spl;
    spl.build(T, y);
    return spl;
  };
  CubicSpline C2B = make_spline(T_lat, v_chi2B);
  CubicSpline C2S = make_spline(T_lat, v_chi2S);
  CubicSpline C2Q = make_spline(T_lat, v_chi2Q);
  CubicSpline C11BS = make_spline(T_lat, v_chi11BS);
  CubicSpline C11BQ = make_spline(T_lat, v_chi11BQ);
  CubicSpline C11SQ = make_spline(T_lat, v_chi11SQ);

  Data2 d_ent = load2(chisDir + "/entro_2013_hrg+extrap.spln");
  g_s0_spl.build(d_ent.col0, d_ent.col1);
  g_ent_T_min = d_ent.col0.front();
  g_ent_T_max = d_ent.col0.back();

  g_T0g = linspace(g_T0_MIN, g_T0_MAX, N_T0);
  g_sp_vec.resize(N_T0);
  g_s0_vec.resize(N_T0);
  for (int i = 0; i < N_T0; i++) {
    double tc = std::max(g_ent_T_min, std::min(g_ent_T_max, g_T0g[i]));
    double soT3 = g_s0_spl.eval(tc, 0);
    double dsoT3 = g_s0_spl.eval(tc, 1);
    g_s0_vec[i] = soT3;
    g_sp_vec[i] = 3.0 * tc * tc * soT3 + tc * tc * tc * dsoT3;
  }
  g_c2B = C2B.eval_vec(g_T0g, 0);
  g_c2Bp = C2B.eval_vec(g_T0g, 1);
  g_c2S = C2S.eval_vec(g_T0g, 0);
  g_c2Sp = C2S.eval_vec(g_T0g, 1);
  g_c2Q = C2Q.eval_vec(g_T0g, 0);
  g_c2Qp = C2Q.eval_vec(g_T0g, 1);
  g_c11BS = C11BS.eval_vec(g_T0g, 0);
  g_c11BSp = C11BS.eval_vec(g_T0g, 1);
  g_c11BQ = C11BQ.eval_vec(g_T0g, 0);
  g_c11BQp = C11BQ.eval_vec(g_T0g, 1);
  g_c11SQ = C11SQ.eval_vec(g_T0g, 0);
  g_c11SQp = C11SQ.eval_vec(g_T0g, 1);

  g_useHRG = useHRG;
  if (g_useHRG) {
    if (!HRG::isInitialized())
      HRG::initialize(pdgListPath);
    HRG::setExcludedVolumes(b_meson_fm3, b_baryon_fm3);
  }

  g_initialized = true;
}

void cleanup() {
  if (!g_initialized)
    return;
  g_T0g.clear();
  g_sp_vec.clear();
  g_s0_vec.clear();
  g_c2B.clear();
  g_c2S.clear();
  g_c2Q.clear();
  g_c11BS.clear();
  g_c11BQ.clear();
  g_c11SQ.clear();
  g_c2Bp.clear();
  g_c2Sp.clear();
  g_c2Qp.clear();
  g_c11BSp.clear();
  g_c11BQp.clear();
  g_c11SQp.clear();
  g_s0_spl = CubicSpline();
  // HRG state is shared (EntrCont may also use it); we don't clean it.
  g_initialized = false;
}

bool isInitialized() { return g_initialized; }

// ============================================================================
// Contour evaluation. Same math as EntrCont; only the seam anchor differs.
// ============================================================================
ContourValues evalContour(double muB, double muQ) {
  if (!g_initialized)
    throw std::runtime_error("EntropyContours not initialized");

  ContourValues c;
  c.muB = muB;
  c.muQ = muQ;
  c.T_phys.assign(N_T0, 0.0);
  c.dTdT0.assign(N_T0, 0.0);
  c.nB_T3.assign(N_T0, 0.0);
  c.nS_T3.assign(N_T0, 0.0);
  c.nQ_T3.assign(N_T0, 0.0);
  c.s_T3.assign(N_T0, 0.0);
  c.P_T4.assign(N_T0, 0.0);

  double mu_tot = std::sqrt(muB * muB + muQ * muQ);
  bool zero_mu = (mu_tot < 1e-10);
  double nBh = zero_mu ? 0.0 : (muB / mu_tot);
  double nSh = 0.0;
  double nQh = zero_mu ? 0.0 : (muQ / mu_tot);
  double mu = mu_tot;

  Vec a2(N_T0), c2e(N_T0), c2B_d(N_T0), c2S_d(N_T0), c2Q_d(N_T0), c2B_dp(N_T0);
  for (int i = 0; i < N_T0; i++) {
    double e = nBh * nBh * g_c2B[i] + nSh * nSh * g_c2S[i] +
               nQh * nQh * g_c2Q[i] + 2.0 * nBh * nSh * g_c11BS[i] +
               2.0 * nBh * nQh * g_c11BQ[i] + 2.0 * nSh * nQh * g_c11SQ[i];
    double ep = nBh * nBh * g_c2Bp[i] + nSh * nSh * g_c2Sp[i] +
                nQh * nQh * g_c2Qp[i] + 2.0 * nBh * nSh * g_c11BSp[i] +
                2.0 * nBh * nQh * g_c11BQp[i] + 2.0 * nSh * nQh * g_c11SQp[i];
    double T0 = g_T0g[i];
    double f2 = 2.0 * T0 * e + T0 * T0 * ep;
    a2[i] = -f2 / g_sp_vec[i];
    c2e[i] = e;
    c2B_d[i] = nBh * g_c2B[i] + nSh * g_c11BS[i] + nQh * g_c11BQ[i];
    c2S_d[i] = nBh * g_c11BS[i] + nSh * g_c2S[i] + nQh * g_c11SQ[i];
    c2Q_d[i] = nBh * g_c11BQ[i] + nSh * g_c11SQ[i] + nQh * g_c2Q[i];
    c2B_dp[i] = nBh * g_c2Bp[i] + nSh * g_c11BSp[i] + nQh * g_c11BQp[i];
  }

  double mu2 = mu * mu;
  for (int i = 0; i < N_T0; i++)
    c.T_phys[i] = g_T0g[i] + a2[i] * mu2 * 0.5;

  Vec a2p = np_gradient(a2, g_T0g);
  for (int i = 0; i < N_T0; i++)
    c.dTdT0[i] = 1.0 + a2p[i] * mu2 * 0.5;

  if (zero_mu) {
    for (int i = 0; i < N_T0; i++) {
      double Tp = std::max(c.T_phys[i], 1.0);
      double tc = std::max(g_ent_T_min, std::min(g_ent_T_max, Tp));
      c.nB_T3[i] = 0.0;
      c.nS_T3[i] = 0.0;
      c.nQ_T3[i] = 0.0;
      c.s_T3[i] = g_s0_spl.eval(tc);
      c.P_T4[i] = 0.0;
    }
  } else {
    Vec mu_hat(N_T0);
    for (int i = 0; i < N_T0; i++) {
      double Tp = std::max(c.T_phys[i], 1.0);
      mu_hat[i] = mu / Tp;
      double rat = g_T0g[i] / Tp;
      double rat3 = rat * rat * rat;
      c.nB_T3[i] = c2B_d[i] * mu_hat[i];
      c.nS_T3[i] = c2S_d[i] * mu_hat[i];
      c.nQ_T3[i] = c2Q_d[i] * mu_hat[i];
      c.s_T3[i] = g_s0_vec[i] * rat3 + c2e[i] * mu_hat[i] * mu_hat[i];
    }

    Vec dPdT0(N_T0);
    for (int i = 0; i < N_T0; i++) {
      double T0 = g_T0g[i];
      dPdT0[i] = T0 * T0 * T0 * g_s0_vec[i] * c.dTdT0[i];
    }
    // P_base already contains the O(mu^2) contour pressure shift: the
    // dT_phys/dT0 = (1 + alpha_2' mu^2 / 2) factor combined with the
    // alpha_2 shift of the upper limit produces exactly
    //   0.5 * T_phys^2 * c_2^e * mu^2
    // via integration by parts of T_0^3 s_0 alpha_2', using the identity
    //   alpha_2 = -d(T^2 c_2^e)/dT / d(T^3 s_0)/dT
    // Earlier versions of this file (and main.cpp) added an explicit
    //   + T_phys^4 * 0.5 * c_2^e * muhat^2
    // term on top, double-counting the Taylor coefficient and giving
    // chi_2^code = 2 * chi_2^lat at fixed T.  We don't add it.  See
    // main.cpp's solve_slice for the full derivation.
    Vec P_p = cumtrapz(dPdT0, g_T0g);

    // HRG seam anchor: replace the file-based HRGSlice lookup of EntrCont
    // with HRG::eval at T = T_HRG_MATCH (computed on the fly via EV-HRG).
    // Same shift formula afterwards.
    bool anchored = false;
    if (g_useHRG) {
      int k_anc = -1;
      double f_anc = 0.0;
      for (int k = 0; k < N_T0 - 1; k++) {
        if ((c.T_phys[k] - T_HRG_MATCH) * (c.T_phys[k + 1] - T_HRG_MATCH) <=
            0.0) {
          double dT = c.T_phys[k + 1] - c.T_phys[k];
          f_anc =
              (std::abs(dT) > 1e-12) ? (T_HRG_MATCH - c.T_phys[k]) / dT : 0.0;
          k_anc = k;
          break;
        }
      }
      if (k_anc >= 0) {
        auto interp = [&](const Vec &v) {
          return v[k_anc] + f_anc * (v[k_anc + 1] - v[k_anc]);
        };
        double P_lat_a = interp(P_p);
        double nB_lat_a = interp(c.nB_T3);
        double nQ_lat_a = interp(c.nQ_T3);
        double nS_lat_a = interp(c.nS_T3);
        double s_lat_a = interp(c.s_T3);

        double muB_anc = mu * nBh; // = muB
        double muQ_anc = mu * nQh; // = muQ
        // *** Substitution vs EntrCont: HRG::eval (EV-HRG) on the fly,
        // *** instead of HRGSlice::lookup file interpolation.
        // *** muS = 0 for the cosmic-trajectory case.
        HRG::Result hrg = HRG::eval(T_HRG_MATCH, muB_anc, muQ_anc, 0.0);

        if (std::isfinite(hrg.P_T4) && std::isfinite(hrg.nB_T3) &&
            std::isfinite(hrg.nQ_T3) && std::isfinite(hrg.nS_T3) &&
            std::isfinite(hrg.s_T3)) {
          const double T_a = T_HRG_MATCH;
          const double T_a4 = T_a * T_a * T_a * T_a;
          double dP_dim = hrg.P_T4 * T_a4 - P_lat_a;
          double dnB = hrg.nB_T3 - nB_lat_a;
          double dnQ = hrg.nQ_T3 - nQ_lat_a;
          double dnS = hrg.nS_T3 - nS_lat_a;
          double ds = hrg.s_T3 - s_lat_a;
          for (int i = 0; i < N_T0; i++) {
            double Tp = c.T_phys[i];
            if (Tp < 1.0) {
              P_p[i] = c.nB_T3[i] = c.nQ_T3[i] = c.nS_T3[i] = c.s_T3[i] = NaN;
              continue;
            }
            double rat3 = (T_a / Tp) * (T_a / Tp) * (T_a / Tp);
            P_p[i] += dP_dim;
            c.nB_T3[i] += dnB * rat3;
            c.nQ_T3[i] += dnQ * rat3;
            c.nS_T3[i] += dnS * rat3;
            c.s_T3[i] += ds * rat3;
          }
          anchored = true;
        }
      }
      if (!anchored) {
        for (int i = 0; i < N_T0; i++) {
          P_p[i] = c.nB_T3[i] = c.nQ_T3[i] = c.nS_T3[i] = c.s_T3[i] = NaN;
        }
      }
    }

    for (int i = 0; i < N_T0; i++) {
      double Tp = c.T_phys[i];
      double Tp4 = std::max(Tp, 1.0);
      Tp4 = Tp4 * Tp4 * Tp4 * Tp4;
      c.P_T4[i] = P_p[i] / Tp4;
    }
  }

  return c;
}

// ============================================================================
// T_phys -> T0 inversion (same as EntrCont).
// ============================================================================
namespace {
struct InvBranch {
  double nB, nS, nQ, s, P, jac;
};
} // namespace

static bool invertAtT(const ContourValues &c, double T_target, InvBranch &out) {
  std::vector<InvBranch> branches;
  for (int k = 0; k < N_T0 - 1; k++) {
    double d0 = c.T_phys[k] - T_target;
    double d1 = c.T_phys[k + 1] - T_target;
    if (d0 * d1 >= 0.0)
      continue;
    double dT = c.T_phys[k + 1] - c.T_phys[k];
    if (std::abs(dT) < 1e-12)
      continue;
    double f = (T_target - c.T_phys[k]) / dT;
    InvBranch b;
    b.nB = c.nB_T3[k] + f * (c.nB_T3[k + 1] - c.nB_T3[k]);
    b.nS = c.nS_T3[k] + f * (c.nS_T3[k + 1] - c.nS_T3[k]);
    b.nQ = c.nQ_T3[k] + f * (c.nQ_T3[k + 1] - c.nQ_T3[k]);
    b.s = c.s_T3[k] + f * (c.s_T3[k + 1] - c.s_T3[k]);
    b.P = c.P_T4[k] + f * (c.P_T4[k + 1] - c.P_T4[k]);
    b.jac = c.dTdT0[k] + f * (c.dTdT0[k + 1] - c.dTdT0[k]);
    branches.push_back(b);
  }
  if (branches.empty())
    return false;
  if (branches.size() == 1) {
    out = branches[0];
    return true;
  }
  const InvBranch *best = nullptr;
  for (auto &b : branches) {
    if (b.jac > 0 && (!best || b.P > best->P))
      best = &b;
  }
  if (!best) {
    best = &branches[0];
    for (auto &b : branches)
      if (b.P > best->P)
        best = &b;
  }
  out = *best;
  return true;
}

static bool reachableT(double T) { return !(g_useHRG && T < T_HRG_MATCH); }

double sQCD(double, double, double T, const ContourValues &c) {
  if (!g_initialized)
    throw std::runtime_error("EntropyContours not initialized");
  if (!reachableT(T))
    return NaN;
  InvBranch b;
  if (!invertAtT(c, T, b))
    return NaN;
  if (!std::isfinite(b.s))
    return NaN;
  return b.s * T * T * T;
}
double BarDens(double, double, double T, const ContourValues &c) {
  if (!g_initialized)
    throw std::runtime_error("EntropyContours not initialized");
  if (!reachableT(T))
    return NaN;
  InvBranch b;
  if (!invertAtT(c, T, b))
    return NaN;
  if (!std::isfinite(b.nB))
    return NaN;
  return b.nB * T * T * T;
}
double QCDcharge(double, double, double T, const ContourValues &c) {
  if (!g_initialized)
    throw std::runtime_error("EntropyContours not initialized");
  if (!reachableT(T))
    return NaN;
  InvBranch b;
  if (!invertAtT(c, T, b))
    return NaN;
  if (!std::isfinite(b.nQ))
    return NaN;
  return b.nQ * T * T * T;
}
double StrDens(double, double, double T, const ContourValues &c) {
  if (!g_initialized)
    throw std::runtime_error("EntropyContours not initialized");
  if (!reachableT(T))
    return NaN;
  InvBranch b;
  if (!invertAtT(c, T, b))
    return NaN;
  if (!std::isfinite(b.nS))
    return NaN;
  return b.nS * T * T * T;
}

double sQCD(double muB, double muQ, double T) {
  ContourValues c = evalContour(muB, muQ);
  return sQCD(muB, muQ, T, c);
}
double BarDens(double muB, double muQ, double T) {
  ContourValues c = evalContour(muB, muQ);
  return BarDens(muB, muQ, T, c);
}
double QCDcharge(double muB, double muQ, double T) {
  ContourValues c = evalContour(muB, muQ);
  return QCDcharge(muB, muQ, T, c);
}
double StrDens(double muB, double muQ, double T) {
  ContourValues c = evalContour(muB, muQ);
  return StrDens(muB, muQ, T, c);
}

} // namespace EntropyContoursParam
