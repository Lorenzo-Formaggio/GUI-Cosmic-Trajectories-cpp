#include "../include/TRangeManager.hpp"
#include "../include/InterpolatedEoS.hpp"
#include <sstream>
#include <iomanip>

namespace TRange {

RangeResult validateAndClamp(int eos, 
                             const std::string& eosTablePath, 
                             double Tmin, 
                             double Tmax, 
                             std::function<void(const std::string&)> logCallback) {
    
    double effectiveTmin = Tmin;
    double effectiveTmax = Tmax;

    if (eos == 2) {
        if (InterpolatedEoS::isLoaded() && InterpolatedEoS::getLoadedFilename() == eosTablePath) {
            if (logCallback) logCallback("Interpolated EoS table is already in memory. Skipping reload.");
        } else {
            if (logCallback) logCallback("Loading interpolated EoS table... (This may take a moment)");
            InterpolatedEoS::loadTable(eosTablePath);
        }

        if (InterpolatedEoS::isLoaded()) {
            double tableTmin = InterpolatedEoS::getTmin();
            double tableTmax = InterpolatedEoS::getTmax();
            
            if (logCallback) {
                std::stringstream ss;
                ss << std::fixed << std::setprecision(1);
                ss << "  Table loaded. T range: " << tableTmin << " – " << tableTmax << " MeV";
                logCallback(ss.str());
            }

            auto formatWarning = [](const std::string& param, double val, double clampVal, bool isMin) {
                std::stringstream ss;
                ss << std::fixed << std::setprecision(1);
                ss << "<font color='#ffc107'><b>Warning:</b> Requested " << param 
                   << " (" << val << " MeV) " << (isMin ? "is below" : "exceeds") 
                   << " table " << (isMin ? "minimum" : "maximum") 
                   << " (" << clampVal << " MeV). Clamping to table " 
                   << (isMin ? "minimum" : "maximum") << ".</font>";
                return ss.str();
            };

            // Clamp user Tmin
            if (effectiveTmin < tableTmin) {
                if (logCallback) logCallback(formatWarning("Tmin", effectiveTmin, tableTmin, true));
                effectiveTmin = tableTmin;
            } else if (effectiveTmin > tableTmax) {
                if (logCallback) logCallback(formatWarning("Tmin", effectiveTmin, tableTmax, false));
                effectiveTmin = tableTmax;
            }

            // Clamp user Tmax
            if (effectiveTmax > tableTmax) {
                if (logCallback) logCallback(formatWarning("Tmax", effectiveTmax, tableTmax, false));
                effectiveTmax = tableTmax;
            } else if (effectiveTmax < tableTmin) {
                if (logCallback) logCallback(formatWarning("Tmax", effectiveTmax, tableTmin, true));
                effectiveTmax = tableTmin;
            }
        }
    }

    return {effectiveTmin, effectiveTmax};
}

} // namespace TRange
