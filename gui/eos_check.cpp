/*
 * eos_check -- cross-check the entropy-contour EoS in this repository against
 * s_contours_c-dev4-tristan's `eos_line`.
 *
 * Usage:
 *   eos_check [muB] [muQ] [muS] [Tmin Tmax dT] [--no-hrg] [--tabulated]
 *             [--fitted] [--data <path>]
 *
 *   --no-hrg     bare contour boundary (no QvdW-HRG seam)
 *   --tabulated  tabulated lattice input (EntrCont, eos 3/5) instead of the
 *                closed-form fits (EntrContParam, eos 4/6)
 *   --fitted     all six cross-susceptibility fits (EntrContParam only)
 *
 * Prints the same observables as `eos_line`, in the same units, along a line of
 * fixed chemical potentials:
 *
 *   ./eos_check 300 30 0 90 200 10
 *
 * is directly comparable to (mu = |(300, 30, 0)| = 301.4963,
 * theta = acos(300/mu) = 5.71059 deg, phi = 0):
 *
 *   ./build/eos_line 300 90 200 10 --dir 5.710593 0 --qvdw
 */

#include "EntrCont.hpp"
#include "EntrContParam.hpp"
#include "HRG.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char **argv) {
  double muB = 300.0, muQ = 0.0, muS = 0.0;
  double Tmin = 90.0, Tmax = 200.0, dT = 10.0;
  bool useHRG = true;
  bool tabulated = false;
  bool fitted = false;
  std::string dataPath = ".";

  double pos[6];
  int npos = 0;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--no-hrg")) {
      useHRG = false;
    } else if (!std::strcmp(argv[i], "--tabulated")) {
      tabulated = true;
    } else if (!std::strcmp(argv[i], "--fitted")) {
      fitted = true;
    } else if (!std::strcmp(argv[i], "--data") && i + 1 < argc) {
      dataPath = argv[++i];
    } else if (npos < 6) {
      pos[npos++] = std::atof(argv[i]);
    }
  }
  if (npos >= 1) muB = pos[0];
  if (npos >= 2) muQ = pos[1];
  if (npos >= 3) muS = pos[2];
  if (npos >= 6) {
    Tmin = pos[3];
    Tmax = pos[4];
    dT = pos[5];
  }

  if (fitted)
    EntropyContoursParam::setCrossMode(EntropyContoursParam::CrossMode::Fitted);

  const std::string chisDir = dataPath + "/EntroContourEoS/chis";
  const std::string listPath = dataPath + "/EntroContourEoS/HRG/list-PDG2020.dat";
  if (tabulated)
    EntropyContours::initialize(chisDir, listPath, 1.0, 3.42, useHRG);
  else
    EntropyContoursParam::initialize(chisDir, listPath, 1.0, 3.42, useHRG);

  const double mu = std::sqrt(muB * muB + muQ * muQ + muS * muS);
  const double deg = 180.0 / M_PI;
  const double theta = (mu > 0.0) ? std::acos(muB / mu) : 0.0;
  const double phi = std::atan2(muS, muQ);

  std::printf("# EoS at muB=%g muQ=%g muS=%g MeV\n", muB, muQ, muS);
  std::printf("# radial mu = %.4f MeV, theta = %.6f deg, phi = %.6f deg\n", mu,
              theta * deg, phi * deg);
  std::printf("# lattice input: %s\n",
              tabulated ? "tabulated splines (EntrCont, eos 3/5)"
                        : "closed-form fits (EntrContParam, eos 4/6)");
  if (!tabulated)
    std::printf("# cross mode: %s\n",
                EntropyContoursParam::crossMode() ==
                        EntropyContoursParam::CrossMode::Fitted
                    ? "fitted (6 independent)"
                    : "isospin-derived");
  std::printf("# low-T boundary: %s (Tlow = %g MeV)\n",
              useHRG ? "QvdW-HRG (Thermal-FIST)" : "mu = 0 contour, p0(Tlow)",
              tabulated ? EntropyContours::referenceTemperature()
                        : EntropyContoursParam::referenceTemperature());
  if (useHRG) {
    const HRG::Params &p = HRG::parameters();
    std::printf("# QvdW: a = %g MeV fm^3, b = %g fm^3, stats = %d, width = %d, "
                "EMM pi = %d, EMM K = %d, %d species\n",
                p.a, p.b, (int)p.quantumStatistics, (int)p.resonanceWidth,
                (int)p.useEMMPions, (int)p.useEMMKaons, HRG::particleCount());
  }
  std::printf("#  T[MeV]      P/T^4      s/T^3    eps/T^4     nB[fm^-3]     "
              "nQ[fm^-3]     nS[fm^-3]\n");

  const double hbarc = 197.3269804;             /* MeV fm */
  const double hbarc3 = hbarc * hbarc * hbarc;  /* MeV^3 -> fm^-3 */

  /* both models share the contour type; only the namespace differs */
  namespace EC = EntropyContours;
  namespace ECP = EntropyContoursParam;
  const ContourEoS::Contour c = tabulated ? EC::evalContour(muB, muQ, muS)
                                          : ECP::evalContour(muB, muQ, muS);

  for (double T = Tmin; T <= Tmax + 1e-9; T += dT) {
    const double T3 = T * T * T;
    const double T4 = T3 * T;
    const double P = tabulated ? EC::pQCD(muB, muQ, T, c) : ECP::pQCD(muB, muQ, T, c);
    const double s = tabulated ? EC::sQCD(muB, muQ, T, c) : ECP::sQCD(muB, muQ, T, c);
    const double nB = tabulated ? EC::BarDens(muB, muQ, T, c) : ECP::BarDens(muB, muQ, T, c);
    const double nQ = tabulated ? EC::QCDcharge(muB, muQ, T, c) : ECP::QCDcharge(muB, muQ, T, c);
    const double nS = tabulated ? EC::StrDens(muB, muQ, T, c) : ECP::StrDens(muB, muQ, T, c);
    const double eps = tabulated ? EC::eQCD(muB, muQ, T, c) : ECP::eQCD(muB, muQ, T, c);
    std::printf("%8.2f %10.5f %10.5f %10.5f %13.6f %13.6f %13.6f\n", T, P / T4,
                s / T3, eps / T4, nB / hbarc3, nQ / hbarc3, nS / hbarc3);
  }

  EntropyContours::cleanup();
  EntropyContoursParam::cleanup();
  HRG::cleanup();
  return 0;
}
