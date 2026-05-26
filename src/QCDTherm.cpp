#include "../include/QCDTherm.hpp"
#include "../include/InterpolatedEoS.hpp"
#include "../include/LatticeQCD.hpp"
#include "../include/EntrCont.hpp"
#include "../include/EntrContParam.hpp"
namespace QCD {

// Current EoS selection (0 = free QGP, 1 = lattice QCD,
//                       2 = Interpolated Table, 3 = Entropy Contour,
//                       4 = Entropy Contour Param)
static int currentEoS = 0;

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
    return EntropyContoursParam::BarDens(muB, muQ, T);
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
    return EntropyContoursParam::QCDcharge(muB, muQ, T);
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
    return EntropyContoursParam::sQCD(muB, muQ, T);
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
