#include "../include/QCDTherm.hpp"
#include "../include/InterpolatedEoS.hpp"
#include "../include/LatticeQCD.hpp"
#include "../include/EntrCont.hpp"
#include "../include/EntrContParam.hpp"
#include <limits>
namespace QCD {

// Current EoS selection (0 = free QGP, 1 = lattice QCD,
//                       2 = Interpolated Table, 3 = Entropy Contour,
//                       4 = Entropy Contour Param)
static int currentEoS = 0;

// ── EoS 4 contour cache ───────────────────────────────────────────────────
// evalContour(muB, muQ) iterates over N_T0 = 1000 grid points. During a
// single Newton step the solver queries BarDens, QCDcharge, and sQCD for the
// same (muB, muQ) — once for the residual and once per Jacobian column.
// Caching the last ContourValues avoids rebuilding the 1000-point contour
// 3× per (muB, muQ) pair and ensures all three functions use the same
// internally-consistent contour (prevents NaN mismatches where one call
// finds an inversion branch and another does not).
static double s_cache4_muB = std::numeric_limits<double>::quiet_NaN();
static double s_cache4_muQ = std::numeric_limits<double>::quiet_NaN();
static EntropyContoursParam::ContourValues s_cache4_cv;

static const EntropyContoursParam::ContourValues &getContour4(double muB, double muQ) {
  if (muB != s_cache4_muB || muQ != s_cache4_muQ) {
    s_cache4_cv  = EntropyContoursParam::evalContour(muB, muQ);
    s_cache4_muB = muB;
    s_cache4_muQ = muQ;
  }
  return s_cache4_cv;
}

void setEoS(int eos, const std::string &dataPath, int nf, int interpType) {
  currentEoS = eos;
  if (eos == 1) {
    bool includeCharm = (nf == 4);
    LatticeQCD::initialize(dataPath + "/LatticeEoS/threeflavors/", includeCharm, interpType);
  } else if (eos == 2) {
    // For Interpolated EoS, load the standard table file
    if (!InterpolatedEoS::isLoaded()) {
      InterpolatedEoS::loadTable(dataPath + "/EoS_Table.txt");
    }
  } else if (eos == 3) {
    EntropyContours::initialize(dataPath + "/EntroContourEoS/chis", dataPath + "/EntroContourEoS/HRG/list-PDG2020.dat", 1.0, 3.42, true);
  } else if (eos == 4) {
    EntropyContoursParam::initialize(dataPath + "/EntroContourEoS/chis", dataPath + "/EntroContourEoS/HRG/list-PDG2020.dat", 1.0, 3.42, true);
  }
}

int getEoS() { return currentEoS; }

void cleanup() {
  if (currentEoS == 1) {
    LatticeQCD::cleanup();
  }
  if (currentEoS == 3) {
    EntropyContours::cleanup();
  }
  if (currentEoS == 4) {
    EntropyContoursParam::cleanup();
  }

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
  } else if (currentEoS == 3) {
    return EntropyContours::BarDens(muB, muQ, T);
  } else if (currentEoS == 4) {
    return EntropyContoursParam::BarDens(muB, muQ, T, getContour4(muB, muQ));
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
  } else if (currentEoS == 3) {
    return EntropyContours::QCDcharge(muB, muQ, T);
  } else if (currentEoS == 4) {
    return EntropyContoursParam::QCDcharge(muB, muQ, T, getContour4(muB, muQ));
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
  } else if (currentEoS == 3) {
    return EntropyContours::sQCD(muB, muQ, T);
  } else if (currentEoS == 4) {
    return EntropyContoursParam::sQCD(muB, muQ, T, getContour4(muB, muQ));
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

} // namespace QCD
