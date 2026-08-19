/*
 * CosmicTrajectoryDensities
 * 
 * Based on CosmicTrajectory.cpp from Thermal-FIST
 * Extended to output individual hadron densities normalized by nB
 */
#include <string.h>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <ctime>
#include <cstdio>
#include <vector>
#include <string>
#include <cmath>

#include "HRGBase.h"
#include "HRGEV.h"
#include "HRGVDW.h"
#include "HRGFit.h"

#include "ThermalFISTConfig.h"

#include "CosmicEos/CosmicEoS.h"


using namespace std;
using namespace thermalfist;


int main(int argc, char *argv[])
{
  // Baryon asymmetry
  double b = 8.6e-11;
  // Electric charge asymmetry
  double q = 0.0;

  // Electron flavor asymmetry
  double le = 0.0;
  if (argc > 1)
    le = atof(argv[1]);

  // Muon flavor asymmetry
  double lmu = 0.0;
  if (argc > 2)
    lmu = atof(argv[2]);

  // Tau flavor asymmetry
  double ltau = 0.0;
  if (argc > 3)
    ltau = atof(argv[3]);

  // If very small, set to zero
  if (fabs(le) < 1.e-13)
    le = 0.0;
  if (fabs(lmu) < 1.e-13)
    lmu = 0.0;
  if (fabs(ltau) < 1.e-13)
    ltau = 0.0;


  // Particle list for the HRG model
  ThermalParticleSystem parts(string(ThermalFIST_INPUT_FOLDER) + "/list/PDG2020/list.dat");

  // HRG model
  ThermalModelIdeal modelHRG(&parts);

  // Resonance widths
  bool useWidth = false;
  modelHRG.SetUseWidth(useWidth);

  // Quantum statistics
  bool useQStats = true;
  modelHRG.SetStatistics(useQStats);

  // Effective mass model for pions (needed for BEC)
  bool interactingpions = true;

  // Cosmic EoS object
  CosmicEoS cosmos(&modelHRG, interactingpions);

  cout << "le   = " << setw(15) << le << endl;
  cout << "lmu  = " << setw(15) << lmu << endl;
  cout << "ltau = " << setw(15) << ltau << endl;

  cosmos.SetAsymmetries(vector<double>({ b, 0., le, lmu, ltau }));

  // Range of temperatures: Tmax = 180 MeV down to Tmin = 0.01 MeV with dT = 0.01 MeV (17,999 points)
  double Tmin = 0.00001; // 0.01 MeV (in GeV)
  double Tmax = 0.18000; // 180 MeV (in GeV)
  double dT   = 0.00001; // 0.01 MeV (in GeV)
  vector<double> Temps;
  for (double tT = Tmax; tT >= Tmin - 0.1 * dT; tT -= dT) {
    Temps.push_back(tT);
  }

  // Output file
  string filename = "CosmicTrajectoryDensities.dat";
  ofstream fout(filename);

  // Header
  fout << setw(15) << "T[MeV]" << " ";
  fout << setw(15) << "muB[MeV]" << " ";
  fout << setw(15) << "muQ[MeV]" << " ";
  fout << setw(15) << "muS[MeV]" << " ";
  fout << setw(15) << "nB" << " ";
  fout << setw(15) << "nS" << " ";
  fout << setw(15) << "n_proton" << " ";
  fout << setw(15) << "n_neutron" << " ";
  fout << setw(15) << "n_Lambda" << " ";
  fout << setw(15) << "n_SigmaP" << " ";
  fout << setw(15) << "n_Sigma0" << " ";
  fout << setw(15) << "n_SigmaM" << " ";
  fout << setw(15) << "n_Xi0" << " ";
  fout << setw(15) << "n_XiM" << " ";
  fout << setw(15) << "n_OmegaM" << " ";
  fout << setw(15) << "nN/nB" << " ";
  fout << setw(15) << "nLam/nB" << " ";
  fout << setw(15) << "nS/nB" << " ";
  fout << setw(15) << "nSig/nB" << " ";
  fout << setw(15) << "nXi/nB" << " ";
  fout << setw(15) << "nOm/nB" << " ";
  fout << endl;

  // On-screen header
  cout << setw(10) << "T[MeV]"
       << setw(15) << "muB[MeV]"
       << setw(15) << "nB"
       << setw(15) << "nN/nB"
       << setw(15) << "nLam/nB"
       << setw(15) << "nS/nB"
       << endl;

  // Initial guess
  vector<double> prev = vector<double>({ 0.700, -1.e-7, -1.e-7, -1.e-7, -1.e-7 });

  // Loop over temperatures
  for (auto&& T : Temps) {
    vector<double> chems = cosmos.SolveChemicalPotentials(T, prev);
    prev = chems;

    if (!interactingpions && abs(chems[1]) > 0.139)
      break;

    // After SolveChemicalPotentials, the HRG model is set and densities computed
    ThermalModelBase* hrg = cosmos.HRGModel();

    // Net baryon density from the cosmos object
    double nB = cosmos.BaryonDensity();

    // Net strangeness density
    double nS_net = hrg->StrangenessDensity();

    // Get individual net (particle - antiparticle) densities using PDG codes
    double n_proton  = hrg->GetDensity(2212, Feeddown::Primordial) - hrg->GetDensity(-2212, Feeddown::Primordial);
    double n_neutron = hrg->GetDensity(2112, Feeddown::Primordial) - hrg->GetDensity(-2112, Feeddown::Primordial);
    double n_Lambda  = hrg->GetDensity(3122, Feeddown::Primordial) - hrg->GetDensity(-3122, Feeddown::Primordial);
    double n_SigmaP  = hrg->GetDensity(3222, Feeddown::Primordial) - hrg->GetDensity(-3222, Feeddown::Primordial);
    double n_Sigma0  = hrg->GetDensity(3212, Feeddown::Primordial) - hrg->GetDensity(-3212, Feeddown::Primordial);
    double n_SigmaM  = hrg->GetDensity(3112, Feeddown::Primordial) - hrg->GetDensity(-3112, Feeddown::Primordial);
    double n_Xi0     = hrg->GetDensity(3322, Feeddown::Primordial) - hrg->GetDensity(-3322, Feeddown::Primordial);
    double n_XiM     = hrg->GetDensity(3312, Feeddown::Primordial) - hrg->GetDensity(-3312, Feeddown::Primordial);
    double n_OmegaM  = hrg->GetDensity(3334, Feeddown::Primordial) - hrg->GetDensity(-3334, Feeddown::Primordial);

    // Composite quantities
    double nN   = n_proton + n_neutron;
    double nLam = n_Lambda;
    double nSig = n_SigmaP + n_Sigma0 + n_SigmaM;
    double nXi  = n_Xi0 + n_XiM;
    double nOm  = n_OmegaM;

    // Ratios
    double nN_nB   = (nB != 0.) ? nN / nB   : 0.;
    double nLam_nB = (nB != 0.) ? nLam / nB : 0.;
    double nS_nB   = (nB != 0.) ? nS_net / nB : 0.;
    double nSig_nB = (nB != 0.) ? nSig / nB : 0.;
    double nXi_nB  = (nB != 0.) ? nXi / nB  : 0.;
    double nOm_nB  = (nB != 0.) ? nOm / nB  : 0.;

    // Get muS from the model
    double muS = hrg->Parameters().muS;

    // Write to file
    fout << setw(15) << T * 1.e3 << " ";
    fout << setw(15) << chems[0] * 1.e3 << " ";
    fout << setw(15) << chems[1] * 1.e3 << " ";
    fout << setw(15) << muS * 1.e3 << " ";
    fout << setw(15) << nB << " ";
    fout << setw(15) << nS_net << " ";
    fout << setw(15) << n_proton << " ";
    fout << setw(15) << n_neutron << " ";
    fout << setw(15) << n_Lambda << " ";
    fout << setw(15) << n_SigmaP << " ";
    fout << setw(15) << n_Sigma0 << " ";
    fout << setw(15) << n_SigmaM << " ";
    fout << setw(15) << n_Xi0 << " ";
    fout << setw(15) << n_XiM << " ";
    fout << setw(15) << n_OmegaM << " ";
    fout << setw(15) << nN_nB << " ";
    fout << setw(15) << nLam_nB << " ";
    fout << setw(15) << nS_nB << " ";
    fout << setw(15) << nSig_nB << " ";
    fout << setw(15) << nXi_nB << " ";
    fout << setw(15) << nOm_nB << " ";
    fout << endl;

    // On-screen
    cout << setw(10) << T * 1.e3
         << setw(15) << chems[0] * 1.e3
         << setw(15) << nB
         << setw(15) << nN_nB
         << setw(15) << nLam_nB
         << setw(15) << nS_nB
         << endl;
  }

  cout << "Output written to " << filename << endl;

  return 0;
}
