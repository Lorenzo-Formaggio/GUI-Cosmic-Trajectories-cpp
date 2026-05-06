#ifndef TRANGEMANAGER_HPP
#define TRANGEMANAGER_HPP

#include <string>
#include <functional>

namespace TRange {

struct RangeResult {
    double Tmin;
    double Tmax;
};

/**
 * @brief Validates and clamps the temperature range based on the Equation of State requirements.
 *        Specifically handles Interpolated EoS table loading and range enforcement.
 * 
 * @param eos             EoS type (2 for Interpolated Table)
 * @param eosTablePath    Path to the EoS table file
 * @param Tmin            User requested minimum temperature
 * @param Tmax            User requested maximum temperature
 * @param logCallback     Function to handle logging of warnings and status
 * @return RangeResult    The effectively clamped Tmin and Tmax
 */
RangeResult validateAndClamp(int eos, 
                             const std::string& eosTablePath, 
                             double Tmin, 
                             double Tmax, 
                             std::function<void(const std::string&)> logCallback);

} // namespace TRange

#endif // TRANGEMANAGER_HPP
