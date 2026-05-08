#include "SimulationWorker.h"

#include "include/GetEq.hpp"
#include "include/JEL.hpp"
#include "include/InterpolatedEoS.hpp"
#include "include/QCDTherm.hpp"
#include "include/Solver.hpp"
#include "include/Metropolis.hpp"
#include "include/TRangeManager.hpp"
#include "include/leptonTherm.hpp"

#include <QMutexLocker>
#include <QDir>
#include <cmath>
#include <sstream>
#include <fstream>

QMutex SimulationWorker::physicsMutex;

void SimulationWorker::run() {
  QMutexLocker locker(&physicsMutex);
  m_stopRequested = false;
  try {
    // ── Initialization ────────────────────────────────────────────────────
    emit logMessage("Initializing JEL tables...");
    initializeJELTables();

    // Build base path for EoS data relative to the project root
    std::string baseDir = workingDir.toStdString();
    std::string eosTablePath = eosTableFilePath.isEmpty() ? (baseDir + "/EoS_Table.txt") : eosTableFilePath.toStdString();

    // Validate and clamp temperature range based on EoS
    auto range = TRange::validateAndClamp(eos, eosTablePath, Tmin, Tmax, [this](const std::string& msg) {
        emit logMessage(QString::fromStdString(msg));
    });
    double effectiveTmin = range.Tmin;
    double effectiveTmax = range.Tmax;

    // Set the EoS
    std::string eosName;
    if (eos == 0) eosName = "Free QGP";
    else if (eos == 1) eosName = "Lattice QCD";
    else if (eos == 2) eosName = "Interpolated Table";
    else if (eos == 3) eosName = "Entropy Contour";
    else eosName = "Unknown";

    emit logMessage(QString("Setting EoS: %1").arg(QString::fromStdString(eosName)));
    QCD::setEoS(eos, baseDir, nf);

    // ── Prepare scan range ────────────────────────────────────────────────
    if (nf == 4 && eos == 1)
      effectiveTmax = 330;

    double Tstart = (scanDirection == 0) ? effectiveTmin : effectiveTmax;
    double Tend   = (scanDirection == 0) ? effectiveTmax : effectiveTmin;
    double Tstep  = (scanDirection == 0) ? dT : -dT;

    int totalSteps = static_cast<int>(std::abs(Tend - Tstart) / dT) + 1;
    int currentStep = 0;

    emit logMessage(QString("Scanning T = %1 → %2 MeV  (step %3 MeV, %4 steps)")
                        .arg(Tstart, 0, 'f', 1)
                        .arg(Tend, 0, 'f', 1)
                        .arg(dT, 0, 'f', 2)
                        .arg(totalSteps));
    emit logMessage("─────────────────────────────────────────");

    // ── Initial guess ─────────────────────────────────────────────────────
    std::vector<double> guess;
    if (initialGuessType == 1) { // Custom Guess
      if (scanDirection == 0) {
        guess = customGuessLowHigh;
      } else {
        guess = customGuessHighLow;
      }
    } else { // Standard Guess
      if (scanDirection == 0) {
        guess = {0.01, -0.001, -1e-05, -1e-05, -1e-05};
      } else {
        guess = {1.0, -0.1, -0.1, -0.1, -0.1};
      }
    }
    std::vector<double> targets = {0.0, 0.0, 0.0, 0.0, 0.0};

    std::vector<double> prev_solution;
    bool has_prev_solution = false;

    // Also write trajectory.txt and errors.txt in the working directory
    std::string trajPath = workingDir.toStdString() + "/trajectory.txt";
    std::string errPath  = workingDir.toStdString() + "/errors.txt";
    std::ofstream outfile(trajPath);
    std::ofstream errfile(errPath);

    if (outfile.is_open()) {
      outfile << "EoS=" << eos << "Flavors= " << nf << " Parameters: b = " << b
              << ", le = " << le << ", lmu = " << lmu << ", ltau = " << ltau
              << std::endl;
      outfile << "T[MeV] muB[MeV] muQ[MeV] munue[MeV] munumu[MeV] mnutau[MeV] "
                 "nB[MeV^3] nQ[MeV^3] s[MeV^3] "
                 "nnue[MeV^3] nnumu[MeV^3] nnutau[MeV^3]"
              << std::endl;
    }
    if (errfile.is_open()) {
      errfile << "# Relative errors: (target - calculated)/target" << std::endl;
      errfile << "T[MeV] err_b err_charge err_le err_lmu err_ltau" << std::endl;
    }

    if (b == 0.0 || le == 0.0 || lmu == 0.0 || ltau == 0.0) {
      emit logMessage("<font color='#ffc107'><b>Warning:</b> One or more target asymmetries are 0.</font>");
      emit logMessage("<font color='#ffc107'>Absolute error is used instead of relative error for those variables to avoid division by zero.</font>");
    }

    // ── Main simulation loop ──────────────────────────────────────────────
    bool firstStep = true;
    for (double T = Tstart;
         (scanDirection == 0) ? (T <= Tend) : (T >= Tend);
         T += Tstep) {

      if (m_stopRequested) {
        emit logMessage("🛑 Simulation stopped by user.");
        break;
      }

      std::vector<Solver::SystemFunction> functions =
          GetEq::getEquations(T, le, lmu, ltau, b, nf);

      // ── Attempt solve, with optional Metropolis retry ─────────────────
      std::vector<double> solution;
      bool solved = false;

      auto tryMetropolis = [&](const QString &reason) {
        emit logMessage(QString("<font color='#ffc107'><b>Solver failed at T=%1 MeV</b> (%2). "
                                "Running Metropolis optimizer (%3 steps, σ=%4, T_m=%5)...</font>")
                            .arg(T, 0, 'f', 1).arg(reason)
                            .arg(metropolisSteps)
                            .arg(metropolisStepSigma, 0, 'g', 3)
                            .arg(metropolisT, 0, 'g', 3));

        std::vector<double> bestGuess = Metropolis::optimize(
            functions, targets, guess,
            metropolisSteps, metropolisStepSigma, metropolisT,
            tolerance, maxIter);

        double bestCost = Metropolis::computeCost(functions, targets, bestGuess, tolerance, maxIter);

        emit logMessage(QString("  → Metropolis best cost: <b>%1</b>. Retrying solver...")
                            .arg(bestCost, 0, 'e', 3));
        try {
          solution = Solver::solveSystem(functions, targets, bestGuess, tolerance, maxIter);
          guess = bestGuess; // keep as new starting guess for next step
          solved = true;
          emit logMessage(QString("<font color='#28a745'>  ✓ Solver converged after Metropolis at T=%1 MeV.</font>").arg(T, 0, 'f', 1));
        } catch (const std::exception &e2) {
          emit logMessage(QString("<font color='#dc3545'>  ✗ Solver still failed after Metropolis at T=%1 MeV: %2. Skipping step.</font>")
                              .arg(T, 0, 'f', 1).arg(e2.what()));
        }
      };

      try {
        solution = Solver::solveSystem(functions, targets, guess, tolerance, maxIter);
        solved = true;
      } catch (const std::exception &e) {
        // Decide whether to run Metropolis based on the mode
        bool runMetro = false;
        if (metropolisMode == 1 && firstStep) runMetro = true;
        else if (metropolisMode == 2)          runMetro = true;

        if (runMetro) {
          tryMetropolis(QString::fromStdString(e.what()));
        } else {
          emit logMessage(QString("<font color='#dc3545'><b>Solver failed at T=%1 MeV:</b> %2. Skipping step.</font>")
                              .arg(T, 0, 'f', 1).arg(e.what()));
        }
      }

      firstStep = false;

      if (!solved) {
        currentStep++;
        emit progressUpdated(std::min(static_cast<int>(100.0 * currentStep / totalSteps), 100));
        continue;
      }

      double muB_sol    = solution[0];
      double muQ_sol    = solution[1];
      double munue_sol  = solution[2];
      double munumu_sol = solution[3];
      double mnutau_sol = solution[4];

      double nB_val  = QCD::BarDens(muB_sol, muQ_sol, T, nf);
      double nQ_val  = QCD::QCDcharge(muB_sol, muQ_sol, T, nf);
      double s_val   = GetEq::TotalS(muB_sol, muQ_sol, munue_sol, munumu_sol, mnutau_sol, T, nf);
      double sQCD    = QCD::sQCD(muB_sol, muQ_sol, T, nf);

      double ne_val   = jelf::nNet(munue_sol - muQ_sol, T, lepton::me, lepton::ge);
      double nmu_val  = jelf::nNet(munumu_sol - muQ_sol, T, lepton::mmu, lepton::gmu);
      double ntau_val = jelf::nNet(mnutau_sol - muQ_sol, T, lepton::mtau, lepton::gtau);
      
      double nnue_val  = jelf::nNet(munue_sol, T, lepton::m_nue, lepton::gnu);
      double nnumu_val = jelf::nNet(munumu_sol, T, lepton::m_numu, lepton::gnu);
      double nnutau_val = jelf::nNet(mnutau_sol, T, lepton::m_nutau, lepton::gnu);

      // Emit the point to the GUI
      TrajectoryPoint pt;
      pt.T      = T;
      pt.muB    = muB_sol;
      pt.muQ    = muQ_sol;
      pt.munue  = munue_sol;
      pt.munumu = munumu_sol;
      pt.mnutau = mnutau_sol;
      pt.nB     = nB_val;
      pt.nQ     = nQ_val;
      pt.s      = s_val;
      pt.s_QCD  = sQCD;
      pt.ne     = ne_val;
      pt.nmu    = nmu_val;
      pt.ntau   = ntau_val;
      pt.nnue   = nnue_val;
      pt.nnumu  = nnumu_val;
      pt.nnutau = nnutau_val;

      // Errors
      GetEq::ErrorValues err = GetEq::getErrors(solution, T, le, lmu, ltau, b, nf);
      pt.err_b      = err.err_b;
      pt.err_charge = err.err_charge;
      pt.err_le     = err.err_le;
      pt.err_lmu    = err.err_lmu;
      pt.err_ltau   = err.err_ltau;

      emit stepCompleted(pt);

      // Write to files
      if (outfile.is_open()) {
        outfile << T << " " << muB_sol << " " << muQ_sol << " "
                << munue_sol << " " << munumu_sol << " " << mnutau_sol << " "
                << nB_val << " " << nQ_val << " " << s_val << " "
                << nnue_val << " " << nnumu_val << " " << nnutau_val << std::endl;
      }
      if (errfile.is_open()) {
        errfile << T << " " << err.err_b << " " << err.err_charge << " "
                << err.err_le << " " << err.err_lmu << " " << err.err_ltau
                << std::endl;
      }

      // Update guess
      if (guessMethod == 1 && has_prev_solution) {
        for (size_t i = 0; i < solution.size(); ++i) {
          guess[i] = prev_solution[i] + 2 * (solution[i] - prev_solution[i]);
        }
      } else {
        guess = solution;
      }
      prev_solution = solution;
      has_prev_solution = true;

      // Progress
      currentStep++;
      int pct = static_cast<int>(100.0 * currentStep / totalSteps);
      emit progressUpdated(std::min(pct, 100));

      // Log every ~10% or so
      if (currentStep % std::max(1, totalSteps / 20) == 0) {
        emit logMessage(QString("  T = %1 MeV   muB = %2")
                            .arg(T, 8, 'f', 1)
                            .arg(muB_sol, 12, 'e', 4));
      }
    }

    outfile.close();
    errfile.close();

    // Cleanup
    QCD::cleanup();

    emit logMessage("─────────────────────────────────────────");
    emit logMessage("✓ Simulation complete.");
    emit logMessage(QString("  Output saved to: %1").arg(QString::fromStdString(trajPath)));
    emit progressUpdated(100);
    emit simulationFinished();

  } catch (const std::exception &e) {
    QCD::cleanup();
    emit simulationError(QString("Simulation failed: %1").arg(e.what()));
  }
}
