#include "GibbsMixedPhase.hpp"

#include <algorithm>
#include <cmath>

namespace GibbsPhase {

namespace {
const double NaN = std::numeric_limits<double>::quiet_NaN();
const double kHbarc = 197.3269804;               /* MeV fm */
const double kHbarc3 = kHbarc * kHbarc * kHbarc; /* MeV^3 -> fm^-3 */
} // namespace

MixedPhaseEoS::MixedPhaseEoS(Model model) : model_(std::move(model)) {}

MixedPhaseEoS::MixedPhaseEoS(Model model, const Options &opts)
    : model_(std::move(model)), opts_(opts) {}

State MixedPhaseEoS::homogeneous(double T, double muB, double muQ) const {
  State st;
  if (!(T >= model_.Tlow))
    return st;
  const ContourEoS::Contour c = model_.build(muB, muQ, 0.0, true);
  if (!model_.evaluate(T, c, st))
    st.valid = false;
  return st;
}

/* Bare-contour evaluation (seam off): used by the coexistence search, and for
 * the dense phase, whose seam terms are those of the dilute phase (same mu). */
bool MixedPhaseEoS::bareState(double T, double muB, double muQ, State &st) const {
  const ContourEoS::Contour c = model_.build(muB, muQ, 0.0, false);
  if (!model_.evaluate(T, c, st) || !(st.s > 0.0) || !std::isfinite(st.nB))
    return false;
  return true;
}

/* Top of the first-order region: the highest T (1 MeV resolution, +1 MeV
 * margin) at which a coexistence exists for some mu_Q. The critical
 * temperature of the contour EoS is maximal close to the pure-mu_B direction
 * and falls off on both sides, so a handful of mu_Q values around it suffice.
 * Called with mutex_ held. */
double MixedPhaseEoS::tcMaxLocked() const {
  if (haveTcMax_)
    return tcMax_;
  const double muQs[] = {-100.0, -50.0, -20.0, 0.0, 20.0, 50.0, 100.0};
  tcMax_ = model_.Tlow; /* no first-order region inside the domain */
  for (double T = 130.0; T >= model_.Tlow; T -= 1.0) {
    bool any = false;
    for (double q : muQs) {
      double mb;
      if (locate(T, q, opts_.muBmin, opts_.muBmax, mb)) {
        any = true;
        break;
      }
    }
    if (any) {
      tcMax_ = T + 1.0;
      break;
    }
  }
  haveTcMax_ = true;
  return tcMax_;
}

double MixedPhaseEoS::criticalTemperatureBound() const {
  const std::lock_guard<std::mutex> lock(mutex_);
  return tcMaxLocked();
}

/* Scan mu_B upward in coarse steps; every upward jump of ln s larger than
 * delta within one step is bisected on the branch identity and accepted only
 * if a discontinuity survives (a steep but continuous rise near a critical
 * point fails the test and the scan continues). Same algorithm as
 * gui/first_order_surface.cpp. */
bool MixedPhaseEoS::locate(double T, double muQ, double from, double to,
                           double &muBcoex) const {
  from = std::max(from, opts_.muBmin);
  to = std::min(to, opts_.muBmax);
  if (!(from < to))
    return false;
  auto bare = [&](double muB, double &lns, double &nB) {
    State st;
    if (!bareState(T, muB, muQ, st))
      return false;
    lns = std::log(st.s);
    nB = st.nB;
    return true;
  };
  double a = from, la = 0.0, na = 0.0;
  bool oka = bare(a, la, na);
  for (double x = a + opts_.coarse; x <= to + 1e-9; x += opts_.coarse) {
    double lx = 0.0, nx = 0.0;
    const bool okx = bare(x, lx, nx);
    if (oka && okx && lx - la > opts_.delta) {
      double lo = a, hi = x, llo = la, lhi = lx, nlo = na, nhi = nx;
      bool clean = true;
      while (hi - lo > opts_.tol) {
        const double m = 0.5 * (lo + hi);
        double lm = 0.0, nm = 0.0;
        if (!bare(m, lm, nm)) {
          clean = false;
          break;
        }
        if (lm - llo < lhi - lm) {
          lo = m;
          llo = lm;
          nlo = nm;
        } else {
          hi = m;
          lhi = lm;
          nhi = nm;
        }
      }
      if (clean && lhi - llo > opts_.minJump &&
          (nhi - nlo) / kHbarc3 > opts_.minDnB) {
        muBcoex = 0.5 * (lo + hi);
        return true;
      }
    }
    a = x;
    la = lx;
    na = nx;
    oka = okx;
  }
  return false;
}

Coexistence MixedPhaseEoS::coexistence(double T, double muQ) const {
  const std::lock_guard<std::mutex> lock(mutex_);
  const auto key = std::make_pair(T, muQ);
  const auto it = memo_.find(key);
  if (it != memo_.end())
    return it->second;

  Coexistence c;
  if (T >= model_.Tlow && T < tcMaxLocked()) {
    double mb = NaN;
    bool found = false;
    /* warm start from the last coexistence found nearby, else a full scan */
    if (haveLast_ && std::fabs(T - lastT_) <= 3.0 &&
        std::fabs(muQ - lastMuQ_) <= 40.0)
      found = locate(T, muQ, lastMuB_ - opts_.window, lastMuB_ + opts_.window, mb);
    if (!found)
      found = locate(T, muQ, opts_.muBmin, opts_.muBmax, mb);
    if (found) {
      /* The seam terms P_ref(mu), n_ref(mu) are the same for both phases
       * (same mu up to eps), so one QvdW solve serves both: the dilute phase
       * with the seam on, the dense one as bare contour + (dilute on - dilute
       * bare). Entropy carries no seam term. */
      c.dilute = homogeneous(T, mb - opts_.eps, muQ);
      State dilBare, denBare;
      const bool ok = c.dilute.valid && bareState(T, mb - opts_.eps, muQ, dilBare) &&
                      bareState(T, mb + opts_.eps, muQ, denBare);
      if (ok) {
        c.dense = denBare;
        c.dense.nB += c.dilute.nB - dilBare.nB;
        c.dense.nQ += c.dilute.nQ - dilBare.nQ;
        c.dense.nS += c.dilute.nS - dilBare.nS;
        c.dense.P += c.dilute.P - dilBare.P;
        c.dense.valid = true;
        c.found = true;
        c.muB = mb;
        haveLast_ = true;
        lastT_ = T;
        lastMuQ_ = muQ;
        lastMuB_ = mb;
      }
    }
  }
  if (memo_.size() >= 4096)
    memo_.clear(); /* bounded memory */
  memo_[key] = c;
  return c;
}

Result MixedPhaseEoS::evaluate(double T, double x, double muQ) const {
  Result r;
  const Coexistence c = coexistence(T, muQ);
  const double W = opts_.width;
  if (!c.found || x <= c.muB) {
    r.muB = x;
    r.state = homogeneous(T, x, muQ);
    return r;
  }
  if (x >= c.muB + W) {
    r.muB = x - W;
    r.state = homogeneous(T, x - W, muQ);
    return r;
  }
  const double lambda = 1.0 - (x - c.muB) / W; /* dilute fraction */
  r.mixed = true;
  r.lambda = lambda;
  r.muB = c.muB;
  const State &d = c.dilute, &e = c.dense;
  r.state.nB = lambda * d.nB + (1.0 - lambda) * e.nB;
  r.state.nQ = lambda * d.nQ + (1.0 - lambda) * e.nQ;
  r.state.nS = lambda * d.nS + (1.0 - lambda) * e.nS;
  r.state.s = lambda * d.s + (1.0 - lambda) * e.s;
  r.state.P = 0.5 * (d.P + e.P); /* equal by construction */
  r.state.valid = true;
  return r;
}

double MixedPhaseEoS::physicalMuB(double T, double x, double muQ) const {
  const Coexistence c = coexistence(T, muQ);
  if (!c.found || x <= c.muB)
    return x;
  if (x >= c.muB + opts_.width)
    return x - opts_.width;
  return c.muB;
}

double MixedPhaseEoS::phaseFraction(double T, double x, double muQ) const {
  const Coexistence c = coexistence(T, muQ);
  if (!c.found || x <= c.muB || x >= c.muB + opts_.width)
    return NaN;
  return 1.0 - (x - c.muB) / opts_.width;
}

double MixedPhaseEoS::coordinate(double T, double muB, double muQ) const {
  const Coexistence c = coexistence(T, muQ);
  if (c.found && muB > c.muB)
    return muB + opts_.width;
  return muB;
}

} // namespace GibbsPhase
