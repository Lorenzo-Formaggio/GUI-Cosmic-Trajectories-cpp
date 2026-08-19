#ifndef ENTRCONT_HPP
#define ENTRCONT_HPP

// EntrCont: constant-entropy-contour EoS using lattice susceptibility splines
// (Chi2B/S/Q, Chi11BS/BQ/SQ from WB) and a low-T HRG seam anchor computed
// on the fly from a Thermal-FIST PDG list via EV-HRG (Vovchenko/Stoecker
// excluded-volume hadron resonance gas), eliminating the need for the large
// QvdW-HRG file. The HRG dependency is src/HRG.cpp + list-PDG2020.dat.

#include <string>
#include <vector>

namespace EntropyContours {

/**
 * @brief Per-(muB, muQ) contour state.
 */
struct ContourValues {
  double muB = 0.0;
  double muQ = 0.0;

  std::vector<double> T_phys;
  std::vector<double> dTdT0;
  std::vector<double> nB_T3;
  std::vector<double> nS_T3;
  std::vector<double> nQ_T3;
  std::vector<double> s_T3;
  std::vector<double> P_T4;
};

/**
 * @brief Initialize the EntropyContours equation of state.
 *
 * Loads the lattice susceptibility splines and the entropy/pressure-at-mu=0
 * splines from @p chisDir, and the PDG hadron list from @p pdgListPath. The
 * HRG anchor at T = T_HRG_MATCH = 80 MeV is computed on the fly via EV-HRG
 * (diagonal excluded-volume), with separate per-class b parameters
 * (b_meson_fm3 for B=0 species, b_baryon_fm3 for B!=0). Setting both b's
 * to zero recovers ideal HRG.
 *
 * @param chisDir     Directory containing Chi*_30-2000.dT1 files and
 *                    entro_2013_hrg+extrap.spln.
 * @param pdgListPath Thermal-FIST PDG list file (text format).
 * @param b_meson_fm3  Excluded volume per meson (fm^3). Default 1.0.
 * @param b_baryon_fm3 Excluded volume per baryon (fm^3). Default 3.42
 *                     (Vovchenko 2017 nucleon hard core, radius ~0.5 fm).
 * @param useHRG      If false, skip the HRG anchor (bare contour expansion).
 */
void initialize(const std::string &chisDir,
                const std::string &pdgListPath,
                double b_meson_fm3 = 1.0,
                double b_baryon_fm3 = 3.42,
                bool useHRG = true);

void cleanup();
bool isInitialized();

ContourValues evalContour(double muB, double muQ);

double sQCD(double muB, double muQ, double T, const ContourValues &c);
double BarDens(double muB, double muQ, double T, const ContourValues &c);
double QCDcharge(double muB, double muQ, double T, const ContourValues &c);
double StrDens(double muB, double muQ, double T, const ContourValues &c);
double pQCD(double muB, double muQ, double T, const ContourValues &c);

double sQCD(double muB, double muQ, double T);
double BarDens(double muB, double muQ, double T);
double QCDcharge(double muB, double muQ, double T);
double StrDens(double muB, double muQ, double T);
double pQCD(double muB, double muQ, double T);

} // namespace EntropyContours

#endif // ENTRCONT_HPP
