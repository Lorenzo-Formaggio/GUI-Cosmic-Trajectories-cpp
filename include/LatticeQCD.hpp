#ifndef LATTICEQCD_HPP
#define LATTICEQCD_HPP

#include <string>

namespace LatticeQCD {

/**
 * @brief Structure containing all susceptibility values at a given temperature.
 *
 * This stores all chi values needed for density calculations, evaluated once
 * at a given temperature. The naming convention is chi_{BQS} where B, Q, S
 * are the orders of derivatives with respect to muB, muQ, muS.
 */
struct ChiValues {
  // Second order susceptibilities
  double chi200; // chi_2^B
  double chi020; // chi_2^Q
  double chi002; // chi_2^S
  double chi110; // chi_11^BQ
  double chi101; // chi_11^BS
  double chi011; // chi_11^SQ

  // Fourth order susceptibilities
  double chi400; // chi_4^B
  double chi040; // chi_4^Q
  double chi004; // chi_4^S
  double chi310; // chi_31^BQ
  double chi301; // chi_31^BS
  double chi130; // chi_13^BQ
  double chi103; // chi_13^BS
  double chi031; // chi_31^SQ
  double chi013; // chi_13^SQ
  double chi220; // chi_22^BQ
  double chi202; // chi_22^BS
  double chi022; // chi_22^SQ
  double chi211; // chi_211^BSQ
  double chi121; // chi_121^BSQ
  double chi112; // chi_112^BSQ

  // Derivatives for entropy calculation (2nd order)
  double dchi200dT;
  double dchi020dT;
  double dchi002dT;
  double dchi110dT;
  double dchi101dT;
  double dchi011dT;

  // Derivatives for entropy calculation (4th order - computed numerically)
  double dchi400dT;
  double dchi040dT;
  double dchi004dT;
  double dchi310dT;
  double dchi301dT;
  double dchi130dT;
  double dchi103dT;
  double dchi031dT;
  double dchi013dT;
  double dchi220dT;
  double dchi202dT;
  double dchi022dT;
  double dchi211dT;
  double dchi121dT;
  double dchi112dT;

  // CHI000 (pressure) and its derivative
  double chi000;
  double dchi000dT;
};

/**
 * @brief Initialize the lattice QCD equation of state.
 *
 * This function loads susceptibility data from files and sets up GSL spline
 * interpolation. Must be called before using any other LatticeQCD functions.
 *
 * @param dataPath Path to the directory containing susceptibility data files.
 *                 Default: "LatticeEoS/threeflavors/"
 */
void initialize(const std::string &dataPath = "LatticeEoS/threeflavors/",
                bool includeCharm = false);

/**
 * @brief Clean up lattice QCD resources.
 *
 * Frees GSL spline and accelerator objects.
 */
void cleanup();

/**
 * @brief Check if lattice QCD is initialized.
 */
bool isInitialized();

/**
 * @brief Evaluate all susceptibilities at a given temperature.
 *
 * This function evaluates all chi values from spline interpolation at the
 * given temperature. Use this once per temperature, then pass the result
 * to the density functions.
 *
 * @param T Temperature (MeV)
 * @return ChiValues structure containing all susceptibilities
 */
ChiValues evalChis(double T);

/**
 * @brief Baryon density from lattice QCD EoS using pre-computed chi values.
 *
 * Uses Taylor expansion up to 4th order in chemical potentials.
 * Note: muS = 0 is assumed for strangeness neutrality.
 *
 * @param muB Baryon chemical potential (MeV)
 * @param muQ Electric charge chemical potential (MeV)
 * @param T Temperature (MeV)
 * @param chi Pre-computed susceptibility values at temperature T
 * @return Baryon number density (MeV^3)
 */
double BarDens(double muB, double muQ, double T, const ChiValues &chi);

/**
 * @brief Electric charge density from lattice QCD EoS using pre-computed chis.
 *
 * @param muB Baryon chemical potential (MeV)
 * @param muQ Electric charge chemical potential (MeV)
 * @param T Temperature (MeV)
 * @param chi Pre-computed susceptibility values at temperature T
 * @return Electric charge density (MeV^3)
 */
double QCDcharge(double muB, double muQ, double T, const ChiValues &chi);

/**
 * @brief Entropy density from lattice QCD EoS using pre-computed chi values.
 *
 * @param muB Baryon chemical potential (MeV)
 * @param muQ Electric charge chemical potential (MeV)
 * @param T Temperature (MeV)
 * @param chi Pre-computed susceptibility values at temperature T
 * @return Entropy density (MeV^3)
 */
double sQCD(double muB, double muQ, double T, const ChiValues &chi);

/**
 * @brief Strangeness density from lattice QCD EoS using pre-computed chis.
 *
 * @param muB Baryon chemical potential (MeV)
 * @param muQ Electric charge chemical potential (MeV)
 * @param T Temperature (MeV)
 * @param chi Pre-computed susceptibility values at temperature T
 * @return Strangeness density (MeV^3)
 */
double StrDens(double muB, double muQ, double T, const ChiValues &chi);

// ============================================================================
// Convenience overloads (evaluate chis internally - less efficient)
// ============================================================================

/**
 * @brief Baryon density (convenience overload that evaluates chis internally).
 */
double BarDens(double muB, double muQ, double T);

/**
 * @brief Electric charge density (convenience overload).
 */
double QCDcharge(double muB, double muQ, double T);

/**
 * @brief Entropy density (convenience overload).
 */
double sQCD(double muB, double muQ, double T);

/**
 * @brief Strangeness density (convenience overload).
 */
double StrDens(double muB, double muQ, double T);

} // namespace LatticeQCD

#endif // LATTICEQCD_HPP
