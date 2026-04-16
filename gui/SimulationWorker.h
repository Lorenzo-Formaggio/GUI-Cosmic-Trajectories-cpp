#ifndef SIMULATIONWORKER_H
#define SIMULATIONWORKER_H

#include <QObject>
#include <QString>
#include <vector>
#include <atomic>

/**
 * @brief Holds per-step results emitted to the GUI.
 */
struct TrajectoryPoint {
  double T;
  double muB;
  double muQ;
  double munue;
  double munumu;
  double mnutau;
  double nB;
  double nQ; // nQ_QCD
  double s;  // s_tot
  double s_QCD;
  double ne, nmu, ntau;
  double nnue, nnumu, nnutau;
  // Residual errors
  double err_b, err_charge, err_le, err_lmu, err_ltau;
};

/**
 * @brief Runs the CosmicTrajectory simulation in a background thread.
 *
 * Mirrors the logic of CosmicTrajectory.cpp but emits Qt signals so the
 * GUI can update live.
 */
class SimulationWorker : public QObject {
  Q_OBJECT

public:
  // Simulation parameters (set before starting)
  double b = 8.6e-11;
  double le = -0.01;
  double lmu = -0.01;
  double ltau = -0.01;
  double dT = 1.0;
  double Tmin = 30.0;
  double Tmax = 2000.0;
  int nf = 4;
  int eos = 0;
  int guessMethod = 0;
  int scanDirection = 0;
  QString eosTableFilePath;

  // Working directory (for finding LatticeEoS/, EoS_Table.txt)
  QString workingDir;

public slots:
  /**
   * @brief Execute the full simulation. Call from a QThread.
   */
  void run();

  /**
   * @brief Requests the simulation to stop. Thread-safe.
   */
  void stop() { m_stopRequested = true; }

signals:
  /** Emitted once per temperature step with the computed values. */
  void stepCompleted(TrajectoryPoint point);

  /** Emitted with log messages for the console panel. */
  void logMessage(const QString &msg);

  /** Progress from 0 to 100. */
  void progressUpdated(int percent);

  /** Emitted when the simulation finishes successfully. */
  void simulationFinished();

  /** Emitted on error. */
  void simulationError(const QString &errorMsg);

private:
  std::atomic<bool> m_stopRequested{false};
};

#endif // SIMULATIONWORKER_H
