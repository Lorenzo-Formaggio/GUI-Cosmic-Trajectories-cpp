#ifndef PARAMETER_READER_HPP
#define PARAMETER_READER_HPP

#include <string>

struct SimulationParameters {
  double b;
  double le;
  double lmu;
  double ltau;
  double dT;
  double Tmin;
  double Tmax;
  int nf;
  int eos;           // 0 = free QGP, 1 = lattice QCD
  int guessMethod;   // 0 = simple (previous solution), 1 = linear extrapolation
  int scanDirection; // 0 = low to high T, 1 = high to low T
};

// Read parameters from a file or use defaults
// Function signature includes the source path to help resolve relative files
SimulationParameters
readParameters(const std::string &filename = "InputParameter.txt",
               const std::string &source_full_path = "");

#endif
