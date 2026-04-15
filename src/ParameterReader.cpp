#include "../include/ParameterReader.hpp"
#include "../include/InterpolatedEoS.hpp"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

SimulationParameters readParameters(const std::string &filename,
                                    const std::string &source_full_path) {
  // Default values (eos = 0 means free QGP, guessMethod = 0 means simple,
  // scanDirection = 0 means low to high)
  SimulationParameters params = {8.6e-11, -0.01, -0.01, -0.01, 1.0, 30.0,
                                 2000.0,  4,     0,     0,     0};

  // Determine the directory of the source file (useful for fallback)
  std::string dir_path = "";
  if (!source_full_path.empty()) {
    size_t last_slash = source_full_path.find_last_of("/\\");
    if (last_slash != std::string::npos) {
      dir_path = source_full_path.substr(0, last_slash + 1);
    }
  }

  // Try opening file in current directory first
  std::string input_path = filename;
  std::ifstream infile(input_path);

  // Fallback to origin directory if not found in CWD and source_full_path was
  // provided
  if (!infile.is_open() && !dir_path.empty()) {
    input_path = dir_path + filename;
    infile.open(input_path);
  }

  bool fileOpened = infile.is_open();

  if (fileOpened) {
    std::string line;
    while (std::getline(infile, line)) {
      std::istringstream iss(line);
      std::string key, eq, val;
      if (iss >> key >> eq >> val && eq == "=") {
        if (key == "b")
          params.b = std::stod(val);
        else if (key == "le")
          params.le = std::stod(val);
        else if (key == "lmu")
          params.lmu = std::stod(val);
        else if (key == "ltau")
          params.ltau = std::stod(val);
        else if (key == "dT")
          params.dT = std::stod(val);
        else if (key == "nf")
          params.nf = std::stoi(val);
        else if (key == "eos")
          params.eos = std::stoi(val);
        else if (key == "Tmin")
          params.Tmin = std::stod(val);
        else if (key == "Tmax")
          params.Tmax = std::stod(val);
        else if (key == "guessMethod")
          params.guessMethod = std::stoi(val);
        else if (key == "scanDirection")
          params.scanDirection = std::stoi(val);
      }
    }
    infile.close();
  }

  // If using interpolated EoS, load the table now to update and extract Tmin/Tmax
  if (params.eos == 2) {
    try {
      InterpolatedEoS::loadTable("EoS_Table.txt");
      if (InterpolatedEoS::isLoaded()) {
        params.Tmin = InterpolatedEoS::getTmin();
        params.Tmax = InterpolatedEoS::getTmax();
      }
    } catch (const std::exception& e) {
      // Don't crash here, just let it be handled later or print error
      std::cerr << "Warning during parameter read: " << e.what() << std::endl;
    }
  }

  // Print aesthetic header using Unicode box-drawing
  std::cout << std::endl;
  std::cout << "╔════════════════════════════════════════════════════════════╗"
            << std::endl;
  std::cout << "║         COSMIC TRAJECTORY - QGP Phase Diagram              ║"
            << std::endl;
  std::cout << "╠════════════════════════════════════════════════════════════╣"
            << std::endl;

  if (fileOpened) {
    std::cout << "║  [OK] Parameters loaded from: " << std::left
              << std::setw(28) << input_path << " ║" << std::endl;
  } else {
    std::cout << "║  [!!] Could not open: " << std::left << std::setw(36)
              << filename << " ║" << std::endl;
    std::cout
        << "║       Using default values.                                ║"
        << std::endl;
  }

  if (params.eos == 2) {
    std::cout << "║  [OK] EoS table loaded from: " << std::left << std::setw(29)
              << "EoS_Table.txt" << " ║" << std::endl;
  }

  std::cout << "╠════════════════════════════════════════════════════════════╣"
            << std::endl;
  std::cout << "║  ASYMMETRY PARAMETERS                                      ║"
            << std::endl;
  std::cout << "║  ----------------------------------------------------------║"
            << std::endl;

  std::ostringstream oss;
  oss << std::scientific << std::setprecision(2) << params.b;
  std::cout << "║    b (baryon)     = " << std::left << std::setw(38)
            << oss.str() << " ║" << std::endl;

  oss.str("");
  oss.clear();
  oss << std::fixed << std::setprecision(10) << params.le;
  std::cout << "║    le (electron)  = " << std::left << std::setw(38)
            << oss.str() << " ║" << std::endl;

  oss.str("");
  oss.clear();
  oss << std::fixed << std::setprecision(10) << params.lmu;
  std::cout << "║    lmu (muon)     = " << std::left << std::setw(38)
            << oss.str() << " ║" << std::endl;

  oss.str("");
  oss.clear();
  oss << std::fixed << std::setprecision(10) << params.ltau;
  std::cout << "║    ltau (tau)     = " << std::left << std::setw(38)
            << oss.str() << " ║" << std::endl;

  std::cout << "╠════════════════════════════════════════════════════════════╣"
            << std::endl;
  std::cout << "║  TEMPERATURE SCAN                                          ║"
            << std::endl;
  std::cout << "║  ----------------------------------------------------------║"
            << std::endl;

  oss.str("");
  oss.clear();
  if (params.scanDirection == 0) {
    oss << std::fixed << std::setprecision(1) << params.Tmin << " -> "
        << params.Tmax << " MeV  (step: " << params.dT << " MeV)";
  } else {
    oss << std::fixed << std::setprecision(1) << params.Tmax << " -> "
        << params.Tmin << " MeV  (step: " << params.dT << " MeV)";
  }
  std::cout << "║    Range: " << std::left << std::setw(48) << oss.str() << " ║"
            << std::endl;

  std::cout << "╠════════════════════════════════════════════════════════════╣"
            << std::endl;
  std::cout << "║  PHYSICS OPTIONS                                           ║"
            << std::endl;
  std::cout << "║  ----------------------------------------------------------║"
            << std::endl;

  oss.str("");
  oss.clear();
  if (params.eos == 2) {
    oss << "Interpolated Table";
  } else {
    oss << params.nf << " flavors";
  }
  std::cout << "║    Quark flavors    : " << std::left << std::setw(36)
            << oss.str() << " ║" << std::endl;

  oss.str("");
  oss.clear();
  if (params.eos == 0) oss << "Free QGP";
  else if (params.eos == 1) oss << "Lattice QCD";
  else if (params.eos == 2) oss << "Interpolated Table";
  else oss << "Unknown";
  std::cout << "║    Equation of State: " << std::left << std::setw(36)
            << oss.str() << " ║" << std::endl;

  oss.str("");
  oss.clear();
  oss << (params.guessMethod == 0 ? "Simple (previous solution)"
                                  : "Linear extrapolation");
  std::cout << "║    Guess method     : " << std::left << std::setw(36)
            << oss.str() << " ║" << std::endl;

  oss.str("");
  oss.clear();
  oss << (params.scanDirection == 0 ? "Low to High T" : "High to Low T");
  std::cout << "║    Scan direction   : " << std::left << std::setw(36)
            << oss.str() << " ║" << std::endl;

  std::cout << "╚════════════════════════════════════════════════════════════╝"
            << std::endl;
  std::cout << std::endl;

  return params;
}
