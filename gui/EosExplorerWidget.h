#ifndef EOSEXPLORERWIDGET_H
#define EOSEXPLORERWIDGET_H

#include <QFont>
#include <QPointF>
#include <QString>
#include <QVector>
#include <QWidget>

// Forward declarations
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QLabel;
class QLineEdit;
class QTextEdit;
class QCheckBox;
class QTabWidget;

#include "TooltipChartView.h"
#include <QtCharts/QAbstractAxis>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QLogValueAxis>
#include <QtCharts/QValueAxis>

/**
 * @brief EoS Explorer widget — third tab of the main window.
 *
 * Plots Baryon density nB(T), Charge density nQ(T), and Entropy density s(T)
 * for user-specified fixed µB, µQ values using any supported Equation of State.
 * Each "Compute" overlays new series on the charts for comparison.
 */
class EosExplorerWidget : public QWidget {
  Q_OBJECT

public:
  explicit EosExplorerWidget(const QString &workingDir,
                             QWidget *parent = nullptr);

private slots:
  void onComputeClicked();
  void onExportClicked();
  void onClearClicked();
  void onThemeToggleClicked();
  void onScaleToggleClicked();
  void onNormalizeToggleClicked();
  void onEosChanged(int index);
  void onScanVarChanged(int index);
  void logMessage(const QString &msg, bool isError = false);
  void onConfigureAxisFontsClicked();

private:
  void applyAxisFonts();
  void setupUi();
  void rebuildAxes(int chartIdx);
  void rebuildAllAxes();
  QString scanVarLabel() const;
  QString scanVarUnit() const;
  void replotData();

  QString m_workingDir;

  // ── Parameter widgets ───────────────────────────────────────────────
  QComboBox *m_comboEos = nullptr;
  QComboBox *m_comboNf = nullptr;
  QComboBox *m_comboScanVar = nullptr; // 0=T, 1=µB, 2=µQ

  // EoS table path (for Interpolated Table)
  QWidget *m_eosPathWidget = nullptr;
  QLineEdit *m_lineEditEosPath = nullptr;
  QPushButton *m_btnBrowseEos = nullptr;
  QLabel *m_labelEosPath = nullptr;

  // Fixed parameters (two of {T, µB, µQ} — the third is scanned)
  QLabel *m_labelFixed1 = nullptr;
  QLabel *m_labelFixed2 = nullptr;
  QDoubleSpinBox *m_spinFixed1 = nullptr; // first fixed param
  QDoubleSpinBox *m_spinFixed2 = nullptr; // second fixed param

  // Scan range (for whichever variable is being scanned)
  QLabel *m_labelScanMin = nullptr;
  QLabel *m_labelScanMax = nullptr;
  QLabel *m_labelScanStep = nullptr;
  QDoubleSpinBox *m_spinScanMin = nullptr;
  QDoubleSpinBox *m_spinScanMax = nullptr;
  QDoubleSpinBox *m_spinDScan = nullptr;

  QCheckBox *m_chkNormalizeT3 = nullptr;

  // Buttons
  QPushButton *m_btnCompute = nullptr;
  QPushButton *m_btnClear = nullptr;
  QPushButton *m_btnThemeToggle = nullptr;
  QPushButton *m_btnScaleToggle = nullptr;

  // Console
  QLabel *m_statusLabel = nullptr;
  QTextEdit *m_console = nullptr;

  // ── Charts (3 tabs: nB, nQ, s) ─────────────────────────────────────
  static const int NUM_CHARTS = 3;
  QTabWidget *m_chartTabs = nullptr;
  TooltipChartView *m_chartViews[NUM_CHARTS] = {};
  QAbstractAxis *m_axesX[NUM_CHARTS] = {};
  QAbstractAxis *m_axesY[NUM_CHARTS] = {};

  bool m_isLogScale = false;
  bool m_isNormalizedT3 = false;

  // Per-chart abs flag (linear-mode only; log forces abs).
  // Order matches the chart index: 0 = nB, 1 = nQ, 2 = s.
  bool m_useAbs[NUM_CHARTS] = {false, false, false};
  bool m_legendVisible = true;

  // Y-axis labels (chart titles are built dynamically from scan variable)
  static constexpr const char *CHART_YNAMES[NUM_CHARTS] = {
      "Baryon Density nB", "Charge Density nQ", "Entropy Density s"};
  static constexpr const char *CHART_YLABELS[NUM_CHARTS] = {
      "Baryon Density nB [MeV³]", "Charge Density nQ [MeV³]",
      "Entropy Density s [MeV³]"};
  static constexpr const char *CHART_YLABELS_NORM[NUM_CHARTS] = {
      "nB / T³", "nQ / T³", "s / T³"};

  // ── Data storage for export ─────────────────────────────────────────
  struct SeriesData {
    QString label;
    int scanVar = 0; // 0=T, 1=µB, 2=µQ
    double fixedT = 0.0;
    double muB = 0.0;
    double muQ = 0.0;
    QVector<QPointF> nB_points; // x = scan variable value
    QVector<QPointF> nQ_points;
    QVector<QPointF> s_points;
    QVector<double> T_values; // temperature at each point (for T³ normalization)
    QVector<double> p_QCD; // QCD pressure [MeV^4]
    QVector<double> e_QCD; // QCD energy density [MeV^4]
  };
  QVector<SeriesData> m_allSeries;

  // Auto-color palette
  int m_colorIndex = 0;
  static QColor nextColor(int &idx);

  QFont m_axisFont;
  bool m_axisFontValid = false;
};

#endif // EOSEXPLORERWIDGET_H
