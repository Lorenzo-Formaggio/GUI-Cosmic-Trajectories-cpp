#include "EosExplorerWidget.h"
#include "SimulationWorker.h"
#include "include/QCDTherm.hpp"
#include "include/JEL.hpp"
#include "include/InterpolatedEoS.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QFileDialog>
#include <QMessageBox>
#include <QPdfWriter>
#include <QPainter>
#include <QTextStream>
#include <QColor>
#include <QTime>
#include <QTextEdit>
#include <QCheckBox>
#include <QTabWidget>
#include <QMenu>
#include <QAction>

// Required for Qt < 5.17 / C++17 compatibility if taking address, though QString takes by value/pointer.
constexpr const char* EosExplorerWidget::CHART_TITLES[NUM_CHARTS];
constexpr const char* EosExplorerWidget::CHART_YLABELS[NUM_CHARTS];
constexpr const char* EosExplorerWidget::CHART_YLABELS_NORM[NUM_CHARTS];

EosExplorerWidget::EosExplorerWidget(const QString &workingDir, QWidget *parent)
    : QWidget(parent), m_workingDir(workingDir) {
  setupUi();
}

void EosExplorerWidget::setupUi() {
  QHBoxLayout *mainLayout = new QHBoxLayout(this);
  mainLayout->setContentsMargins(0, 0, 0, 0);

  QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
  mainLayout->addWidget(splitter);

  // ─── Left Panel (Controls) ──────────────────────────────────────────
  QWidget *leftPanel = new QWidget(splitter);
  QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
  leftLayout->setContentsMargins(0, 0, 0, 0);

  QGroupBox *groupParams = new QGroupBox("EoS Explorer Settings", leftPanel);
  groupParams->setStyleSheet("QGroupBox { border: 1px solid gray; border-radius: 5px; margin-top: 1ex; font-weight: bold; } "
                             "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 3px 0 3px; }");
  QVBoxLayout *paramsLayout = new QVBoxLayout(groupParams);
  QGridLayout *grid = new QGridLayout();

  int row = 0;

  // EoS Selection
  m_comboEos = new QComboBox();
  m_comboEos->addItems({"Free QGP (0)", "Lattice QCD (1)", "Interpolated Table (2)", "Entropy Contour (3)"});
  connect(m_comboEos, &QComboBox::currentIndexChanged, this, &EosExplorerWidget::onEosChanged);
  grid->addWidget(new QLabel("Equation of State:"), row, 0);
  grid->addWidget(m_comboEos, row++, 1);

  // Flavors (nf)
  m_comboNf = new QComboBox();
  m_comboNf->addItems({"2", "3", "4"});
  m_comboNf->setCurrentIndex(1); // Default to 3
  grid->addWidget(new QLabel("Flavors (nf):"), row, 0);
  grid->addWidget(m_comboNf, row++, 1);

  // Interpolated Table Path
  m_eosPathWidget = new QWidget();
  QHBoxLayout *eosPathLayout = new QHBoxLayout(m_eosPathWidget);
  eosPathLayout->setContentsMargins(0, 0, 0, 0);
  m_lineEditEosPath = new QLineEdit();
  m_lineEditEosPath->setPlaceholderText("Path to EoS table...");
  m_btnBrowseEos = new QPushButton("Browse");
  eosPathLayout->addWidget(m_lineEditEosPath);
  eosPathLayout->addWidget(m_btnBrowseEos);
  
  m_labelEosPath = new QLabel("Table Path:");
  grid->addWidget(m_labelEosPath, row, 0);
  grid->addWidget(m_eosPathWidget, row++, 1);

  m_labelEosPath->setVisible(false);
  m_eosPathWidget->setVisible(false);

  connect(m_btnBrowseEos, &QPushButton::clicked, this, [this]() {
      QString file = QFileDialog::getOpenFileName(this, "Select EoS Table File", m_workingDir, "Text Files (*.txt);;All Files (*)");
      if (!file.isEmpty()) {
          m_lineEditEosPath->setText(file);
      }
  });

  // Fixed Parameters
  auto addSpin = [&](const QString& label, double val, double min, double max, int decimals) -> QDoubleSpinBox* {
    grid->addWidget(new QLabel(label), row, 0);
    QDoubleSpinBox *spin = new QDoubleSpinBox();
    spin->setDecimals(decimals);
    spin->setRange(min, max);
    spin->setValue(val);
    grid->addWidget(spin, row, 1);
    row++;
    return spin;
  };

  m_spinMuB = addSpin("Fix µB [MeV]:", 0.0, -10000.0, 10000.0, 2);
  m_spinMuQ = addSpin("Fix µQ [MeV]:", 0.0, -10000.0, 10000.0, 2);

  grid->addWidget(new QLabel("── Scan Range ──"), row++, 0, 1, 2, Qt::AlignCenter);

  m_spinTmin = addSpin("T min [MeV]:", 30.0, 0.1, 10000.0, 1);
  m_spinTmax = addSpin("T max [MeV]:", 500.0, 0.1, 10000.0, 1);
  m_spinDT   = addSpin("T step [MeV]:", 2.0, 0.01, 100.0, 2);

  m_chkNormalizeT3 = new QCheckBox("Normalize density by T³");
  m_chkNormalizeT3->setChecked(false);
  connect(m_chkNormalizeT3, &QCheckBox::clicked, this, &EosExplorerWidget::onNormalizeToggleClicked);
  grid->addWidget(m_chkNormalizeT3, row++, 0, 1, 2);

  paramsLayout->addLayout(grid);

  // Compute / Clear Buttons
  QHBoxLayout *actionLayout = new QHBoxLayout();
  m_btnCompute = new QPushButton("Compute");
  m_btnCompute->setObjectName("BtnCompute");
  m_btnCompute->setMinimumHeight(28); // Smaller button
  connect(m_btnCompute, &QPushButton::clicked, this, &EosExplorerWidget::onComputeClicked);

  m_btnClear = new QPushButton("Clear Charts");
  m_btnClear->setObjectName("BtnClear");
  m_btnClear->setMinimumHeight(28); // Smaller button
  connect(m_btnClear, &QPushButton::clicked, this, &EosExplorerWidget::onClearClicked);

  actionLayout->addWidget(m_btnCompute);
  actionLayout->addWidget(m_btnClear);
  paramsLayout->addLayout(actionLayout);

  leftLayout->addWidget(groupParams);
  
  // Console Panel
  QGroupBox *groupConsole = new QGroupBox("Console", leftPanel);
  QVBoxLayout *consoleLayout = new QVBoxLayout(groupConsole);
  
  m_statusLabel = new QLabel("Ready");
  m_statusLabel->setStyleSheet("color: #17a2b8; font-weight: bold;");
  consoleLayout->addWidget(m_statusLabel);
  
  m_console = new QTextEdit();
  m_console->setReadOnly(true);
  m_console->setFontFamily("Courier");
  m_console->setMinimumHeight(150);
  consoleLayout->addWidget(m_console);
  
  leftLayout->addWidget(groupConsole);
  leftLayout->addStretch();

  // ─── Right Panel (Charts) ───────────────────────────────────────────
  QWidget *rightPanel = new QWidget(splitter);
  QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
  rightLayout->setContentsMargins(0, 0, 0, 0);

  m_chartTabs = new QTabWidget();
  rightLayout->addWidget(m_chartTabs);

  QString tabNames[NUM_CHARTS] = {"Baryon Density (nB)", "Charge Density (nQ)", "Entropy (s)"};

  for (int i = 0; i < NUM_CHARTS; ++i) {
    QChart *chart = new QChart();
    chart->setTitle(CHART_TITLES[i]);
    chart->setTheme(QChart::ChartThemeLight);
    chart->setAnimationOptions(QChart::NoAnimation);
    chart->legend()->setAlignment(Qt::AlignRight);
    chart->legend()->setMarkerShape(QLegend::MarkerShapeFromSeries);

    m_chartViews[i] = new TooltipChartView(chart);
    m_chartViews[i]->setRenderHint(QPainter::Antialiasing);

    m_axesX[i] = nullptr;
    m_axesY[i] = nullptr;

    m_chartTabs->addTab(m_chartViews[i], tabNames[i]);
  }

  rebuildAllAxes();

  // ── Plot Settings Menu (Sleek Dark Styling) ───────────────────────────
  // ── Plot Settings Menu (Inherits Global Style) ───────────────────────────
  QPushButton *btnPlotSettings = new QPushButton("Plot Settings", rightPanel);
  
  QMenu *plotMenu = new QMenu(this);

  // Section: Visibility / Abs
  QAction *actShowHide = plotMenu->addAction("Show/Hide Quantities...");
  connect(actShowHide, &QAction::triggered, this, [this]() {
    QDialog dlg(this);
    dlg.setWindowTitle("Show/Hide Quantities");
    QVBoxLayout *v = new QVBoxLayout(&dlg);
    v->addWidget(new QLabel("Per-chart absolute value (forced ON in log mode):"));
    static const QStringList chartNames = {"nB", "nQ", "s"};
    QVector<QCheckBox*> absChks;
    for (int i = 0; i < NUM_CHARTS; ++i) {
      auto *c = new QCheckBox(QString("|·| on %1").arg(chartNames[i]));
      c->setChecked(m_useAbs[i] || m_isLogScale);
      c->setEnabled(!m_isLogScale);
      absChks.append(c);
      v->addWidget(c);
    }
    QPushButton *ok = new QPushButton("OK");
    connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    v->addWidget(ok);
    if (dlg.exec() == QDialog::Accepted) {
      bool changed = false;
      for (int i = 0; i < NUM_CHARTS; ++i) {
        bool now = absChks[i]->isChecked();
        if (m_useAbs[i] != now) { m_useAbs[i] = now; changed = true; }
      }
      if (changed) replotData();
    }
  });

  plotMenu->addSeparator();

  // Section: View Controls
  QAction *actScale = plotMenu->addAction("Toggle Log/Linear");
  connect(actScale, &QAction::triggered, this, &EosExplorerWidget::onScaleToggleClicked);

  QAction *actTheme = plotMenu->addAction("Toggle Plot Theme");
  connect(actTheme, &QAction::triggered, this, &EosExplorerWidget::onThemeToggleClicked);

  // Show/hide chart legend (applies to all 3 charts)
  QAction *actLegend = plotMenu->addAction("Show Legend");
  actLegend->setCheckable(true);
  actLegend->setChecked(m_legendVisible);
  connect(actLegend, &QAction::toggled, this, [this](bool on) {
    m_legendVisible = on;
    for (int i = 0; i < NUM_CHARTS; ++i) {
      if (m_chartViews[i] && m_chartViews[i]->chart())
        m_chartViews[i]->chart()->legend()->setVisible(on);
    }
  });

  plotMenu->addSeparator();

  // Section: Export
  QAction *actExport = plotMenu->addAction("Export Active Plot...");
  connect(actExport, &QAction::triggered, this, &EosExplorerWidget::onExportClicked);

  btnPlotSettings->setMenu(plotMenu);

  // ── Auto-Fit Limits button (next to Plot Settings) ───────────────────
  QPushButton *btnAutoFit = new QPushButton("Auto-Fit Limits", rightPanel);
  btnAutoFit->setToolTip("Fit axis ranges to the currently visible curves "
                         "in the active chart (uses log/linear margins).");
  connect(btnAutoFit, &QPushButton::clicked, this, [this]() {
    int idx = m_chartTabs->currentIndex();
    if (idx >= 0 && idx < NUM_CHARTS && m_chartViews[idx]) {
      autoFitChartFromVisibleSeries(m_chartViews[idx]->chart());
    }
  });

  QHBoxLayout *toolsLayout = new QHBoxLayout();
  toolsLayout->setContentsMargins(0, 0, 0, 15);
  toolsLayout->addStretch();
  toolsLayout->addWidget(btnPlotSettings);
  toolsLayout->addWidget(btnAutoFit);
  toolsLayout->addStretch();
  rightLayout->addLayout(toolsLayout);

  splitter->addWidget(leftPanel);
  splitter->addWidget(rightPanel);
  splitter->setSizes({300, 700});
}

void EosExplorerWidget::rebuildAllAxes() {
  for (int i = 0; i < NUM_CHARTS; ++i) {
    rebuildAxes(i);
  }
}

void EosExplorerWidget::rebuildAxes(int chartIdx) {
  if (chartIdx < 0 || chartIdx >= NUM_CHARTS) return;
  QChart *chart = m_chartViews[chartIdx]->chart();
  
  if (m_axesX[chartIdx]) chart->removeAxis(m_axesX[chartIdx]);
  if (m_axesY[chartIdx]) chart->removeAxis(m_axesY[chartIdx]);
  delete m_axesX[chartIdx];
  delete m_axesY[chartIdx];

  if (m_isLogScale) {
    m_axesX[chartIdx] = new QLogValueAxis();
    static_cast<QLogValueAxis*>(m_axesX[chartIdx])->setBase(10.0);
    static_cast<QLogValueAxis*>(m_axesX[chartIdx])->setLabelFormat("%g");
    
    m_axesY[chartIdx] = new QLogValueAxis();
    static_cast<QLogValueAxis*>(m_axesY[chartIdx])->setBase(10.0);
    static_cast<QLogValueAxis*>(m_axesY[chartIdx])->setLabelFormat("%g");
  } else {
    m_axesX[chartIdx] = new QValueAxis();
    static_cast<QValueAxis*>(m_axesX[chartIdx])->setLabelFormat("%g");
    
    m_axesY[chartIdx] = new QValueAxis();
    static_cast<QValueAxis*>(m_axesY[chartIdx])->setLabelFormat("%g");
  }

  m_axesX[chartIdx]->setTitleText("Temperature [MeV]");
  m_axesY[chartIdx]->setTitleText(m_isNormalizedT3 ? CHART_YLABELS_NORM[chartIdx] : CHART_YLABELS[chartIdx]);

  chart->addAxis(m_axesX[chartIdx], Qt::AlignBottom);
  chart->addAxis(m_axesY[chartIdx], Qt::AlignLeft);

  // Re-attach existing series
  for (auto *abs : chart->series()) {
    auto *series = qobject_cast<QLineSeries*>(abs);
    if (series) {
      series->attachAxis(m_axesX[chartIdx]);
      series->attachAxis(m_axesY[chartIdx]);
    }
  }

  // Auto-range
  if (!m_allSeries.isEmpty()) {
    double minX = 1e99, maxX = -1e99;
    double minY = 1e99, maxY = -1e99;
    
    for (const auto& data : m_allSeries) {
      const QVector<QPointF>& points = (chartIdx == 0) ? data.nB_points :
                                       (chartIdx == 1) ? data.nQ_points : data.s_points;
      for (const auto& pt : points) {
        double y = pt.y();
        if (m_isNormalizedT3) {
            double T3 = pt.x() * pt.x() * pt.x();
            if (T3 > 0) y /= T3;
        }
        if (m_isLogScale)             y = std::max(std::abs(y), 1e-15);
        else if (m_useAbs[chartIdx])  y = std::abs(y);
        minX = std::min(minX, pt.x());
        maxX = std::max(maxX, pt.x());
        minY = std::min(minY, y);
        maxY = std::max(maxY, y);
      }
    }
    
    if (minX >= maxX) maxX = minX + 1.0;
    if (minY >= maxY) maxY = minY + 1.0;

    if (m_isLogScale) {
      m_axesX[chartIdx]->setRange(minX * 0.9, maxX * 1.1);
      m_axesY[chartIdx]->setRange(minY * 0.5, maxY * 2.0);
    } else {
      m_axesX[chartIdx]->setRange(minX * 0.9, maxX * 1.1);
      m_axesY[chartIdx]->setRange(std::min(0.0, minY * 1.1), maxY * 1.1);
    }
  } else {
    // No data yet — apply a sensible default range so the axes still
    // render with tick marks, like the other widgets.
    if (m_isLogScale) {
      m_axesX[chartIdx]->setRange(1.0, 1000.0);
      m_axesY[chartIdx]->setRange(1e-3, 1e3);
    } else {
      const double tMin = m_spinTmin ? m_spinTmin->value() : 80.0;
      const double tMax = m_spinTmax ? m_spinTmax->value() : 200.0;
      m_axesX[chartIdx]->setRange(tMin, tMax);
      m_axesY[chartIdx]->setRange(0.0, 1.0);
    }
  }
}

void EosExplorerWidget::onEosChanged(int index) {
  bool isTable = (index == 2);
  m_labelEosPath->setVisible(isTable);
  m_eosPathWidget->setVisible(isTable);
}

void EosExplorerWidget::logMessage(const QString &msg, bool isError) {
  // If the message already contains HTML colour markup, append it directly
  // so inner colours are not overridden by an outer span.
  if (msg.contains("<font ") || msg.contains("<span ")) {
    m_console->append(msg);
    return;
  }
  QString color = isError ? "#ff6b6b" : "#ffffff";
  QString text = msg;
  if (isError) text = "<b>Error:</b> " + text;
  m_console->append(QString("<span style=\"color:%1;\">%2</span>")
                        .arg(color)
                        .arg(text));
}

void EosExplorerWidget::onComputeClicked() {
  QMutexLocker locker(&SimulationWorker::physicsMutex);
  m_btnCompute->setEnabled(false);
  m_statusLabel->setText("Computing...");
  m_statusLabel->setStyleSheet("color: #ffc107; font-weight: bold;");
  QCoreApplication::processEvents();

  try {
    // Clean up previous EoS state so we can re-initialize (important for switching Lattice QCD flavors)
    QCD::cleanup();

    logMessage("Initializing JEL tables...");
    initializeJELTables();

    int eos = m_comboEos->currentIndex();
    int nf = m_comboNf->currentText().toInt();
    
    // Check for Lattice QCD flavor compatibility
    if (eos == 1 && nf == 2) {
      logMessage("<font color='#ffc107'><b>Warning:</b> System does not have a 2 flavor Lattice QCD EoS. Defaulting to 3 flavors.</font>");
      nf = 3;
    }

    QString eosName;
    if (eos == 0) eosName = "Free QGP";
    else if (eos == 1) eosName = "Lattice QCD";
    else if (eos == 2) eosName = "Interpolated Table";
    else if (eos == 3) eosName = "Entropy Contour";
    
    // If using interpolated EoS, load the table and log its range
    if (eos == 2) {
      QString eosPath = m_lineEditEosPath->text().isEmpty() ? (m_workingDir + "/EoS_Table.txt") : m_lineEditEosPath->text();
      if (InterpolatedEoS::isLoaded() && InterpolatedEoS::getLoadedFilename() == eosPath.toStdString()) {
        logMessage("Interpolated EoS table is already in memory. Skipping reload.");
      } else {
        logMessage("Loading interpolated EoS table... (This may take a moment)");
        InterpolatedEoS::loadTable(eosPath.toStdString());
      }
      if (InterpolatedEoS::isLoaded()) {
          logMessage(QString("  Table loaded. T range: %1 \u2013 %2 MeV")
                            .arg(InterpolatedEoS::getTmin(), 0, 'f', 1)
                            .arg(InterpolatedEoS::getTmax(), 0, 'f', 1));
      }
    }

    logMessage(QString("Setting EoS: %1").arg(eosName));

    // Use the logic already in QCDTherm.cpp for setting up the EoS
    std::string baseDir = m_workingDir.toStdString();
    QCD::setEoS(eos, baseDir, nf);

    double muB = m_spinMuB->value();
    double muQ = m_spinMuQ->value();
    double tMin = m_spinTmin->value();
    double tMax = m_spinTmax->value();
    double dt = m_spinDT->value();

    // Clamp tMin / tMax to the loaded table range (warn if outside)
    if (eos == 2 && InterpolatedEoS::isLoaded()) {
      double tableTmin = InterpolatedEoS::getTmin();
      double tableTmax = InterpolatedEoS::getTmax();

      if (tMin < tableTmin) {
        logMessage(QString("<font color='#ffc107'><b>Warning:</b> Requested Tmin (%1 MeV) is below table minimum (%2 MeV). Clamping to table minimum.</font>")
                       .arg(tMin, 0, 'f', 1).arg(tableTmin, 0, 'f', 1));
        tMin = tableTmin;
      } else if (tMin > tableTmax) {
        logMessage(QString("<font color='#ffc107'><b>Warning:</b> Requested Tmin (%1 MeV) exceeds table maximum (%2 MeV). Clamping to table maximum.</font>")
                       .arg(tMin, 0, 'f', 1).arg(tableTmax, 0, 'f', 1));
        tMin = tableTmax;
      }

      if (tMax > tableTmax) {
        logMessage(QString("<font color='#ffc107'><b>Warning:</b> Requested Tmax (%1 MeV) exceeds table maximum (%2 MeV). Clamping to table maximum.</font>")
                       .arg(tMax, 0, 'f', 1).arg(tableTmax, 0, 'f', 1));
        tMax = tableTmax;
      } else if (tMax < tableTmin) {
        logMessage(QString("<font color='#ffc107'><b>Warning:</b> Requested Tmax (%1 MeV) is below table minimum (%2 MeV). Clamping to table minimum.</font>")
                       .arg(tMax, 0, 'f', 1).arg(tableTmin, 0, 'f', 1));
        tMax = tableTmin;
      }
    }
    
    logMessage(QString("Parameters: muB=%1 MeV, muQ=%2 MeV").arg(muB).arg(muQ));
    
    if (tMin > tMax) std::swap(tMin, tMax);
    if (dt <= 0) dt = 1.0;
    int totalSteps = static_cast<int>(std::abs(tMax - tMin) / dt) + 1;

    logMessage("─────────────────────────────────────────");
    logMessage(QString("Scanning T = %1 → %2 MeV  (step %3 MeV, %4 steps)")
                        .arg(tMin, 0, 'f', 1)
                        .arg(tMax, 0, 'f', 1)
                        .arg(dt, 0, 'f', 2)
                        .arg(totalSteps));

    QString seriesName = QString("T-scan (µB=%1, µQ=%2)").arg(muB).arg(muQ);
    
    SeriesData newData;
    newData.label = seriesName;
    newData.muB = muB;
    newData.muQ = muQ;

    QLineSeries *series_arr[NUM_CHARTS];
    QColor c = nextColor(m_colorIndex);

    for (int i = 0; i < NUM_CHARTS; ++i) {
      series_arr[i] = new QLineSeries();
      series_arr[i]->setName(seriesName);
      series_arr[i]->setColor(c);
    }

    int count = 0;
    for (double T = tMin; T <= tMax; T += dt) {
      double nB = QCD::BarDens(muB, muQ, T, nf);
      double nQ = QCD::QCDcharge(muB, muQ, T, nf);
      double s  = QCD::sQCD(muB, muQ, T, nf);

      double yVals[NUM_CHARTS] = {nB, nQ, s};

      for (int i = 0; i < NUM_CHARTS; ++i) {
        double yVal = yVals[i];
        if (m_isNormalizedT3) {
            double T3 = T * T * T;
            if (T3 > 0) yVal /= T3;
        }
        if (m_isLogScale)            yVal = std::max(std::abs(yVal), 1e-15);
        else if (m_useAbs[i])        yVal = std::abs(yVal);
        series_arr[i]->append(T, yVal);
      }
      
      newData.nB_points.append(QPointF(T, nB));
      newData.nQ_points.append(QPointF(T, nQ));
      newData.s_points.append(QPointF(T, s));
      count++;
    }

    m_allSeries.append(newData);

    for (int i = 0; i < NUM_CHARTS; ++i) {
      m_chartViews[i]->chart()->addSeries(series_arr[i]);
      series_arr[i]->attachAxis(m_axesX[i]);
      series_arr[i]->attachAxis(m_axesY[i]);
    }

    rebuildAllAxes();
    logMessage("─────────────────────────────────────────");
    logMessage(QString("✓ Calculation complete. %1 points generated.").arg(count));
    m_statusLabel->setText("Ready");
    m_statusLabel->setStyleSheet("color: #28a745; font-weight: bold;");

  } catch (const std::exception &e) {
    logMessage(e.what(), true);
    m_statusLabel->setText("Error");
    m_statusLabel->setStyleSheet("color: #dc3545; font-weight: bold;");
    QMessageBox::critical(this, "Error", QString("Failed to compute: %1").arg(e.what()));
  }

  m_btnCompute->setEnabled(true);
}

void EosExplorerWidget::onClearClicked() {
  for (int i = 0; i < NUM_CHARTS; ++i) {
    m_chartViews[i]->chart()->removeAllSeries();
  }
  m_allSeries.clear();
  m_colorIndex = 0;
  rebuildAllAxes();
}

void EosExplorerWidget::onThemeToggleClicked() {
  for (int i = 0; i < NUM_CHARTS; ++i) {
    QChart *chart = m_chartViews[i]->chart();
    QChart::ChartTheme newTheme = (chart->theme() == QChart::ChartThemeLight) 
                                  ? QChart::ChartThemeDark : QChart::ChartThemeLight;
    chart->setTheme(newTheme);

    // Restore colors
    int idx = 0;
    for (auto *abs : chart->series()) {
      auto *series = qobject_cast<QLineSeries*>(abs);
      if (series) {
        series->setColor(nextColor(idx));
      }
    }
  }
}

void EosExplorerWidget::replotData() {
  for (int c = 0; c < NUM_CHARTS; ++c) {
    int i = 0;
    for (auto *abs : m_chartViews[c]->chart()->series()) {
      auto *series = qobject_cast<QLineSeries*>(abs);
      if (series && i < m_allSeries.size()) {
        series->clear();
        const QVector<QPointF>& points = (c == 0) ? m_allSeries[i].nB_points :
                                         (c == 1) ? m_allSeries[i].nQ_points : m_allSeries[i].s_points;
        for (const auto& pt : points) {
          double y = pt.y();
          if (m_isNormalizedT3) {
              double T3 = pt.x() * pt.x() * pt.x();
              if (T3 > 0) y /= T3;
          }
          if (m_isLogScale)         y = std::max(std::abs(y), 1e-15);
          else if (m_useAbs[c])     y = std::abs(y);
          series->append(pt.x(), y);
        }
      }
      i++;
    }
  }
  rebuildAllAxes();
}

void EosExplorerWidget::onScaleToggleClicked() {
  m_isLogScale = !m_isLogScale;
  replotData();
}

void EosExplorerWidget::onNormalizeToggleClicked() {
  m_isNormalizedT3 = m_chkNormalizeT3->isChecked();
  replotData();
}

void EosExplorerWidget::onExportClicked() {
  if (m_allSeries.isEmpty()) {
    QMessageBox::warning(this, "No Data", "There is no data to export.");
    return;
  }

  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Export Chart");
  msgBox.setText("Select export format:");
  QPushButton *btnPdf = msgBox.addButton("Export as PDF", QMessageBox::ActionRole);
  QPushButton *btnTxt = msgBox.addButton("Export as TXT", QMessageBox::ActionRole);
  msgBox.addButton(QMessageBox::Cancel);
  msgBox.exec();

  if (msgBox.clickedButton() == btnPdf) {
    QString fileName = QFileDialog::getSaveFileName(this, "Save PDF", m_workingDir, "PDF Files (*.pdf)");
    if (fileName.isEmpty()) return;

    QPdfWriter writer(fileName);
    writer.setCreator("Cosmic Trajectories");
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageOrientation(QPageLayout::Landscape);
    
    QPainter painter(&writer);
    int activeIdx = m_chartTabs->currentIndex();
    if (activeIdx >= 0 && activeIdx < NUM_CHARTS) {
      m_chartViews[activeIdx]->render(&painter);
    }
    painter.end();
    
    QMessageBox::information(this, "Success", "Active chart successfully exported to PDF.");
  } 
  else if (msgBox.clickedButton() == btnTxt) {
    QString fileName = QFileDialog::getSaveFileName(this, "Save TXT", m_workingDir, "Text Files (*.txt)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QMessageBox::critical(this, "Error", "Could not open file for writing.");
      return;
    }

    QTextStream out(&file);
    // Canonical Single-Run format. EoS Explorer doesn't compute leptons or
    // lepton chem-pots, so those columns are written as 0; s_QCD and s_tot
    // are identical here (no lepton contribution).
    out << "T\tmuB\tmuQ\tmunue\tmunumu\tmnutau\tnB\tnQ\ts_QCD\ts_tot"
           "\tne\tnmu\tntau\tnnue\tnnumu\tnnutau\n";

    for (const auto &series : m_allSeries) {
      out << QString("# T-scan (muB=%1, muQ=%2)\n")
                  .arg(series.muB).arg(series.muQ);
      const int n = series.nB_points.size();
      for (int i = 0; i < n; ++i) {
        const double T  = series.nB_points[i].x();
        const double nB = series.nB_points[i].y();
        const double nQ = (i < series.nQ_points.size()) ? series.nQ_points[i].y() : 0.0;
        const double s  = (i < series.s_points.size())  ? series.s_points[i].y()  : 0.0;
        out << T
            << "\t" << series.muB << "\t" << series.muQ
            << "\t" << 0.0 << "\t" << 0.0 << "\t" << 0.0           // munue, munumu, mnutau
            << "\t" << nB << "\t" << nQ << "\t" << s << "\t" << s  // s_QCD == s_tot
            << "\t" << 0.0 << "\t" << 0.0 << "\t" << 0.0           // ne, nmu, ntau
            << "\t" << 0.0 << "\t" << 0.0 << "\t" << 0.0           // nnue, nnumu, nnutau
            << "\n";
      }
    }

    file.close();
    QMessageBox::information(this, "Success", "EoS data exported in Single Run format.");
  }
}

QColor EosExplorerWidget::nextColor(int &idx) {
  static const QList<QColor> colors = {
    Qt::blue,
    Qt::red,
    QColor(255, 165, 0), // Orange
    Qt::green,
    QColor(128, 0, 128), // Purple
    Qt::cyan,
    Qt::magenta,
    QColor(165, 42, 42)  // Brown
  };
  QColor c = colors[idx % colors.size()];
  idx++;
  return c;
}
