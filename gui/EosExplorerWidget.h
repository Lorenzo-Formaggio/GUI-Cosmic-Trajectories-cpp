#ifndef EOSEXPLORERWIDGET_H
#define EOSEXPLORERWIDGET_H

#include <QWidget>
#include <QFont>
#include <QVector>
#include <QPointF>
#include <QString>

// Forward declarations
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QLabel;
class QLineEdit;
class QTextEdit;
class QCheckBox;
class QTabWidget;

#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QLogValueAxis>
#include <QtCharts/QValueAxis>
#include <QtCharts/QAbstractAxis>
#include "TooltipChartView.h"

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
  explicit EosExplorerWidget(const QString &workingDir, QWidget *parent = nullptr);

private slots:
  void onComputeClicked();
  void onExportClicked();
  void onClearClicked();
  void onThemeToggleClicked();
  void onScaleToggleClicked();
  void onNormalizeToggleClicked();
  void onEosChanged(int index);
  void logMessage(const QString &msg, bool isError = false);
  void onConfigureAxisFontsClicked();

private:
  void applyAxisFonts();
  void setupUi();
  void rebuildAxes(int chartIdx);
  void rebuildAllAxes();
  void replotData();

  QString m_workingDir;

  // ── Parameter widgets ───────────────────────────────────────────────
  QComboBox      *m_comboEos       = nullptr;
  QComboBox      *m_comboNf        = nullptr;

  // EoS table path (for Interpolated Table)
  QWidget        *m_eosPathWidget  = nullptr;
  QLineEdit      *m_lineEditEosPath = nullptr;
  QPushButton    *m_btnBrowseEos   = nullptr;
  QLabel         *m_labelEosPath   = nullptr;

  // Fixed chemical potentials
  QDoubleSpinBox *m_spinMuB        = nullptr;
  QDoubleSpinBox *m_spinMuQ        = nullptr;

  // Temperature scan range
  QDoubleSpinBox *m_spinTmin       = nullptr;
  QDoubleSpinBox *m_spinTmax       = nullptr;
  QDoubleSpinBox *m_spinDT         = nullptr;

  QCheckBox      *m_chkNormalizeT3 = nullptr;

  // Buttons
  QPushButton    *m_btnCompute     = nullptr;
  QPushButton    *m_btnClear       = nullptr;
  QPushButton    *m_btnThemeToggle = nullptr;
  QPushButton    *m_btnScaleToggle = nullptr;

  // Console
  QLabel         *m_statusLabel    = nullptr;
  QTextEdit      *m_console        = nullptr;

  // ── Charts (3 tabs: nB, nQ, s) ─────────────────────────────────────
  static const int NUM_CHARTS = 3;
  QTabWidget       *m_chartTabs = nullptr;
  TooltipChartView *m_chartViews[NUM_CHARTS] = {};
  QAbstractAxis    *m_axesX[NUM_CHARTS] = {};
  QAbstractAxis    *m_axesY[NUM_CHARTS] = {};

  bool m_isLogScale = false;
  bool m_isNormalizedT3 = false;

  // Per-chart abs flag (linear-mode only; log forces abs).
  // Order matches the chart index: 0 = nB, 1 = nQ, 2 = s.
  bool m_useAbs[NUM_CHARTS] = {false, false, false};
  bool m_legendVisible = true;

  // Chart titles and Y-axis labels
  static constexpr const char* CHART_TITLES[NUM_CHARTS] = {
    "Baryon Density nB vs Temperature",
    "Charge Density nQ vs Temperature",
    "Entropy Density s vs Temperature"
  };
  static constexpr const char* CHART_YLABELS[NUM_CHARTS] = {
    "Baryon Density nB [MeV³]",
    "Charge Density nQ [MeV³]",
    "Entropy Density s [MeV³]"
  };
  static constexpr const char* CHART_YLABELS_NORM[NUM_CHARTS] = {
    "nB / T³",
    "nQ / T³",
    "s / T³"
  };

  // ── Data storage for export ─────────────────────────────────────────
  struct SeriesData {
    QString label;
    double muB = 0.0;
    double muQ = 0.0;
    QVector<QPointF> nB_points;
    QVector<QPointF> nQ_points;
    QVector<QPointF> s_points;
  };
  QVector<SeriesData> m_allSeries;

  // Auto-color palette
  int m_colorIndex = 0;
  static QColor nextColor(int &idx);

  QFont m_axisFont;
  bool  m_axisFontValid = false;
};

#endif // EOSEXPLORERWIDGET_H
