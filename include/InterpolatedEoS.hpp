#ifndef INTERPOLATEDEOS_HPP
#define INTERPOLATEDEOS_HPP

#include <string>

namespace InterpolatedEoS {

/**
 * @brief Structure to hold the interpolated thermodynamic quantities.
 */
struct EoSValues {
  double nB; // Baryon density
  double nQ; // Electric charge density
  double s;  // Entropy density
};

/**
 * @brief Load the EoS table from a file.
 *
 * The file format is expected to be columns: T muB muQ nB nQ s
 * The data must form a regular rectilinear grid (tensor product of T, muB, muQ
 * axes). The order of lines doesn't strictly matter as long as it fills the
 * grid, but the axes will be sorted internally.
 *
 * @param filename Path to the table file.
 */
void loadTable(const std::string &filename);

/**
 * @brief Evaluate the EoS at a specific point (T, muB, muQ) using trilinear
 * interpolation.
 *
 * @param T Temperature (MeV)
 * @param muB Baryon Chemical Potential (MeV)
 * @param muQ Electric Charge Chemical Potential (MeV)
 * @return EoSValues struct containing nB, nQ, s.
 */
EoSValues evaluate(double T, double muB, double muQ);

/**
 * @brief Get the minimum temperature in the loaded EoS grid.
 */
double getTmin();

/**
 * @brief Get the maximum temperature in the loaded EoS grid.
 */
double getTmax();

/**
 * @brief Check if the table has been loaded.
 */
bool isLoaded();

/**
 * @brief Get the path to the currently loaded EoS table.
 */
std::string getLoadedFilename();

/**
 * @brief Free memory and reset the module.
 */
void cleanup();

} // namespace InterpolatedEoS

#endif // INTERPOLATEDEOS_HPP
