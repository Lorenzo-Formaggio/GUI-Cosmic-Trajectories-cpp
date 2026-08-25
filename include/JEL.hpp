// JEL.hpp
#ifndef JEL_HPP
#define JEL_HPP

#include <cmath>
#include <vector>

// Constants
constexpr double HC = 1;
constexpr double PI = 3.14159265358979323846;

// JEL Fermion parameters
constexpr double A_JEL = 0.433;
constexpr int M_JEL = 3;
constexpr int N_JEL = 3;

// JEL Boson parameters
constexpr double A_BOSE = 1.040;
constexpr int M_BOSE = 3;
constexpr int N_BOSE = 4;

constexpr double F_JEL_CUT = 1e-7;
constexpr double H_JEL_CUT = 1e-7;

// Fermion coefficient matrix pmn (4x4)
extern const double PMN[4][4];

// Boson coefficient matrix pBoseMN (4x5)
extern const double P_BOSE_MN[4][5];

// Interpolation tables
extern std::vector<double> PSI_JEL_TABLE;
extern std::vector<double> F_JEL_TABLE;
extern std::vector<double> PSI_BOSE_JEL_TABLE;
extern std::vector<double> H_JEL_TABLE;

// Initialize interpolation tables
void initializeJELTables();

// Utility function for linear interpolation
double linearInterp(const std::vector<double> &x, const std::vector<double> &y,
                    double xi);

// Helper functions (global)
double Psi(double mu, double T, double m);
double Psif(double f);
double gf(double f, double T, double m);
double fPsiJEL(double psi);
double fJEL(double mu, double T, double m);

double PsiBose(double mu, double T, double m);
double psiBoseFromH(double h);
double hPsiJEL(double psi);
double hJEL(double mu, double T, double m);

// Fermion thermodynamic functions (global)
double nf(double f, double T, double m, double g);
double Pf(double f, double T, double m, double g);
double ef(double f, double T, double m, double g);

// Boson thermodynamic functions (global)
double nb(double h, double T, double m, double g);
double Pb(double h, double T, double m, double g);
double eb(double h, double T, double m, double g);

// Namespace: T=0 Fermion functions
namespace JELf {
double kFT0(double mu, double m);
double nT0(double mu, double m, double g);
double nsT0(double mu, double m, double g);
double PT0(double mu, double m, double g);
double eT0(double mu, double m, double g);
} // namespace JELf

// Namespace: Fermion gas quantities
namespace jelf {
// Particles only
double nPart(double mu, double T, double m, double g);
double PPart(double mu, double T, double m, double g);
double ePart(double mu, double T, double m, double g);
double sPart(double mu, double T, double m, double g);
double nsPart(double mu, double T, double m, double g);

// Massless fermion 
double nm0(double mu, double T, double m, double g);
double sm0(double mu, double T, double m, double g);
double Pm0(double mu, double T, double m, double g);
double em0(double mu, double T, double m, double g);

// Particles + antiparticles
double nNet(double mu, double T, double m, double g);
double PTot(double mu, double T, double m, double g);
double eTot(double mu, double T, double m, double g);
double sTot(double mu, double T, double m, double g);
double nsTot(double mu, double T, double m, double g);
} // namespace jelf

// Namespace: Boson gas quantities
namespace jelb {
// Particles only
double nPart(double mu, double T, double m, double g);
double PPart(double mu, double T, double m, double g);
double ePart(double mu, double T, double m, double g);
double sPart(double mu, double T, double m, double g);
double nsPart(double mu, double T, double m, double g);

// Massless boson 
double nb0(double mu, double T, double m, double g);
double sb0(double mu, double T, double m, double g);
double Pb0(double mu, double T, double m, double g);
double eb0(double mu, double T, double m, double g);

// Particles + antiparticles
double nNet(double mu, double T, double m, double g);
double PTot(double mu, double T, double m, double g);
double eTot(double mu, double T, double m, double g);
double sTot(double mu, double T, double m, double g);
double nsTot(double mu, double T, double m, double g);
} // namespace jelb

#endif // JEL_HPP