#ifndef RUNFROMFILEWIDGET_H
#define RUNFROMFILEWIDGET_H

#include "SimulationWorker.h"

#include <QWidget>
#include <QVector>
#include <QColor>
#include <QString>

// Forward declarations (Qt)
class QDoubleSpinBox;
class QSpinBox;
class QComboBox;
class QPushButton;
class QLabel;
class QTabWidget;
class QTableWidget;
class QTextEdit;
class QThread;
class QProgressBar;
class QDialog;
class QCheckBox;

#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QLogValueAxis>
#include <QtCharts/QAbstractAxis>
#include "TooltipChartView.h"

/**
 * @brief A single (b, le, lmu, ltau) record read from a parameter file.
 */
struct TrajParamRow {
  double b;
  double le;
  double lmu;
  double ltau;
};

/**
 * @brief One "source" = one parameter file + EoS choice + plot style.
 *
 * A source can contain many parameter rows, each producing one trajectory.
 * All trajectories of a source share the same EoS, line style, and color.
 */
struct RunSource {
  // ── Configuration (set from the table) ──────────────────────────────
  QString filePath;
  int eos = 0;               // 0 Free QGP, 1 Lattice QCD, 2 Interpolated, 3 Entropy Contour
  int nf = 3;
  QString eosTablePath;      // only used for eos == 2
  Qt::PenStyle penStyle = Qt::SolidLine;  // SolidLine / DashLine / DotLine
  // First (start) and last (end) trajectory colors. Intermediate trajectories
  // get an RGB-linear interpolation between the two.
  QColor color    = QColor(31, 119, 180);
  QColor colorEnd = QColor(31, 119, 180);

  // ── Parsed input ─────────────────────────────────────────────────────
  QVector<TrajParamRow> rows;

  // ── Per-row results and chart series ─────────────────────────────────
  QVector<QVector<TrajectoryPoint>> data;
  QVector<QLineSeries*> series_nB;
  QVector<QLineSeries*> series_s;
  QVector<QLineSeries*> series_nQ;
  QVector<QLineSeries*> series_ne;
  QVector<QLineSeries*> series_nmu;
  QVector<QLineSeries*> series_ntau;
  QVector<QLineSeries*> series_nnue;
  QVector<QLineSeries*> series_nnumu;
  QVector<QLineSeries*> series_nnutau;
  QVector<QLineSeries*> series_muB;
  QVector<QLineSeries*> series_muQ;
  QVector<QLineSeries*> series_munue;
  QVector<QLineSeries*> series_munumu;
  QVector<QLineSeries*> series_mnutau;
};

/**
 * @brief Run trajectories from one or more parameter files.
 *
 * - Each row in the source table is a "source" that pairs a parameter file
 *   (formatted like input/input_traj.txt: lines of "b le lmu ltau") with an
 *   EoS, line style, and RGB color.
 * - The user sets common Tmin/Tmax/dT/guess/solver settings.
 * - "Run All" iterates every source's parameter rows sequentially, running a
 *   full trajectory per row, plotting them as overlays on shared charts.
 */
class RunFromFileWidget : public QWidget {
  Q_OBJECT
public:
  explicit RunFromFileWidget(const QString &workingDir, QWidget *parent = nullptr);
  ~RunFromFileWidget() override;

private slots:
  void onAddSource();
  void onRemoveSource();
  void onBrowseFile(int row);
  void onBrowseEosTable(int row);
  void onPickColor(int row, bool isEnd);
  void onTableEosChanged(int row, int newIndex);
  void onRunAll();
  void onStopAll();
  void onClearAll();
  void onSolverSettingsClicked();
  void onScaleToggle();
  void onAxisToggle();
  void onThemeToggle();
  void onExportFullData();
  void onExportActivePlot();
  void onAxisLimitsClicked();
  void updateSeriesVisibility();

private:
  void setupUi();
  void createChartPanel(QWidget *parent);
  void addSourceRow(const RunSource &src);
  void readRowIntoSource(int row);
  bool parseParamFile(const QString &path, QVector<TrajParamRow> &out, QString &err);
  void startNextTrajectory();
  void onWorkerStepCompleted(TrajectoryPoint pt);
  void onWorkerFinished();
  void onWorkerError(const QString &msg);
  void onWorkerLog(const QString &msg);
  void clearAllSeriesAndData();
  void replotAll();
  void updateChartAxes();
  void logMessage(const QString &msg, int sourceIdx = -1, int rowIdx = -1);
  void setSourceStatus(int sourceIdx, const QString &text, const QString &cssColor = "gray");
  void teardownWorker();
  // Color helpers
  QColor interpolatedColor(const RunSource &src, int rowIdx, int total) const;
  void   refreshSourceSeriesColors(int sourceIdx);

  // Abs/legend helpers
  double absVal(double v, bool useAbs) const;
  QString legendSuffix(const TrajParamRow &row) const;
  void refreshSeriesNames();
  void refreshLegendVisibility();

  QString m_workingDir;
  QVector<RunSource> m_sources;

  // ── Common parameters ───────────────────────────────────────────────
  QDoubleSpinBox *m_spinTmin = nullptr;
  QDoubleSpinBox *m_spinTmax = nullptr;
  QDoubleSpinBox *m_spinDT   = nullptr;
  QComboBox      *m_comboScan  = nullptr;
  QComboBox      *m_comboGuess = nullptr;

  // Solver state (shared)
  double m_tolerance = 1e-6;
  int    m_maxIter   = 100;
  int    m_initialGuessType = 0;
  std::vector<double> m_customGuessLowHigh = {0.01, -0.001, -1e-05, -1e-05, -1e-05};
  std::vector<double> m_customGuessHighLow = {1.0, -0.1, -0.1, -0.1, -0.1};
  int    m_metropolisMode      = 0;
  int    m_metropolisSteps     = 500;
  double m_metropolisStepSigma = 1.0;
  double m_metropolisT         = 0.01;

  // ── UI widgets ──────────────────────────────────────────────────────
  QTableWidget *m_table = nullptr;
  QPushButton  *m_btnAdd    = nullptr;
  QPushButton  *m_btnRemove = nullptr;
  QPushButton  *m_btnRun    = nullptr;
  QPushButton  *m_btnStop   = nullptr;
  QPushButton  *m_btnClear  = nullptr;
  QPushButton  *m_btnSolverSettings = nullptr;
  QPushButton  *m_btnScaleToggle    = nullptr;
  QPushButton  *m_btnAxisToggle     = nullptr;
  QPushButton  *m_btnThemeToggle    = nullptr;
  QPushButton  *m_btnExportFullData = nullptr;
  QPushButton  *m_btnExportPlot     = nullptr;
  QPushButton  *m_btnAxisLimits     = nullptr;
  QDialog      *m_axisLimitsDialog  = nullptr;

  // Visibility Dialog
  QPushButton  *m_btnShowHide = nullptr;
  QDialog      *m_visDialog   = nullptr;

  // Series visibility checkboxes (apply to all sources)
  QCheckBox    *m_chknB     = nullptr;
  QCheckBox    *m_chkS      = nullptr;
  QCheckBox    *m_chknQ     = nullptr;
  QCheckBox    *m_chkNe     = nullptr;
  QCheckBox    *m_chkNmu    = nullptr;
  QCheckBox    *m_chkNtau   = nullptr;
  QCheckBox    *m_chkNnue   = nullptr;
  QCheckBox    *m_chkNnumu  = nullptr;
  QCheckBox    *m_chkNnutau = nullptr;
  QCheckBox    *m_chkMuB    = nullptr;
  QCheckBox    *m_chkMuQ    = nullptr;
  QCheckBox    *m_chkMunue  = nullptr;
  QCheckBox    *m_chkMunumu = nullptr;
  QCheckBox    *m_chkMnutau = nullptr;

  // Per-quantity abs checkboxes (mirror visibility set)
  QCheckBox    *m_absnB     = nullptr;
  QCheckBox    *m_absS      = nullptr;
  QCheckBox    *m_absnQ     = nullptr;
  QCheckBox    *m_absNe     = nullptr;
  QCheckBox    *m_absNmu    = nullptr;
  QCheckBox    *m_absNtau   = nullptr;
  QCheckBox    *m_absNnue   = nullptr;
  QCheckBox    *m_absNnumu  = nullptr;
  QCheckBox    *m_absNnutau = nullptr;
  QCheckBox    *m_absMuB    = nullptr;
  QCheckBox    *m_absMuQ    = nullptr;
  QCheckBox    *m_absMunue  = nullptr;
  QCheckBox    *m_absMunumu = nullptr;
  QCheckBox    *m_absMnutau = nullptr;

  // Per-quantity abs flags (defaults match previous "|·|" labels)
  bool m_useAbsnB     = false;
  bool m_useAbsS      = false;
  bool m_useAbsnQ     = true;
  bool m_useAbsNe     = false;
  bool m_useAbsNmu    = false;
  bool m_useAbsNtau   = false;
  bool m_useAbsNnue   = false;
  bool m_useAbsNnumu  = false;
  bool m_useAbsNnutau = false;
  bool m_useAbsMuB    = true;
  bool m_useAbsMuQ    = true;
  bool m_useAbsMunue  = true;
  bool m_useAbsMunumu = true;
  bool m_useAbsMnutau = true;

  // Legend customization
  bool m_legendVisible       = true;
  bool m_legendShowSourceTag = true;   // include the "S<i>" prefix
  bool m_legendShowB         = false;
  bool m_legendShowLe        = false;
  bool m_legendShowLmu       = false;
  bool m_legendShowLtau      = false;

  // Manual axis limits (one entry per chart-axis); autoRange=true defers to data.
  struct AxisLimit { bool autoRange = true; double lo = 0.0; double hi = 1.0; };
  AxisLimit m_densX, m_densY;
  AxisLimit m_lepDensX, m_lepDensY;
  AxisLimit m_muX,   m_muY;
  AxisLimit m_lepX,  m_lepY;
  QTextEdit    *m_console = nullptr;
  QLabel       *m_statusLabel = nullptr;
  QProgressBar *m_progressBar = nullptr;
  QDialog      *m_solverDialog = nullptr;

  // ── Run queue (sequential execution) ───────────────────────────────
  struct QueueEntry { int sourceIdx; int rowIdx; };
  QVector<QueueEntry> m_runQueue;
  int  m_queueIndex = 0;
  bool m_running    = false;
  bool m_stopRequested = false;
  QThread          *m_workerThread = nullptr;
  SimulationWorker *m_worker       = nullptr;
  // Active series targets for currently running trajectory
  int m_currentSourceIdx = -1;
  int m_currentRowIdx    = -1;

  // ── Charts ──────────────────────────────────────────────────────────
  QTabWidget       *m_chartTabs = nullptr;
  TooltipChartView *m_densView  = nullptr;
  TooltipChartView *m_lepDensView = nullptr;
  TooltipChartView *m_muView    = nullptr;
  TooltipChartView *m_lepView   = nullptr;
  QAbstractAxis    *m_densAxisX = nullptr, *m_densAxisY = nullptr;
  QAbstractAxis    *m_lepDensAxisX = nullptr, *m_lepDensAxisY = nullptr;
  QAbstractAxis    *m_muAxisX   = nullptr, *m_muAxisY   = nullptr;
  QAbstractAxis    *m_lepAxisX  = nullptr, *m_lepAxisY  = nullptr;

  bool m_isLogScale     = true;
  bool m_tempIsVertical = true;
};

#endif // RUNFROMFILEWIDGET_H
