#include "../include/QCDTherm.hpp"
#include "../include/InterpolatedEoS.hpp"
#include "../include/LatticeQCD.hpp"
#include "../include/EntrCont.hpp"
#include "../include/EntrContParam.hpp"
#include "../include/GibbsMixedPhase.hpp"
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>
namespace QCD {

// Current EoS selection (0 = free QGP, 1 = lattice QCD,
//                       2 = Interpolated Table, 3 = Entropy Contour,
//                       4 = Entropy Contour Param,
//                       5 = Entropy Contour with the Gibbs mixed phase,
//                       6 = Entropy Contour Param with the Gibbs mixed phase)
static int currentEoS = 0;

// The Entropy Contour family: two lattice inputs, each either homogeneous
// (3, 4: one phase at a time, the Maxwell picture) or with the Gibbs mixed
// phase in the first-order region (5, 6; see GibbsMixedPhase.hpp).
static bool tabulatedContour(int eos) { return eos == 3; }
static bool parametrizedContour(int eos) { return eos == 4; }
static bool gibbsContour(int eos) { return eos == 5 || eos == 6; }

// The Gibbs layer over the selected lattice model (eos 5: tabulated, 6: param).
static std::unique_ptr<GibbsPhase::MixedPhaseEoS> s_gibbs;

// ── Contour caches (EoS 3/5 and 4/6) ─────────────────────────────────────
// evalContour(muB, muQ) iterates over N_T0 = 1000 grid points. During a
// single Newton step the solver queries BarDens, QCDcharge, and sQCD for the
// same (muB, muQ) — once for the residual and once per Jacobian column.
// Caching the last ContourValues avoids rebuilding the 1000-point contour
// 3× per (muB, muQ) pair and ensures all three functions use the same
// internally-consistent contour (prevents NaN mismatches where one call
// finds an inversion branch and another does not).
//
// The caches are thread-local: the simulation worker's Metropolis fallback
// evaluates the EoS from several threads at once (std::async chains), and a
// shared cache would race. A generation counter bumped by setEoS()/cleanup()
// invalidates every thread's cache after an EoS switch -- the seam model is
// part of the contour, and the engine a stale contour points to may be gone.
static std::atomic<unsigned> s_cacheGeneration{0};

struct ContourCache {
  double muB = std::numeric_limits<double>::quiet_NaN();
  double muQ = std::numeric_limits<double>::quiet_NaN();
  unsigned gen = 0;
  ContourEoS::Contour cv; // EntropyContours::ContourValues == EntropyContoursParam::ContourValues
};
static thread_local ContourCache s_cache3; // EoS 3 / 5
static thread_local ContourCache s_cache4; // EoS 4 / 6

static const EntropyContours::ContourValues &getContour3(double muB, double muQ) {
  const unsigned gen = s_cacheGeneration.load(std::memory_order_acquire);
  if (s_cache3.gen != gen || muB != s_cache3.muB || muQ != s_cache3.muQ) {
    s_cache3.cv  = EntropyContours::evalContour(muB, muQ);
    s_cache3.muB = muB;
    s_cache3.muQ = muQ;
    s_cache3.gen = gen;
  }
  return s_cache3.cv;
}

static const EntropyContoursParam::ContourValues &getContour4(double muB, double muQ) {
  const unsigned gen = s_cacheGeneration.load(std::memory_order_acquire);
  if (s_cache4.gen != gen || muB != s_cache4.muB || muQ != s_cache4.muQ) {
    s_cache4.cv  = EntropyContoursParam::evalContour(muB, muQ);
    s_cache4.muB = muB;
    s_cache4.muQ = muQ;
    s_cache4.gen = gen;
  }
  return s_cache4.cv;
}

struct GibbsCache {
  double T = std::numeric_limits<double>::quiet_NaN();
  double x = std::numeric_limits<double>::quiet_NaN();
  double muQ = std::numeric_limits<double>::quiet_NaN();
  unsigned gen = 0;
  GibbsPhase::Result r;
};
static thread_local GibbsCache s_cacheG; // EoS 5 / 6

// One Gibbs evaluation serves nB, nQ, s and P of the same point.
static const GibbsPhase::Result &gibbsAt(double x, double muQ, double T) {
  const unsigned gen = s_cacheGeneration.load(std::memory_order_acquire);
  if (s_cacheG.gen != gen || T != s_cacheG.T || x != s_cacheG.x || muQ != s_cacheG.muQ) {
    s_cacheG.r = s_gibbs->evaluate(T, x, muQ);
    s_cacheG.T = T;
    s_cacheG.x = x;
    s_cacheG.muQ = muQ;
    s_cacheG.gen = gen;
  }
  return s_cacheG.r;
}

static void resetContourCaches() {
  s_cacheGeneration.fetch_add(1, std::memory_order_acq_rel);
}

// Build the Gibbs layer over the lattice model that backs eos 5 or 6.
static void makeGibbs(int eos) {
  GibbsPhase::Model m;
  if (eos == 5) {
    m.build = [](double muB, double muQ, double muS, bool useHRG) {
      return EntropyContours::evalContour(muB, muQ, muS, useHRG);
    };
    m.evaluate = [](double T, const ContourEoS::Contour &c, GibbsPhase::State &st) {
      st.nB = EntropyContours::BarDens(0.0, 0.0, T, c);
      st.nQ = EntropyContours::QCDcharge(0.0, 0.0, T, c);
      st.nS = EntropyContours::StrDens(0.0, 0.0, T, c);
      st.s = EntropyContours::sQCD(0.0, 0.0, T, c);
      st.P = EntropyContours::pQCD(0.0, 0.0, T, c);
      st.valid = std::isfinite(st.nB) && std::isfinite(st.nQ) && std::isfinite(st.nS) &&
                 std::isfinite(st.s) && std::isfinite(st.P);
      return st.valid;
    };
    m.Tlow = EntropyContours::referenceTemperature();
  } else {
    m.build = [](double muB, double muQ, double muS, bool useHRG) {
      return EntropyContoursParam::evalContour(muB, muQ, muS, useHRG);
    };
    m.evaluate = [](double T, const ContourEoS::Contour &c, GibbsPhase::State &st) {
      st.nB = EntropyContoursParam::BarDens(0.0, 0.0, T, c);
      st.nQ = EntropyContoursParam::QCDcharge(0.0, 0.0, T, c);
      st.nS = EntropyContoursParam::StrDens(0.0, 0.0, T, c);
      st.s = EntropyContoursParam::sQCD(0.0, 0.0, T, c);
      st.P = EntropyContoursParam::pQCD(0.0, 0.0, T, c);
      st.valid = std::isfinite(st.nB) && std::isfinite(st.nQ) && std::isfinite(st.nS) &&
                 std::isfinite(st.s) && std::isfinite(st.P);
      return st.valid;
    };
    m.Tlow = EntropyContoursParam::referenceTemperature();
  }
  s_gibbs.reset(new GibbsPhase::MixedPhaseEoS(m));
}

void setEoS(int eos, const std::string &dataPath, int nf, int interpType) {
  currentEoS = eos;
  resetContourCaches();
  if (eos == 1) {
    bool includeCharm = (nf == 4);
    LatticeQCD::initialize(dataPath + "/LatticeEoS/threeflavors/", includeCharm, interpType);
  } else if (eos == 2) {
    // For Interpolated EoS, load the standard table file
    if (!InterpolatedEoS::isLoaded()) {
      InterpolatedEoS::loadTable(dataPath + "/EoS_Table.txt");
    }
  } else if (eos == 3 || eos == 5) {
    EntropyContours::initialize(dataPath + "/EntroContourEoS/chis", dataPath + "/EntroContourEoS/HRG/list-PDG2020.dat", 1.0, 3.42, true);
  } else if (eos == 4 || eos == 6) {
    EntropyContoursParam::initialize(dataPath + "/EntroContourEoS/chis", dataPath + "/EntroContourEoS/HRG/list-PDG2020.dat", 1.0, 3.42, true);
  }
  if (gibbsContour(eos))
    makeGibbs(eos);
  else
    s_gibbs.reset();
}

bool isGibbs(int eos) { return gibbsContour(eos); }

double physicalMuB(double x, double muQ, double T) {
  if (!gibbsContour(currentEoS) || !s_gibbs)
    return x;
  return s_gibbs->physicalMuB(T, x, muQ);
}

double phaseFraction(double x, double muQ, double T) {
  if (!gibbsContour(currentEoS) || !s_gibbs)
    return std::numeric_limits<double>::quiet_NaN();
  return s_gibbs->phaseFraction(T, x, muQ);
}

double coordinateFromMuB(double muB, double muQ, double T) {
  if (!gibbsContour(currentEoS) || !s_gibbs)
    return muB;
  return s_gibbs->coordinate(T, muB, muQ);
}

int getEoS() { return currentEoS; }

void cleanup() {
  if (currentEoS == 1) {
    LatticeQCD::cleanup();
  }
  s_gibbs.reset();
  if (currentEoS == 3 || currentEoS == 5) {
    EntropyContours::cleanup();
  }
  if (currentEoS == 4 || currentEoS == 6) {
    EntropyContoursParam::cleanup();
  }
  resetContourCaches();

  if (currentEoS != 2) {
    // If we are running a simulation that does NOT use the Interpolated EoS,
    // we should free its memory to release RAM.
    InterpolatedEoS::cleanup();
  }
}

// Baryon density (3 or 4 flavors based on nf)
double BarDens(double muB, double muQ, double T, int nf) {
  if (currentEoS == 1) {
    return LatticeQCD::BarDens(muB, muQ, T);
  } else if (currentEoS == 2) {
    auto val = InterpolatedEoS::evaluate(T, muB, muQ);
    return val.nB;
  } else if (tabulatedContour(currentEoS)) {
    return EntropyContours::BarDens(muB, muQ, T, getContour3(muB, muQ));
  } else if (parametrizedContour(currentEoS)) {
    return EntropyContoursParam::BarDens(muB, muQ, T, getContour4(muB, muQ));
  } else if (gibbsContour(currentEoS)) {
    return gibbsAt(muB, muQ, T).state.nB; /* muB is the stretched coordinate x */
  }

  // Free QGP
  double result = 1.0 / 3.0 *
                  (jelf::nNet(muB / 3 + 2 * muQ / 3, T, mu, gq) +
                   jelf::nNet(muB / 3 - muQ / 3, T, md, gq));
  if (nf >= 3) {
    result += 1.0 / 3.0 * jelf::nNet(muB / 3 - muQ / 3, T, ms, gq);
  }
  if (nf == 4) {
    result += 1.0 / 3.0 * jelf::nNet(muB / 3 + 2 * muQ / 3, T, mc, gq);
  }
  return result;
}

double QCDcharge(double muB, double muQ, double T, int nf) {
  if (currentEoS == 1) {
    return LatticeQCD::QCDcharge(muB, muQ, T);
  } else if (currentEoS == 2) {
    auto val = InterpolatedEoS::evaluate(T, muB, muQ);
    return val.nQ;
  } else if (tabulatedContour(currentEoS)) {
    return EntropyContours::QCDcharge(muB, muQ, T, getContour3(muB, muQ));
  } else if (parametrizedContour(currentEoS)) {
    return EntropyContoursParam::QCDcharge(muB, muQ, T, getContour4(muB, muQ));
  } else if (gibbsContour(currentEoS)) {
    return gibbsAt(muB, muQ, T).state.nQ; /* muB is the stretched coordinate x */
  }

  // Free QGP
  double result = 2.0 / 3.0 * jelf::nNet(muB / 3 + 2 * muQ / 3, T, mu, gq) -
                  1.0 / 3.0 * jelf::nNet(muB / 3 - muQ / 3, T, md, gq);
  if (nf >= 3) {
    result -= 1.0 / 3.0 * jelf::nNet(muB / 3 - muQ / 3, T, ms, gq);
  }
  if (nf == 4) {
    result += 2.0 / 3.0 * jelf::nNet(muB / 3 + 2 * muQ / 3, T, mc, gq);
  }
  return result;
}

double sQCD(double muB, double muQ, double T, int nf) {
  if (currentEoS == 1) {
    return LatticeQCD::sQCD(muB, muQ, T);
  } else if (currentEoS == 2) {
    auto val = InterpolatedEoS::evaluate(T, muB, muQ);
    return val.s;
  } else if (tabulatedContour(currentEoS)) {
    return EntropyContours::sQCD(muB, muQ, T, getContour3(muB, muQ));
  } else if (parametrizedContour(currentEoS)) {
    return EntropyContoursParam::sQCD(muB, muQ, T, getContour4(muB, muQ));
  } else if (gibbsContour(currentEoS)) {
    return gibbsAt(muB, muQ, T).state.s; /* muB is the stretched coordinate x */
  }

  // Free QGP
  double result = jelf::sTot(muB / 3 + 2 * muQ / 3, T, mu, gq) +
                  jelf::sTot(muB / 3 - muQ / 3, T, md, gq) +
                  jelb::sb0(0, T, 0, ggluon);
  if (nf >= 3) {
    result += jelf::sTot(muB / 3 - muQ / 3, T, ms, gq);
  }
  if (nf == 4) {
    result += jelf::sTot(muB / 3 + 2 * muQ / 3, T, mc, gq);
  }
  return result;
}

double pQCD(double muB, double muQ, double T, int nf) {
  if (currentEoS == 1) {
    return LatticeQCD::pQCD(muB, muQ, T);
  } else if (currentEoS == 2) {
    // Interpolated EoS does not store pressure directly;
    // use the thermodynamic identity P = T*s - e. But we don't have e either.
    // Fall back to computing P from the relation: dP/dmu_i = n_i.
    // Since the table only provides s, nB, nQ, we approximate using the
    // free QGP formula for now.
    // TODO: extend the interpolated table to include pressure.
    double result = jelf::PTot(muB / 3 + 2 * muQ / 3, T, mu, gq) +
                    jelf::PTot(muB / 3 - muQ / 3, T, md, gq) +
                    jelb::PTot(0, T, 0, ggluon);
    if (nf >= 3) {
      result += jelf::PTot(muB / 3 - muQ / 3, T, ms, gq);
    }
    if (nf == 4) {
      result += jelf::PTot(muB / 3 + 2 * muQ / 3, T, mc, gq);
    }
    return result;
  } else if (tabulatedContour(currentEoS)) {
    return EntropyContours::pQCD(muB, muQ, T, getContour3(muB, muQ));
  } else if (parametrizedContour(currentEoS)) {
    return EntropyContoursParam::pQCD(muB, muQ, T, getContour4(muB, muQ));
  } else if (gibbsContour(currentEoS)) {
    return gibbsAt(muB, muQ, T).state.P; /* muB is the stretched coordinate x */
  }

  // Free QGP
  double result = jelf::PTot(muB / 3 + 2 * muQ / 3, T, mu, gq) +
                  jelf::PTot(muB / 3 - muQ / 3, T, md, gq) +
                  jelb::PTot(0, T, 0, ggluon);
  if (nf >= 3) {
    result += jelf::PTot(muB / 3 - muQ / 3, T, ms, gq);
  }
  if (nf == 4) {
    result += jelf::PTot(muB / 3 + 2 * muQ / 3, T, mc, gq);
  }
  return result;
}

double eQCD(double muB, double muQ, double T, int nf) {
  // Use the thermodynamic identity:
  //   e = T*s - P + muB*nB + muQ*nQ   (with muS = 0)
  double s  = sQCD(muB, muQ, T, nf);
  double P  = pQCD(muB, muQ, T, nf);
  double nB = BarDens(muB, muQ, T, nf);
  double nQ = QCDcharge(muB, muQ, T, nf);

  return T * s - P + muB * nB + muQ * nQ;
}

} // namespace QCD
