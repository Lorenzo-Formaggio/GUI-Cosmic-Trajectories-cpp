#include "EntrContParam.hpp"

#include "HRG.hpp"
#include "LatticeDerivatives.hpp"

#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>

namespace EntropyContoursParam {

static const double NaN = std::numeric_limits<double>::quiet_NaN();

// ============================================================================
// Lattice input at mu = 0: the closed-form Wuppertal-Budapest fits.
//
// Transcribed from scontours::LatticeInput (s_contours_c-dev4-tristan,
// src/LatticeInput.cpp): best-fit parameters of the chi^2 minimization against
// continuum-extrapolated Wuppertal-Budapest data [Borsanyi et al., PRL 125
// (2020) 052001; PRD 105 (2022) 114504], full double precision, identical to
// 4D_scontours_Parameterization_v2.3_CP_Finder.nb.
//
// The T-derivatives are the closed forms generated symbolically upstream
// (LatticeDerivatives.hpp, copied verbatim), so nothing here differentiates
// numerically -- exactly as in the reference.
//
// Everything downstream of this input -- the contour algebra, the anchor solve,
// the QvdW-HRG seam -- lives in ContourEoSCore.hpp, shared with EntrCont.
// ============================================================================
namespace {

constexpr double kMassProton = 938.0; // MeV
constexpr double kMassPion = 140.0;   // MeV
constexpr double kMassKaon = 495.0;   // MeV
constexpr double kTScale = 200.0;     // MeV, Michael temperature normalization

/* f(T) = sign * michael(d0, d1, d2, d3, d5; mass, Tscale) */
struct MichaelFit {
  double mass, sign, d0, d1, d2, d3, d5;
  double d(double T, int order) const {
    return sign * scontours::generated::michael<double>(order, d0, d1, d2, d3,
                                                        d5, mass, kTScale, T);
  }
};

/* s/T^3 = a tanh((T - T0)/dT) + b */
struct TanhFit {
  double a, b, T0, dT;
  double d(double T, int order) const {
    return scontours::generated::entropyTanh<double>(order, a, b, T0, dT, T);
  }
};

/* p0 f_SN(T; mu1, p2, alpha1) + p4 f_SN(T; p5, p6, p7) */
struct DoubleSkewNormalFit {
  double p0, p2, p4, p5, p6, p7, mu1, alpha1;
  double d(double T, int order) const {
    return scontours::generated::doubleSkewNormal<double>(order, p0, p2, p4, p5,
                                                          p6, p7, mu1, alpha1,
                                                          T);
  }
};

/* s/T^3 = a tanh((T - T0)/dT) + b */
const TanhFit kEntropy{5.656075363106222, 6.430259990479002,
                       163.68142466391484, 43.35160324853628};
/* chi2B: proton-mass Boltzmann term */
const MichaelFit kChi2B{kMassProton,        +1.0,
                        3.6076255940379123, 0.7502148791172487,
                        21.155345203652914, 0.3305183902184058,
                        0.7585835763532186};
/* chi2Q: pion-mass Boltzmann term */
const MichaelFit kChi2Q{kMassPion,          +1.0,
                        0.7164987887980752, 0.7570047606327102,
                        6.015382470369457,  0.629575552188607,
                        0.006687997932345464};
/* chi2S: kaon-mass Boltzmann term */
const MichaelFit kChi2S{kMassKaon,          +1.0,
                        0.8322621087687887, 0.8295810817249416,
                        8.07331902668102,   0.8595952510551099,
                        0.6186329300539317};
/* chi11BS: negative at all T (B-S anticorrelation in the HRG phase) */
const MichaelFit kChi11BSfit{kMassPion,             -1.0,
                             0.0003281619741080934, 0.8574332020728931,
                             9.692874886279938,     0.25674721454206617,
                             0.46193348422190794};
/* chi11QS: kaon-dominated; d5 held fixed in the fit */
const MichaelFit kChi11QSfit{kMassKaon,          +1.0,
                             0.3350268638607059, 0.867722746742251,
                             7.604312446947438,  0.2808457840677243,
                             0.414368};
/* chi11BQ: shoulder (fixed location/skewness) + crossover peak */
const DoubleSkewNormalFit kChi11BQfit{0.35975712110963015,
                                      21.994844290144993,
                                      1.8406766329515367,
                                      150.83855166636303,
                                      50.28303873202961,
                                      3.1967362601870666,
                                      140.0,
                                      0.144163};

/** The fitted lattice input, as a ContourEoS::LatticeSource. */
class FittedLattice : public ContourEoS::LatticeSource {
public:
  explicit FittedLattice(CrossMode mode) : mode_(mode) {}

  void at(double T, ContourEoS::LatticeAt &L) const override {
    double c2B[3], c2Q[3], c2S[3], cQS[3], cBS[3], cBQ[3];
    for (int o = 0; o < 3; ++o) {
      c2B[o] = kChi2B.d(T, o);
      c2Q[o] = kChi2Q.d(T, o);
      c2S[o] = kChi2S.d(T, o);
      cQS[o] = kChi11QSfit.d(T, o); /* the fitted input in both modes */
    }
    if (mode_ == CrossMode::Fitted) {
      for (int o = 0; o < 3; ++o) {
        cBS[o] = kChi11BSfit.d(T, o);
        cBQ[o] = kChi11BQfit.d(T, o);
      }
    } else {
      /* isospin relations [Eqs. (17)-(18)]:
       *   chi11BS = 2 chi11QS - chi2S,  chi11BQ = (chi2B + chi11BS)/2 */
      for (int o = 0; o < 3; ++o) {
        cBS[o] = 2.0 * cQS[o] - c2S[o];
        cBQ[o] = 0.5 * (c2B[o] + cBS[o]);
      }
    }
    for (int o = 0; o < 3; ++o) {
      L.chi[0][o] = c2B[o];
      L.chi[1][o] = c2Q[o];
      L.chi[2][o] = c2S[o];
      L.chi[3][o] = cBQ[o];
      L.chi[4][o] = cBS[o];
      L.chi[5][o] = cQS[o];
    }
    /* dimensional entropy density s(T) = T^3 (s/T^3)(T) and its T-derivatives
     * by the Leibniz rule (scontours::EntropyDensity). */
    const double f0 = kEntropy.d(T, 0);
    const double f1 = kEntropy.d(T, 1);
    const double f2 = kEntropy.d(T, 2);
    L.s0[0] = T * T * T * f0;
    L.s0[1] = 3.0 * T * T * f0 + T * T * T * f1;
    L.s0[2] = 6.0 * T * f0 + 6.0 * T * T * f1 + T * T * T * f2;
  }

  double entropyDensity(double T) const override {
    return T * T * T * kEntropy.d(T, 0);
  }

private:
  CrossMode mode_;
};

bool g_initialized = false;
bool g_useHRG = true;
CrossMode g_crossMode = CrossMode::IsospinDerived;
std::unique_ptr<FittedLattice> g_lattice;
std::unique_ptr<ContourEoS::Engine> g_engine;

/* Anchor grid range. The lower end is NOT arbitrary: the symbolically generated
 * closed-form T-derivatives (LatticeDerivatives.hpp) group their exponentials
 * and powers in a way that overflows double below ~41 MeV -- the chi2B fit
 * (proton mass) returns inf/NaN there, even though the value itself is ~1e-8.
 * That is harmless upstream, where nothing is ever evaluated below Tlow, but
 * here the grid would sample it. Every anchor the EoS can reach satisfies
 * T0 >= T_phys >= Tlow = 80 MeV, so starting at 60 MeV leaves a wide margin on
 * both sides; the finiteness guard in the inversion is the backstop. */
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
 * the seam model itself (HRG::eval) is the thermodynamics. Preserved from the
 * previous implementation so callers keep seeing NaN there rather than an
 * extrapolation. */
bool reachableT(double T) {
  return !(g_useHRG && T < g_engine->options().Tlow);
}

/* Shared preamble of every accessor. */
bool lookup(double T, const ContourValues &c, ContourEoS::AnchorResult &r) {
  if (!g_initialized)
    throw std::runtime_error("EntropyContoursParam not initialized");
  if (!reachableT(T))
    return false;
  return g_engine->invert(c, T, r);
}

} // namespace

// ============================================================================
// Initialization.
// ============================================================================
void setCrossMode(CrossMode mode) { g_crossMode = mode; }
CrossMode crossMode() { return g_crossMode; }

double referenceTemperature() {
  return g_engine ? g_engine->options().Tlow : engineOptions().Tlow;
}

void initialize(const std::string & /*chisDir*/,
                const std::string &pdgListPath, double b_meson_fm3,
                double b_baryon_fm3, bool useHRG) {
  if (g_initialized)
    return;

  g_lattice.reset(new FittedLattice(g_crossMode));
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
  /* HRG state is shared with EntrCont; we don't tear it down here. */
  g_initialized = false;
}

bool isInitialized() { return g_initialized; }

// ============================================================================
// Contour evaluation.
// ============================================================================
ContourValues evalContour(double muB, double muQ, double muS) {
  if (!g_initialized)
    throw std::runtime_error("EntropyContoursParam not initialized");
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

} // namespace EntropyContoursParam
