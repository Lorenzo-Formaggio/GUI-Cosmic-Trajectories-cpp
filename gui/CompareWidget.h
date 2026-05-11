#ifndef COMPAREWIDGET_H
#define COMPAREWIDGET_H

#include "SimulationWorker.h"

#include <QtWidgets>
#include <QWidget>
#include <QVector>
#include <QColor>

// Forward declarations
class QDoubleSpinBox;
class QComboBox;
class QPushButton;
class QPushButton;
class QLabel;
class QTabWidget;
class QThread;
class QLineEdit;
class QGroupBox;
class QVBoxLayout;

#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QLogValueAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QAbstractAxis>
#include <QtCharts/QScatterSeries>
#include "TooltipChartView.h"

/**
 * @brief Per-slot data: UI widgets, chart series, thread, and trajectory data.
 */
struct SlotConfig {
  // Container groupbox so the whole slot can be added/removed dynamically.
  QGroupBox *box = nullptr;

  // Parameter widgets
  QDoubleSpinBox *spinB    = nullptr;
  QDoubleSpinBox *spinLe   = nullptr;
  QDoubleSpinBox *spinLmu  = nullptr;
  QDoubleSpinBox *spinLtau = nullptr;
  QDoubleSpinBox *spinDT   = nullptr;
  QDoubleSpinBox *spinTmin = nullptr;
  QDoubleSpinBox *spinTmax = nullptr;
  QComboBox *comboNf       = nullptr;
  QComboBox *comboEos      = nullptr;
  QComboBox *comboGuess    = nullptr;
  QComboBox *comboScan     = nullptr;

  QWidget     *eosPathWidget  = nullptr;
  QLineEdit   *lineEditEosPath = nullptr;
  QPushButton *btnBrowseEos   = nullptr;
  QLabel      *labelEosPath   = nullptr;

  QPushButton *btnRun    = nullptr;
  QPushButton *btnStop   = nullptr;
  QPushButton *btnClear  = nullptr;
  QPushButton *btnRemove = nullptr;
  QLabel      *statusLabel = nullptr;

  // Trajectory data
  QVector<TrajectoryPoint> data;
  QColor color;

  // Snapshot of run parameters (set when the slot is run)
  bool   runParamsValid = false;
  double B    = 0.0;
  double Le   = 0.0;
  double Lmu  = 0.0;
  double Ltau = 0.0;

  // Chart series (one per quantity)
  QLineSeries *sernB    = nullptr;
  QLineSeries *serS     = nullptr;
  QLineSeries *sernQ    = nullptr;
  QLineSeries *serMuB   = nullptr;
  QLineSeries *serMuQ   = nullptr;
  QLineSeries *serMunue  = nullptr;
  QLineSeries *serMunumu = nullptr;
  QLineSeries *serMnutau = nullptr;

  // Thread management
  QThread          *thread = nullptr;
  SimulationWorker *worker = nullptr;
};

/**
 * @brief Widget showing 5 parameter slots and overlaid comparison charts.
 */
class CompareWidget : public QWidget {
  Q_OBJECT
public:
  explicit CompareWidget(const QString &workingDir, QWidget *parent = nullptr);
  ~CompareWidget() override;

private:
  void setupUi();
  void createSlotPanel(QWidget *parent);
  void createChartPanel(QWidget *parent);
  void runSlot(SlotConfig *s);
  void clearSlot(SlotConfig *s);
  void clearSlotSeries(SlotConfig *s);
  void replotData();
  void updateChartAxes();
  void updateSlotSeriesVisibility(SlotConfig *s);
  void onLogMessage(const QString &msg, SlotConfig *s);

  // Dynamic slot management
  void addSlot();
  void removeSlot(SlotConfig *s);
  void buildSlotUi(SlotConfig *s);
  void buildSlotSeries(SlotConfig *s);
  int  slotIndex(const SlotConfig *s) const;
  QColor pickSlotColor(int i) const;

  // Per-quantity abs/legend helpers
  double absVal(double v, bool useAbs) const;
  QString legendSuffix(const SlotConfig &s) const;
  void refreshSeriesNames();
  void refreshLegendVisibility();
  
  void onThemeToggleClicked();
  void onAxisToggleClicked();
  void onScaleToggleClicked();
  void onExportClicked();
  void onCriticalPointButtonClicked();
  void updateCriticalPoint();
  void onSolverSettingsButtonClicked();

  QString m_workingDir;
  QVector<SlotConfig*> m_slots;
  QVBoxLayout *m_slotsLayout = nullptr;   // hosts the per-slot groupboxes
  QPushButton *m_btnAddSlot  = nullptr;

  QPushButton  *m_btnThemeToggle = nullptr;
  QPushButton  *m_btnAxisToggle  = nullptr;
  QPushButton *m_btnScaleToggle = nullptr;
  QPushButton *m_btnCriticalPoint = nullptr;
  QPushButton *m_btnSolverSettings = nullptr;
  QPushButton *m_btnShowHide = nullptr;
  QTextEdit    *m_console           = nullptr;

  QDialog      *m_cpDialog              = nullptr;
  QDialog      *m_solverSettingsDialog  = nullptr;
  QDialog      *m_visDialog             = nullptr;

  // Solver settings state (shared across all slots)
  double m_tolerance   = 1e-6;
  int    m_maxIter     = 100;
  int    m_guessMethod = 0;

  // Metropolis optimizer state (shared)
  int    m_metropolisMode      = 0;
  int    m_metropolisSteps     = 500;
  double m_metropolisStepSigma = 1.0;
  double m_metropolisT         = 0.01;

  QCheckBox *m_chknB      = nullptr;
  QCheckBox *m_chkS       = nullptr;
  QCheckBox *m_chknQ      = nullptr;
  QCheckBox *m_chkMuB     = nullptr;
  QCheckBox *m_chkMuQ     = nullptr;
  QCheckBox *m_chkMunue   = nullptr;
  QCheckBox *m_chkMunumu  = nullptr;
  QCheckBox *m_chkMnutau  = nullptr;

  // Per-quantity absolute-value checkboxes (mirror visibility set)
  QCheckBox *m_absnB      = nullptr;
  QCheckBox *m_absS       = nullptr;
  QCheckBox *m_absnQ      = nullptr;
  QCheckBox *m_absMuB     = nullptr;
  QCheckBox *m_absMuQ     = nullptr;
  QCheckBox *m_absMunue   = nullptr;
  QCheckBox *m_absMunumu  = nullptr;
  QCheckBox *m_absMnutau  = nullptr;

  // Per-quantity abs-flag state (defaults match the previous |·| display)
  bool m_useAbsnB     = false;
  bool m_useAbsS      = false;
  bool m_useAbsnQ     = true;
  bool m_useAbsMuB    = true;
  bool m_useAbsMuQ    = true;
  bool m_useAbsMunue  = true;
  bool m_useAbsMunumu = true;
  bool m_useAbsMnutau = true;

  // Legend customization
  bool m_legendVisible       = true;
  bool m_legendShowB         = false;
  bool m_legendShowLe        = false;
  bool m_legendShowLmu       = false;
  bool m_legendShowLtau      = false;

  // Critical point widgets
  QDoubleSpinBox *m_spinCpT   = nullptr;
  QDoubleSpinBox *m_spinCpMuB = nullptr;
  QDoubleSpinBox *m_spinCpMuQ = nullptr;
  QCheckBox      *m_chkShowCp = nullptr;

  QScatterSeries *m_seriesCpB = nullptr;
  QScatterSeries *m_seriesCpQ = nullptr;

  // Global custom guess settings
  int m_initialGuessType = 0; // 0 = Standard, 1 = Custom
  std::vector<double> m_customGuessLowHigh = {0.01, -0.001, -1e-05, -1e-05, -1e-05};
  std::vector<double> m_customGuessHighLow = {1.0, -0.1, -0.1, -0.1, -0.1};

  bool m_tempIsVertical = true;
  bool m_isLogScale     = true;

  QTabWidget    *m_chartTabs      = nullptr;
  TooltipChartView *m_densChartView  = nullptr;
  TooltipChartView *m_muChartView    = nullptr;
  TooltipChartView *m_lepChartView   = nullptr;
  QAbstractAxis *m_densAxisX      = nullptr;
  QAbstractAxis *m_densAxisY      = nullptr;
  QAbstractAxis *m_muAxisX        = nullptr;
  QAbstractAxis *m_muAxisY        = nullptr;
  QAbstractAxis *m_lepAxisX       = nullptr;
  QAbstractAxis *m_lepAxisY       = nullptr;
};

#endif // COMPAREWIDGET_H
