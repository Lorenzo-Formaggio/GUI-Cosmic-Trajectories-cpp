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

#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QLogValueAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QAbstractAxis>
#include <QtCharts/QScatterSeries>
#include "TooltipChartView.h"

static constexpr int NUM_SLOTS = 5;

/**
 * @brief Per-slot data: UI widgets, chart series, thread, and trajectory data.
 */
struct SlotConfig {
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

  QPushButton *btnRun   = nullptr;
  QPushButton *btnStop  = nullptr;
  QPushButton *btnClear = nullptr;
  QLabel      *statusLabel = nullptr;

  // Trajectory data
  QVector<TrajectoryPoint> data;
  QColor color;

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
  void runSlot(int idx);
  void clearSlot(int idx);
  void clearSlotSeries(int idx);
  void replotData();
  void updateChartAxes();
  void updateSlotSeriesVisibility(int idx);
  
  void onThemeToggleClicked();
  void onAxisToggleClicked();
  void onScaleToggleClicked();
  void onExportClicked();
  void onCriticalPointButtonClicked();
  void updateCriticalPoint();

  QString m_workingDir;
  SlotConfig m_slots[NUM_SLOTS];

  QPushButton  *m_btnThemeToggle = nullptr;
  QPushButton  *m_btnAxisToggle  = nullptr;
  QPushButton  *m_btnScaleToggle = nullptr;
  QPushButton  *m_btnCriticalPoint = nullptr;
  QDialog      *m_cpDialog       = nullptr;

  QCheckBox *m_chknB      = nullptr;
  QCheckBox *m_chkS       = nullptr;
  QCheckBox *m_chknQ      = nullptr;
  QCheckBox *m_chkMuB     = nullptr;
  QCheckBox *m_chkMuQ     = nullptr;
  QCheckBox *m_chkMunue   = nullptr;
  QCheckBox *m_chkMunumu  = nullptr;
  QCheckBox *m_chkMnutau  = nullptr;

  // Critical point widgets
  QDoubleSpinBox *m_spinCpT   = nullptr;
  QDoubleSpinBox *m_spinCpMuB = nullptr;
  QDoubleSpinBox *m_spinCpMuQ = nullptr;
  QCheckBox      *m_chkShowCp = nullptr;

  QScatterSeries *m_seriesCpB = nullptr;
  QScatterSeries *m_seriesCpQ = nullptr;

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
