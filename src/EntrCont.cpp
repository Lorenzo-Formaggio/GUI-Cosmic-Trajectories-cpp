#include "EntrCont.hpp"

#include "HRG.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace EntropyContours {

using Vec = std::vector<double>;
static const double NaN = std::numeric_limits<double>::quiet_NaN();

// ============================================================================
// Configuration -- identical to EntrCont.
// ============================================================================
static constexpr int N_T0 = 1000;
static constexpr double T0_HEADROOM = 30.0;
static constexpr double SIGMA = 2.0;
static constexpr double T_HRG_MATCH = 80.0;
static constexpr double HBARC_GEVFM = 0.1973269;

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
static Vec gaussian_filter1d(const Vec &data, double sigma) {
  int radius = (int)std::ceil(4.0 * sigma);
  int n = (int)data.size();
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
      int idx = i + j;
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

// ============================================================================
// File loading (same as EntrCont).
// ============================================================================
struct Data3 {
  Vec T, val, err;
};
static Data3 load3(const std::string &dir, const std::string &name) {
  std::string path = dir + "/" + name + "_30-2000.dT1";
  std::ifstream f(path);
  if (!f.is_open())
    throw std::runtime_error("EntropyContours: cannot open " + path);
  Data3 d;
  std::string line;
  while (std::getline(f, line)) {
    if (line.empty() || line[0] == '#')
      continue;
    std::istringstream iss(line);
    double t, v, e;
    if (iss >> t >> v >> e) {
      d.T.push_back(t);
      d.val.push_back(v);
      d.err.push_back(e);
    }
  }
  return d;
}
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

  Data3 chi2B = load3(chisDir, "Chi2B");
  Data3 chi2S = load3(chisDir, "Chi2S");
  Data3 chi2Q = load3(chisDir, "Chi2Q");
  Data3 chi11BS = load3(chisDir, "Chi11BS");
  Data3 chi11BQ = load3(chisDir, "Chi11BQ");
  Data3 chi11SQ = load3(chisDir, "Chi11SQ");

  const Vec &T_lat = chi2B.T;
  g_T0_MIN = T_lat.front();
  g_T0_MAX = T_lat.back() + T0_HEADROOM;

  auto make_spline = [&](const Vec &T, const Vec &y) {
    Vec ys = gaussian_filter1d(y, SIGMA);
    CubicSpline spl;
    spl.build(T, ys);
    return spl;
  };
  CubicSpline C2B = make_spline(T_lat, chi2B.val);
  CubicSpline C2S = make_spline(T_lat, chi2S.val);
  CubicSpline C2Q = make_spline(T_lat, chi2Q.val);
  CubicSpline C11BS = make_spline(T_lat, chi11BS.val);
  CubicSpline C11BQ = make_spline(T_lat, chi11BQ.val);
  CubicSpline C11SQ = make_spline(T_lat, chi11SQ.val);

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

    Vec dPdT0(N_T0), P_p(N_T0);
    for (int i = 0; i < N_T0; i++) {
      double T0 = g_T0g[i];
      dPdT0[i] = T0 * T0 * T0 * g_s0_vec[i] * c.dTdT0[i];
    }
    Vec P_base = cumtrapz(dPdT0, g_T0g);
    for (int i = 0; i < N_T0; i++) {
      double Tp = c.T_phys[i];
      double Tp4 = Tp * Tp * Tp * Tp;
      P_p[i] = P_base[i] + Tp4 * 0.5 * c2e[i] * mu_hat[i] * mu_hat[i];
    }

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

} // namespace EntropyContours
