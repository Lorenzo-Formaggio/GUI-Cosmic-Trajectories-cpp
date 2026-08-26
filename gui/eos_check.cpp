/*
 * eos_check -- cross-check the entropy-contour EoS in this repository against
 * s_contours_c-dev4-tristan's `eos_line`.
 *
 * Usage:
 *   eos_check [muB] [muQ] [muS] [Tmin Tmax dT] [--no-hrg] [--fitted]
 *             [--data <path>]
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
  bool fitted = false;
  std::string dataPath = ".";

  double pos[6];
  int npos = 0;
  for (int i = 1; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--no-hrg")) {
      useHRG = false;
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

  EntropyContoursParam::initialize(dataPath + "/EntroContourEoS/chis",
                                   dataPath +
                                       "/EntroContourEoS/HRG/list-PDG2020.dat",
                                   1.0, 3.42, useHRG);

  const double mu = std::sqrt(muB * muB + muQ * muQ + muS * muS);
  const double deg = 180.0 / M_PI;
  const double theta = (mu > 0.0) ? std::acos(muB / mu) : 0.0;
  const double phi = std::atan2(muS, muQ);

  std::printf("# EoS at muB=%g muQ=%g muS=%g MeV\n", muB, muQ, muS);
  std::printf("# radial mu = %.4f MeV, theta = %.6f deg, phi = %.6f deg\n", mu,
              theta * deg, phi * deg);
  std::printf("# cross mode: %s\n",
              EntropyContoursParam::crossMode() ==
                      EntropyContoursParam::CrossMode::Fitted
                  ? "fitted (6 independent)"
                  : "isospin-derived");
  std::printf("# low-T boundary: %s (Tlow = %g MeV)\n",
              useHRG ? "QvdW-HRG (Thermal-FIST)" : "mu = 0 contour, p0(Tlow)",
              EntropyContoursParam::referenceTemperature());
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

  const EntropyContoursParam::ContourValues c =
      EntropyContoursParam::evalContour(muB, muQ, muS);

  for (double T = Tmin; T <= Tmax + 1e-9; T += dT) {
    const double T3 = T * T * T;
    const double T4 = T3 * T;
    const double P = EntropyContoursParam::pQCD(muB, muQ, T, c);
    const double s = EntropyContoursParam::sQCD(muB, muQ, T, c);
    const double nB = EntropyContoursParam::BarDens(muB, muQ, T, c);
    const double nQ = EntropyContoursParam::QCDcharge(muB, muQ, T, c);
    const double nS = EntropyContoursParam::StrDens(muB, muQ, T, c);
    const double eps = EntropyContoursParam::eQCD(muB, muQ, T, c);
    std::printf("%8.2f %10.5f %10.5f %10.5f %13.6f %13.6f %13.6f\n", T, P / T4,
                s / T3, eps / T4, nB / hbarc3, nQ / hbarc3, nS / hbarc3);
  }

  EntropyContoursParam::cleanup();
  HRG::cleanup();
  return 0;
}
