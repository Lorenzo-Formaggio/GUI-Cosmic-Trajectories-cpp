#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "SimulationWorker.h"

#include <QMainWindow>
#include <QThread>
#include <QVector>

// Forward declarations (Qt)
class QDoubleSpinBox;
class QComboBox;
class QPushButton;
class QProgressBar;
class QSpinBox;
class QTextEdit;
class QTabWidget;
class QLabel;
class QCheckBox;
class QLineEdit;

#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QLogValueAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QAbstractAxis>
#include <QtCharts/QScatterSeries>
#include "TooltipChartView.h"

/**
 * @brief Main application window.
 *
 * Left panel: parameter inputs + run button + console.
 * Right panel: tabbed chart views (densities, chem. pots, lepton chem. pots).
 */
class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

private slots:
  void onRunClicked();
  void onStopClicked();
  void onStepCompleted(TrajectoryPoint point);
  void onSimulationFinished();
  void onSimulationError(const QString &msg);
  void onLogMessage(const QString &msg);
  void onProgressUpdated(int pct);
  void onEosChanged(int index);
  void onThemeToggleClicked();
  void onAxisToggleClicked();
  void onScaleToggleClicked();
  void onExportClicked();
  void onExportFullDataClicked();
  void onCriticalPointButtonClicked();
  void updateCriticalPoint();
  void onSolverSettingsButtonClicked();

private:
  void setupUi();
  void setupStyle();
  void createParameterPanel(QWidget *parent);
  void createChartPanel(QWidget *parent);
  void createConsolePanel(QWidget *parent);
  void clearCharts(bool keepData = false);
  void replotData();
  void updateChartAxes();
  void updateAxesTypes();

  // ── Parameter widgets ───────────────────────────────────────────────
  QDoubleSpinBox *m_spinB;
  QDoubleSpinBox *m_spinLe;
  QDoubleSpinBox *m_spinLmu;
  QDoubleSpinBox *m_spinLtau;
  QDoubleSpinBox *m_spinDT;
  QDoubleSpinBox *m_spinTmin;
  QDoubleSpinBox *m_spinTmax;
  QComboBox      *m_comboNf;
  QComboBox      *m_comboEos;
  QComboBox      *m_comboGuess;
  QComboBox      *m_comboScan;


  QWidget        *m_eosPathWidget;
  QLineEdit      *m_lineEditEosPath;
  QPushButton    *m_btnBrowseEos;
  QLabel         *m_labelEosPath;

  // ── Critical point widgets ──────────────────────────────────────────
  QDoubleSpinBox *m_spinCpT   = nullptr;
  QDoubleSpinBox *m_spinCpMuB = nullptr;
  QDoubleSpinBox *m_spinCpMuQ = nullptr;
  QCheckBox      *m_chkShowCp = nullptr;

  // ── Control widgets ─────────────────────────────────────────────────
  QPushButton    *m_btnRun;
  QPushButton    *m_btnStop;
  QPushButton    *m_btnCriticalPoint;
  QPushButton    *m_btnSolverSettings;
  QPushButton    *m_btnShowHide = nullptr;
  QDialog        *m_cpDialog = nullptr;
  QDialog        *m_solverSettingsDialog = nullptr;
  QDialog        *m_visDialog = nullptr;
  QProgressBar *m_progressBar;
  QTextEdit    *m_console;
  QLabel       *m_statusLabel;
  QPushButton  *m_btnThemeToggle;
  QPushButton  *m_btnAxisToggle;
  QPushButton  *m_btnScaleToggle;
  QPushButton  *m_btnExportFullData;

  // ── Series visibility checkboxes ────────────────────────────────────
  QCheckBox *m_chknB;
  QCheckBox *m_chkS;
  QCheckBox *m_chknQ;
  QCheckBox *m_chkNnue;
  QCheckBox *m_chkNnumu;
  QCheckBox *m_chkNnutau;
  QCheckBox *m_chkNe;
  QCheckBox *m_chkNmu;
  QCheckBox *m_chkNtau;
  QCheckBox *m_chkMuB;
  QCheckBox *m_chkMuQ;
  QCheckBox *m_chkMunue;
  QCheckBox *m_chkMunumu;
  QCheckBox *m_chkMnutau;
  QCheckBox *m_chkErrB, *m_chkErrQ, *m_chkErrLe, *m_chkErrLmu, *m_chkErrLtau;

  // ── Chart widgets ───────────────────────────────────────────────────
  QTabWidget *m_chartTabs;

  // Tab 1: Densities
  TooltipChartView *m_densityChartView;
  QLineSeries *m_seriesnB;
  QLineSeries *m_seriesS;
  QLineSeries *m_seriesnQ;
  QLineSeries *m_seriesNnue;
  QLineSeries *m_seriesNnumu;
  QLineSeries *m_seriesNnutau;

  // Tab 4: Lepton Densities
  TooltipChartView *m_leptonDensChartView;
  QLineSeries *m_seriesNe;
  QLineSeries *m_seriesNmu;
  QLineSeries *m_seriesNtau;

  // Tab 2: Baryon & Charge μ
  TooltipChartView *m_muChartView;
  QLineSeries *m_seriesMuB;
  QLineSeries *m_seriesMuQ;
  QScatterSeries *m_seriesCpB = nullptr;
  QScatterSeries *m_seriesCpQ = nullptr;

  // Tab 3: Lepton μ
  TooltipChartView *m_leptonChartView;
  QLineSeries *m_seriesMunue;
  QLineSeries *m_seriesMunumu;
  QLineSeries *m_seriesMnutau;

  // Tab 4: Residual Errors
  TooltipChartView *m_errorChartView;
  QLineSeries *m_seriesErrB, *m_seriesErrQ, *m_seriesErrLe, *m_seriesErrLmu, *m_seriesErrLtau;

  // Axes
  QAbstractAxis *m_densAxisX, *m_densAxisY;
  QAbstractAxis *m_muAxisX,   *m_muAxisY;
  QAbstractAxis *m_lepAxisX,  *m_lepAxisY;
  QAbstractAxis *m_lepDensAxisX, *m_lepDensAxisY;
  QAbstractAxis *m_errAxisX,  *m_errAxisY;

  // Linear/Log state
  bool m_isLogScale = true;

  // ── Worker thread ───────────────────────────────────────────────────
  QThread           *m_workerThread = nullptr;
  SimulationWorker  *m_worker       = nullptr;

  // Data storage for axis range computation
  QVector<TrajectoryPoint> m_trajectoryData;
  bool m_tempIsVertical = true;

  // Global custom guess settings
  int m_initialGuessType = 0; // 0 = Standard, 1 = Custom
  std::vector<double> m_customGuessLowHigh = {0.01, -0.001, -1e-05, -1e-05, -1e-05};
  std::vector<double> m_customGuessHighLow = {1.0, -0.1, -0.1, -0.1, -0.1};

  // Solver settings state
  double m_tolerance = 1e-6;
  int m_maxIter = 100;
  int m_guessMethod = 0; // 0 = Simple, 1 = Linear Extrap

  // Metropolis optimizer state (0=off, 1=first step only, 2=always retry)
  int    m_metropolisMode      = 0;
  int    m_metropolisSteps     = 500;
  double m_metropolisStepSigma = 1.0;
  double m_metropolisT         = 0.01;
};

#endif // MAINWINDOW_H
