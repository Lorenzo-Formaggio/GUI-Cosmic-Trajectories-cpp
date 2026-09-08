/*
 * first_order_surface -- locate the first-order (Maxwell) surface of the
 * Entropy Contour Param EoS (eos = 4) on a regular (T, mu_Q) grid at mu_S = 0
 * and write it in the layout of assets/first_order_surface_param.dat:
 *
 *   # T  muB  muS  muQ  dnB
 *
 * For every (T, mu_Q) the EoS is scanned in mu_B. Past the critical point the
 * contour folds and one T has two mechanically stable anchors; the engine's
 * branch selection (ContourEoS::Engine::invert: the stable branch of highest
 * pressure) switches from the dilute to the dense phase exactly where the two
 * have equal pressure, i.e. on the Maxwell line. That switch shows up as a jump
 * of the entropy density s = s0(T0) (monotonic in the anchor T0), which is
 * bracketed by a coarse scan and bisected to `tol` in mu_B. The last column is
 * dnB = n_B(dense) - n_B(dilute) in fm^-3, the density discontinuity.
 *
 * The location is independent of the QvdW-HRG seam: both phases share
 * (mu_B, mu_Q, mu_S), so the boundary terms P_ref and n_ref cancel in the
 * pressure difference and in the density jump. The seam is therefore switched
 * off here (no Thermal-FIST solve), which makes each evaluation cheap. The
 * same surface is where the Gibbs variant (eos = 6) places its mixed phase.
 *
 * Usage:
 *   first_order_surface [Tmin Tmax dT] [dmuQ] [-o FILE] [--jobs N]
 *                       [--muB-min X] [--muB-max X] [--muQ-max X]
 *                       [--fitted] [--diag FILE]
 *
 * --diag writes one row per point with the phase densities and the relative
 * pressure mismatch |P_dense - P_dilute|/P (a check that the switch really is
 * the equal-pressure point): T muQ muB dnB nB_dilute nB_dense dP/P.
 *
 * Defaults: T = 80 .. 114.5 step 0.25 MeV, dmuQ = 0.5 MeV, mu_B scanned in
 * [550, 1000] MeV, |mu_Q| <= 600 MeV, all cores. Rows are sorted by T, then
 * mu_Q. Progress goes to stderr.
 */

#include "EntrContParam.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace ECP = EntropyContoursParam;

namespace {

const double kHbarc = 197.3269804;                    /* MeV fm */
const double kHbarc3 = kHbarc * kHbarc * kHbarc;      /* MeV^3 -> fm^-3 */

struct Settings {
  double Tmin = 80.0, Tmax = 114.5, dT = 0.25;
  double dmuQ = 0.5, muQmax = 600.0;
  double muBmin = 550.0, muBmax = 1000.0;
  double coarse = 1.0;   /* MeV, coarse mu_B step of the jump search */
  double tol = 1e-6;     /* MeV, bisection tolerance in mu_B */
  double delta = 0.02;   /* coarse-scan trigger: jump of ln s within one step */
  double minJump = 0.005; /* ln s jump that must survive the bisection */
  double minDnB = 1e-3;  /* fm^-3, density jump that must survive it */
  double window = 60.0;  /* MeV, forward search window from the warm start */
  double back = 10.0;    /* MeV, warm start this far below the previous mu_B */
  double maxStep = 40.0; /* MeV, larger mu_B jumps between neighbours = fold end */
  int jobs = 0;
};

struct State {
  double s = 0.0, nB = 0.0, P = 0.0;
  bool ok = false;
};

State evalAt(double muB, double muQ, double T) {
  const ECP::ContourValues c = ECP::evalContour(muB, muQ, 0.0);
  State e;
  e.s = ECP::sQCD(muB, muQ, T, c);
  e.nB = ECP::BarDens(muB, muQ, T, c);
  e.P = ECP::pQCD(muB, muQ, T, c);
  e.ok = std::isfinite(e.s) && std::isfinite(e.nB) && std::isfinite(e.P) &&
         e.s > 0.0;
  return e;
}

struct Point {
  double T, muB, muQ, dnB;
  double nBlo, nBhi, dP; /* diagnostics: phase densities (fm^-3), |dP|/P */
};

/* Scan mu_B upward from `from` to `to` in coarse steps. Every upward jump of
 * ln s larger than delta within one step is a candidate: it is bisected, and
 * accepted only if a discontinuity survives (ln s and n_B still jump across a
 * bracket of width tol). A steep but continuous rise -- the crossover side of
 * a critical point -- fails that test and the scan simply continues. */
bool locate(const Settings &st, double T, double muQ, double from, double to,
            Point &out) {
  from = std::max(from, st.muBmin);
  to = std::min(to, st.muBmax);
  if (!(from < to))
    return false;

  double a = from;
  State ea = evalAt(a, muQ, T);
  for (double x = a + st.coarse; x <= to + 1e-9; x += st.coarse) {
    State ex = evalAt(x, muQ, T);
    if (ea.ok && ex.ok && std::log(ex.s / ea.s) > st.delta) {
      /* bisection on the branch identity: assign the midpoint to the side
       * whose ln s it is closer to (the continuous drift within the bracket is
       * far below a genuine discontinuity). */
      double lo = a, hi = x;
      State elo = ea, ehi = ex;
      bool clean = true;
      while (hi - lo > st.tol) {
        const double m = 0.5 * (lo + hi);
        const State em = evalAt(m, muQ, T);
        if (!em.ok) {
          clean = false;
          break;
        }
        const double la = std::log(elo.s), lb = std::log(ehi.s),
                     lm = std::log(em.s);
        if (lm - la < lb - lm) {
          lo = m;
          elo = em;
        } else {
          hi = m;
          ehi = em;
        }
      }
      const double dnB = (ehi.nB - elo.nB) / kHbarc3;
      if (clean && std::log(ehi.s / elo.s) > st.minJump && dnB > st.minDnB) {
        out.T = T;
        out.muQ = muQ;
        out.muB = 0.5 * (lo + hi);
        out.nBlo = elo.nB / kHbarc3;
        out.nBhi = ehi.nB / kHbarc3;
        out.dnB = dnB;
        out.dP = std::fabs(ehi.P - elo.P) / std::max(std::fabs(elo.P), 1e-300);
        return true;
      }
      /* not a discontinuity: keep scanning past the candidate */
    }
    a = x;
    ea = ex;
  }
  return false;
}

/* One temperature slice: sweep mu_Q outward from 0 in both directions with a
 * warm start from the neighbouring point; a miss triggers a full scan, and the
 * sweep stops after three consecutive misses (the fold has closed). */
void slice(const Settings &st, double T, std::vector<Point> &pts,
           std::vector<std::string> &notes) {
  Point p0;
  const bool have0 = locate(st, T, 0.0, st.muBmin, st.muBmax, p0);
  if (have0)
    pts.push_back(p0);

  for (int dirn = +1; dirn >= -1; dirn -= 2) {
    double last = have0 ? p0.muB : NAN;
    int misses = 0;
    const double qmax = have0 ? st.muQmax : 150.0; /* top of the surface only */
    for (double q = dirn * st.dmuQ; std::fabs(q) <= qmax + 1e-9;
         q += dirn * st.dmuQ) {
      Point p;
      bool ok = false;
      if (std::isfinite(last))
        ok = locate(st, T, q, last - st.back, last + st.window, p);
      if (!ok) {
        ok = locate(st, T, q, st.muBmin, st.muBmax, p);
        if (ok && std::isfinite(last) && std::fabs(p.muB - last) > st.maxStep) {
          char buf[200];
          std::snprintf(buf, sizeof buf,
                        "T=%.3f muQ=%.2f: jump at muB=%.3f is %.1f MeV from the "
                        "neighbour (%.3f); treated as the end of the fold",
                        T, q, p.muB, p.muB - last, last);
          notes.push_back(buf);
          ok = false;
        }
      }
      if (ok) {
        pts.push_back(p);
        last = p.muB;
        misses = 0;
      } else if (++misses >= 3) {
        break;
      }
    }
  }
  std::sort(pts.begin(), pts.end(),
            [](const Point &x, const Point &y) { return x.muQ < y.muQ; });
}

} // namespace

int main(int argc, char **argv) {
  Settings st;
  std::string outPath, diagPath;
  bool fitted = false;
  double pos[4];
  int npos = 0;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "-o") && i + 1 < argc)
      outPath = argv[++i];
    else if (!std::strcmp(argv[i], "--diag") && i + 1 < argc)
      diagPath = argv[++i];
    else if (!std::strcmp(argv[i], "--jobs") && i + 1 < argc)
      st.jobs = std::atoi(argv[++i]);
    else if (!std::strcmp(argv[i], "--muB-min") && i + 1 < argc)
      st.muBmin = std::atof(argv[++i]);
    else if (!std::strcmp(argv[i], "--muB-max") && i + 1 < argc)
      st.muBmax = std::atof(argv[++i]);
    else if (!std::strcmp(argv[i], "--muQ-max") && i + 1 < argc)
      st.muQmax = std::atof(argv[++i]);
    else if (!std::strcmp(argv[i], "--fitted"))
      fitted = true;
    else if (npos < 4)
      pos[npos++] = std::atof(argv[i]);
    else {
      std::fprintf(stderr, "unexpected argument: %s\n", argv[i]);
      return 1;
    }
  }
  if (npos >= 3) {
    st.Tmin = pos[0];
    st.Tmax = pos[1];
    st.dT = pos[2];
  } else if (npos == 1 || npos == 2) {
    std::fprintf(stderr, "give Tmin Tmax dT together\n");
    return 1;
  }
  if (npos >= 4)
    st.dmuQ = pos[3];
  if (!(st.dT > 0.0) || !(st.dmuQ > 0.0) || st.Tmin > st.Tmax) {
    std::fprintf(stderr, "invalid grid\n");
    return 1;
  }
  if (st.jobs <= 0)
    st.jobs = std::max(1u, std::thread::hardware_concurrency());

  if (fitted)
    ECP::setCrossMode(ECP::CrossMode::Fitted);
  /* HRG seam off: the Maxwell location and the density jump do not depend on
   * it (see the header), and without it no Thermal-FIST solve is needed. */
  ECP::initialize("", "", 1.0, 3.42, /*useHRG=*/false);

  std::vector<double> Ts;
  for (int k = 0;; ++k) {
    const double T = st.Tmin + k * st.dT;
    if (T > st.Tmax + 1e-9)
      break;
    Ts.push_back(T);
  }
  std::vector<std::vector<Point>> rows(Ts.size());
  std::vector<std::vector<std::string>> notes(Ts.size());
  std::atomic<std::size_t> next{0}, done{0};
  std::mutex log;
  auto worker = [&]() {
    for (;;) {
      const std::size_t k = next.fetch_add(1);
      if (k >= Ts.size())
        return;
      slice(st, Ts[k], rows[k], notes[k]);
      const std::size_t d = ++done;
      std::lock_guard<std::mutex> lk(log);
      std::fprintf(stderr, "\r%zu/%zu slices done (T = %.2f: %zu points)   ", d,
                   Ts.size(), Ts[k], rows[k].size());
      std::fflush(stderr);
    }
  };
  std::vector<std::thread> pool;
  for (int i = 0; i < st.jobs; ++i)
    pool.emplace_back(worker);
  for (auto &t : pool)
    t.join();
  std::fprintf(stderr, "\n");

  FILE *f = outPath.empty() ? stdout : std::fopen(outPath.c_str(), "w");
  if (!f) {
    std::fprintf(stderr, "cannot open %s\n", outPath.c_str());
    return 1;
  }
  std::size_t n = 0;
  double maxdP = 0.0;
  for (const auto &r : rows)
    for (const auto &p : r) {
      ++n;
      maxdP = std::max(maxdP, p.dP);
    }
  std::fprintf(f, "# T  muB  muS  muQ  dnB\n");
  std::fprintf(f, "# First-order (Maxwell) surface of the Entropy Contour Param "
                  "EoS (eos = 4), mu_S = 0; generated by gui/first_order_surface\n");
  std::fprintf(f, "# grid: T = %g..%g step %g MeV, mu_Q step %g MeV; mu_B solved "
                  "to %g MeV by bisection of the equal-pressure branch switch "
                  "(HRG seam off: it cancels); cross mode: %s\n",
               st.Tmin, st.Tmax, st.dT, st.dmuQ, st.tol,
               fitted ? "fitted (6 independent)" : "isospin-derived");
  std::fprintf(f, "# columns: T [MeV], mu_B [MeV], mu_S [MeV], mu_Q [MeV], "
                  "dnB = n_B(dense) - n_B(dilute) [fm^-3]; %zu points, max "
                  "|P_dense - P_dilute|/P = %.1e\n",
               n, maxdP);
  for (const auto &r : rows)
    for (const auto &p : r)
      std::fprintf(f, "%14.6e %14.6e %14.6e %14.6e %14.6e\n", p.T, p.muB, 0.0,
                   p.muQ, p.dnB);
  if (f != stdout)
    std::fclose(f);

  for (const auto &ns : notes)
    for (const auto &s : ns)
      std::fprintf(stderr, "note: %s\n", s.c_str());
  if (!diagPath.empty()) {
    FILE *g = std::fopen(diagPath.c_str(), "w");
    if (g) {
      std::fprintf(g, "# T muQ muB dnB nB_dilute nB_dense dP/P\n");
      for (const auto &r : rows)
        for (const auto &p : r)
          std::fprintf(g, "%.4f %.4f %.7f %.6e %.6e %.6e %.3e\n", p.T, p.muQ,
                       p.muB, p.dnB, p.nBlo, p.nBhi, p.dP);
      std::fclose(g);
    }
  }
  /* diagnostics: the points where the two phases' pressures match worst */
  std::vector<const Point *> worst;
  for (const auto &r : rows)
    for (const auto &p : r)
      worst.push_back(&p);
  std::sort(worst.begin(), worst.end(),
            [](const Point *x, const Point *y) { return x->dP > y->dP; });
  std::fprintf(stderr, "worst |dP|/P (T, muQ, muB, dnB, nB_dilute, nB_dense):\n");
  for (std::size_t i = 0; i < worst.size() && i < 12; ++i)
    std::fprintf(stderr, "  %.2e  %8.3f %8.2f %10.4f %9.5f %9.5f %9.5f\n",
                 worst[i]->dP, worst[i]->T, worst[i]->muQ, worst[i]->muB,
                 worst[i]->dnB, worst[i]->nBlo, worst[i]->nBhi);
  std::size_t nbad = 0;
  for (const Point *p : worst)
    if (p->dP > 1e-6)
      ++nbad;
  std::fprintf(stderr, "%zu points with |dP|/P > 1e-6\n", nbad);
  std::fprintf(stderr, "%zu points written%s%s\n", n,
               outPath.empty() ? "" : " to ", outPath.c_str());
  ECP::cleanup();
  return 0;
}
