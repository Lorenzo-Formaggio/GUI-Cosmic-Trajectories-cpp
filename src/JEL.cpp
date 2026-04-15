// jel.cpp
#include "../include/JEL.hpp"
#include <cmath>
#include <stdexcept>

//*******************************************
//(****** Fermi Integrals: JEL approach ******
//*******************************************)
// Ref: Johns, Ellis and Lattimer 1996 APJ ,473:1020È1028,1996 Dec 20. See also
// https://arxiv.org/abs/2311.03025v2
// Approximation of Fermi integrals with 10^-4 precision. The scalar density is
// not precise for small masses

// Coefficient matrices
const double PMN[4][4] = {{5.34689, 18.0517, 21.3422, 8.53240},
                          {16.8441, 55.7051, 63.6901, 24.6213},
                          {17.4708, 56.3902, 62.1319, 23.2602},
                          {6.07364, 18.9992, 20.0285, 7.11153}};

const double P_BOSE_MN[4][5] = {
    {1.68130, 6.85060, 10.8539, 7.81762, 2.16465},
    {4 * 1.68130, 4 * 6.85060, 4 * 10.8539, 4 * 7.81762, 4 * 2.16465},
    {8.51373, 35.6576, 57.7975, 42.4049, 11.8321},
    {3.47433, 15.1995, 25.6536, 19.3811, 5.54423}};

std::vector<double> PSI_JEL_TABLE;
std::vector<double> F_JEL_TABLE;
std::vector<double> PSI_BOSE_JEL_TABLE;
std::vector<double> H_JEL_TABLE;

// Initialize interpolation tables
void initializeJELTables() {
  // Fermion tables
  PSI_JEL_TABLE.clear();
  F_JEL_TABLE.clear();

  // Range -10 to 8 with step 0.00001
  // i goes from -1000000 to 800000
  for (int k = -1000000; k <= 800000; ++k) {
    double i = k / 100000.0;
    double f = std::pow(10.0, i);
    double psi = Psif(f);
    F_JEL_TABLE.push_back(f);
    PSI_JEL_TABLE.push_back(psi);
  }

  // Boson tables
  PSI_BOSE_JEL_TABLE.clear();
  H_JEL_TABLE.clear();

  // Range -7 to 8 with step 0.00001
  // For Bosons, Psi decreases as h increases (and h increases with i).
  // We need Psi sorted ascending for interpolation.
  // So we iterate i from 8 down to -7.
  // i goes from 800000 down to -700000
  for (int k = 800000; k >= -700000; --k) {
    double i = k / 100000.0;
    double h = std::pow(10.0, i);
    double psi = psiBoseFromH(h);
    H_JEL_TABLE.push_back(h);
    PSI_BOSE_JEL_TABLE.push_back(psi);
  }
}

// Linear interpolation utility
double linearInterp(const std::vector<double> &x, const std::vector<double> &y,
                    double xi) {
  if (x.size() != y.size() || x.empty()) {
    throw std::runtime_error("Invalid interpolation data");
  }

  auto it = std::lower_bound(x.begin(), x.end(), xi);

  if (it == x.begin())
    return y[0];
  if (it == x.end())
    return y.back();

  size_t idx = std::distance(x.begin(), it);
  double x1 = x[idx - 1], x2 = x[idx];
  double y1 = y[idx - 1], y2 = y[idx];

  return y1 + (y2 - y1) * (xi - x1) / (x2 - x1);
}

// T=0 Fermion functions for fermions
namespace JELf {
// Fermion gas functions
double kFT0(double mu, double m) { return std::sqrt(mu * mu - m * m); }
// Fermion density T=0
double nT0(double mu, double m, double g) {
  double sign = (mu >= 0) ? 1.0 : -1.0;
  double kF = kFT0(mu, m);
  return sign * g * std::pow(kF, 3) / (6.0 * PI * PI * std::pow(HC, 3));
}
// Fermion net density T=0
double nsT0(double mu, double m, double g) {
  double kF = kFT0(mu, m);
  double abs_mu = std::abs(mu);
  return m * g / (4.0 * PI * PI * std::pow(HC, 3)) *
         (kF * abs_mu - m * m * std::log((kF + abs_mu) / m));
}
// Fermion pressure T=0
double PT0(double mu, double m, double g) {
  double kF = kFT0(mu, m);
  double abs_mu = std::abs(mu);
  return g / (48.0 * PI * PI * std::pow(HC, 3)) *
         ((2.0 * std::pow(kF, 3) - 3.0 * m * m * kF) * abs_mu +
          3.0 * std::pow(m, 4) * std::log((kF + abs_mu) / m));
}
// Fermion energy T=0
double eT0(double mu, double m, double g) {
  double kF = kFT0(mu, m);
  double abs_mu = std::abs(mu);
  return g / (16.0 * PI * PI * std::pow(HC, 3)) *
         ((2.0 * std::pow(kF, 3) + m * m * kF) * abs_mu -
          std::pow(m, 4) * std::log((kF + abs_mu) / m));
}
} // namespace JELf
// Fermion helper functions
double Psi(double mu, double T, double m) { return (mu - m) / T; }

double Psif(double f) {
  return 2.0 * std::sqrt(1.0 + f / A_JEL) +
         std::log((std::sqrt(1.0 + f / A_JEL) - 1.0) /
                  (std::sqrt(1.0 + f / A_JEL) + 1.0));
}

double gf(double f, double T, double m) { return std::sqrt(1.0 + f) * T / m; }

double fPsiJEL(double psi) {
  if (psi < -14.66737234624) {
    return 1e-9;
  }
  return linearInterp(PSI_JEL_TABLE, F_JEL_TABLE, psi);
}

double fJEL(double mu, double T, double m) { return fPsiJEL(Psi(mu, T, m)); }

// Fermion thermodynamic functions in JEL variables
double nf(double f, double T, double m, double g) {
  double gf_val = std::sqrt(1.0 + f) * T / m;
  double f1 = 1.0 + f;
  double gf1 = 1.0 + gf_val;

  double sum = 0.0;
  for (int i = 0; i <= 3; ++i) {
    for (int j = 0; j <= 3; ++j) {
      sum += PMN[i][j] * std::pow(f, i) * std::pow(gf_val, j) *
             (1.0 + i + (0.25 + j / 2.0 - M_JEL) * f / f1 +
              (0.75 - N_JEL / 2.0) * f * gf_val / (f1 * gf1));
    }
  }

  return g / 2.0 * std::pow(m, 3) / (PI * PI * std::pow(HC, 3)) * f *
         std::pow(gf_val, 3.0 / 2.0) * std::pow(1.0 + gf_val, 3.0 / 2.0) /
         (std::pow(f1, M_JEL + 0.5) * std::pow(gf1, N_JEL) *
          std::sqrt(1.0 + f / A_JEL)) *
         sum;
}

double Pf(double f, double T, double m, double g) {
  double gf_val = std::sqrt(1.0 + f) * T / m;

  double sum = 0.0;
  for (int i = 0; i <= 3; ++i) {
    for (int j = 0; j <= 3; ++j) {
      sum += PMN[i][j] * std::pow(f, i) * std::pow(gf_val, j);
    }
  }

  return g / 2.0 * std::pow(m, 4) / (PI * PI * std::pow(HC, 3)) * f *
         std::pow(gf_val, 5.0 / 2.0) * std::pow(1.0 + gf_val, 3.0 / 2.0) /
         (std::pow(f + 1.0, M_JEL + 1) * std::pow(gf_val + 1.0, N_JEL)) * sum;
}

double ef(double f, double T, double m, double g) {
  double gf_val = std::sqrt(1.0 + f) * T / m;
  double f1 = 1.0 + f;
  double gf1 = 1.0 + gf_val;

  double sum = 0.0;
  for (int i = 0; i <= M_JEL; ++i) {
    for (int j = 0; j <= N_JEL; ++j) {
      sum += PMN[i][j] * std::pow(f, i) * std::pow(gf_val, j) *
             (1.5 + j + (1.5 - N_JEL) * gf_val / gf1);
    }
  }

  double nVal = nf(f, T, m, g);

  return g / 2.0 * std::pow(m, 4) / (PI * PI * std::pow(HC, 3)) * f *
             std::pow(gf_val, 5.0 / 2.0) * std::pow(1.0 + gf_val, 3.0 / 2.0) /
             (std::pow(f1, M_JEL + 1) * std::pow(gf1, N_JEL)) * sum +
         m * nVal;
}

// Fermion gas quantities (particles only) in µ T variables
namespace jelf {
// Particle number density of fermions (particles only)
// Returns: n (MeV^3)
double nPart(double mu, double T, double m, double g) {
  double f = fJEL(mu, T, m);
  if (f < F_JEL_CUT)
    return 0.0;
  return nf(f, T, m, g);
}

// Pressure of fermion gas (particle contribution)
// Returns: P (MeV^4)
double PPart(double mu, double T, double m, double g) {
  double f = fJEL(mu, T, m);
  if (f < F_JEL_CUT)
    return 0.0;
  return Pf(f, T, m, g);
}

// Energy density of fermion gas (particle contribution)
// Returns: rho (MeV^4)
double ePart(double mu, double T, double m, double g) {
  double f = fJEL(mu, T, m);
  if (f < F_JEL_CUT)
    return 0.0;
  return ef(f, T, m, g);
}

// Entropy density of fermion gas (particle contribution)
// Returns: s (MeV^3)
double sPart(double mu, double T, double m, double g) {
  return (ePart(mu, T, m, g) + PPart(mu, T, m, g) - mu * nPart(mu, T, m, g)) /
         T;
}

// Scalar density of fermions (particles only)
// Corresponds to <\bar{\psi}\psi> or the trace of the energy-momentum tensor
// divided by mass Returns: n_s (1/MeV^3)
double nsPart(double mu, double T, double m, double g) {
  return (ePart(mu, T, m, g) - 3.0 * PPart(mu, T, m, g)) / m;
}

// Massless fermion special cases
double nm0(double mu, double T, double m, double g) {
  return (1.0 / std::pow(HC, 3)) * g *
         ((1.0 / 12.0) * T * T * mu +
          (1.0 / (12.0 * PI * PI)) * std::pow(mu, 3));
}

double sm0(double mu, double T, double m, double g) {
  return (1.0 / std::pow(HC, 3)) * g *
         ((7.0 * PI * PI / 180.0) * std::pow(T, 3) +
          (1.0 / 12.0) * mu * mu * T);
}

// Fermion gas quantities (particles + antiparticles)

// Net number density (particles - antiparticles)
// Returns: n_net = n_fermions - n_antifermions (MeV^3)
double nNet(double mu, double T, double m, double g) {
  if (m == 0.0) {
    return nm0(mu, T, m, g) - nm0(-mu, T, m, g);
  }
  return nPart(mu, T, m, g) - nPart(-mu, T, m, g);
}

// Total pressure (particles + antiparticles)
// Returns: P_tot (MeV^4)
double PTot(double mu, double T, double m, double g) {
  return PPart(mu, T, m, g) + PPart(-mu, T, m, g);
}

// Total energy density (particles + antiparticles)
// Returns: rho_tot (MeV^4)
double eTot(double mu, double T, double m, double g) {
  return ePart(mu, T, m, g) + ePart(-mu, T, m, g);
}

// Total entropy density (particles + antiparticles)
// Returns: s_tot (MeV^3)
double sTot(double mu, double T, double m, double g) {
  if (m == 0.0) {
    return sm0(mu, T, m, g) + sm0(-mu, T, m, g);
  }
  return sPart(mu, T, m, g) + sPart(-mu, T, m, g);
}

// Total scalar density (particles + antiparticles)
// Returns: n_s_tot ( MeV^3)
double nsTot(double mu, double T, double m, double g) {
  return nsPart(mu, T, m, g) + nsPart(-mu, T, m, g);
}
} // namespace jelf
// Boson helper functions
double PsiBose(double mu, double T, double m) { return (m - mu) / T; }

double psiBoseFromH(double h) {
  return h / (std::sqrt(A_BOSE) + h) -
         std::log((std::sqrt(A_BOSE) + h) / std::sqrt(A_BOSE));
}

double hPsiJEL(double psi) {
  if (psi < -17.406387035814335) {
    return 1e8;
  }
  return linearInterp(PSI_BOSE_JEL_TABLE, H_JEL_TABLE, psi);
}

double hJEL(double mu, double T, double m) {
  return hPsiJEL(PsiBose(mu, T, m));
}

// Boson thermodynamic functions
double nb(double h, double T, double m, double g) {
  double t = T / m;
  double sum = 0.0;

  for (int i = 0; i <= M_BOSE; ++i) {
    for (int j = 0; j <= N_BOSE; ++j) {
      sum += P_BOSE_MN[i][j] * std::pow(h, i - 2) * std::pow(t, j) *
             (-i + h * (M_BOSE + 1 - i));
    }
  }

  return g / 2.0 * std::pow(m, 3) / (PI * PI * std::pow(HC, 3)) *
         std::pow(h + std::sqrt(A_BOSE), 2.0) *
         std::pow(t * (1.0 + t), 3.0 / 2.0) /
         (std::pow(1.0 + h, M_BOSE + 2) * std::pow(1.0 + t, N_BOSE)) * sum;
}

double Pb(double h, double T, double m, double g) {
  double t = T / m;
  double sum = 0.0;

  for (int i = 0; i <= M_BOSE; ++i) {
    for (int j = 0; j <= N_BOSE; ++j) {
      sum += P_BOSE_MN[i][j] * std::pow(h, i) * std::pow(t, j);
    }
  }

  return g / 2.0 * std::pow(m, 4) / (PI * PI * std::pow(HC, 3)) *
         std::pow(t, 5.0 / 2.0) * std::pow(1.0 + t, 3.0 / 2.0) /
         (std::pow(1.0 + h, M_BOSE + 1) * std::pow(1.0 + t, N_BOSE)) * sum;
}

double eb(double h, double T, double m, double g) {
  double t = T / m;
  double sum = 0.0;

  for (int i = 0; i <= M_BOSE; ++i) {
    for (int j = 0; j <= N_BOSE; ++j) {
      sum += P_BOSE_MN[i][j] * std::pow(h, i) * std::pow(t, j) *
             (1.5 + j + (1.5 - N_BOSE) * t / (1.0 + t));
    }
  }

  return g / 2.0 * std::pow(m, 4) / (PI * PI * std::pow(HC, 3)) *
         std::pow(t, 5.0 / 2.0) * std::pow(1.0 + t, 3.0 / 2.0) /
         (std::pow(1.0 + h, M_BOSE + 1) * std::pow(1.0 + t, N_BOSE)) * sum;
}

// Boson gas quantities (particles only)
namespace jelb {
// Particle number density of bosons (particles only)
// Returns: n (MeV^3)
double nPart(double mu, double T, double m, double g) {
  double h = hJEL(mu, T, m);
  if (h < H_JEL_CUT)
    return 0.0;
  return nb(h, T, m, g);
}

// Pressure of boson gas (particle contribution)
// Returns: P (MeV^4)
double PPart(double mu, double T, double m, double g) {
  double h = hJEL(mu, T, m);
  if (h < H_JEL_CUT)
    return 0.0;
  return Pb(h, T, m, g);
}

// Energy density of boson gas (particle contribution)
// Returns: rho (MeV^4)
double ePart(double mu, double T, double m, double g) {
  double h = hJEL(mu, T, m);
  if (h < H_JEL_CUT)
    return 0.0;
  return eb(h, T, m, g);
}

// Entropy density of boson gas (particle contribution)
// Returns: s (MeV^3)
double sPart(double mu, double T, double m, double g) {
  return (ePart(mu, T, m, g) + PPart(mu, T, m, g) - mu * nPart(mu, T, m, g)) /
         T;
}

// Scalar density of bosons (particles only)
// Returns: n_s (MeV^3)
double nsPart(double mu, double T, double m, double g) {
  return (ePart(mu, T, m, g) - 3.0 * PPart(mu, T, m, g)) / m;
}

// Massless boson special cases
double nb0(double mu, double T, double m, double g) {
  return g * 1.202 * std::pow(T, 3) / (std::pow(HC, 3) * PI * PI);
}

double sb0(double mu, double T, double m, double g) {
  return 2.0 * (PI * PI / 45.0) * g * std::pow(T, 3) / std::pow(HC, 3);
}

// Boson gas quantities (particles + antiparticles)

// Net number density (particles - antiparticles)
// Returns: n_net = n_bosons - n_antibosons (MeV^3)
double nNet(double mu, double T, double m, double g) {
  if (m == 0.0) {
    return nb0(mu, T, m, g);
  }
  return nPart(mu, T, m, g) - nPart(-mu, T, m, g);
}

// Total pressure (particles + antiparticles)
// Returns: P_tot (MeV^4)
double PTot(double mu, double T, double m, double g) {
  return PPart(mu, T, m, g) + PPart(-mu, T, m, g);
}

// Total energy density (particles + antiparticles)
// Returns: rho_tot (MeV^4)
double eTot(double mu, double T, double m, double g) {
  return ePart(mu, T, m, g) + ePart(-mu, T, m, g);
}

// Total entropy density (particles + antiparticles)
// Returns: s_tot (MeV^3)
double sTot(double mu, double T, double m, double g) {
  if (m == 0.0) {
    return sb0(mu, T, m, g);
  }
  return sPart(mu, T, m, g) + sPart(-mu, T, m, g);
}

// Total scalar density (particles + antiparticles)
// Returns: n_s_tot (MeV^3)
double nsTot(double mu, double T, double m, double g) {
  return nsPart(mu, T, m, g) + nsPart(-mu, T, m, g);
}
} // namespace jelb