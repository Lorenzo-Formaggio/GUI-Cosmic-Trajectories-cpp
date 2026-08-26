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
#include <QFontDialog>
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
constexpr const char* EosExplorerWidget::CHART_YNAMES[NUM_CHARTS];
constexpr const char* EosExplorerWidget::CHART_YLABELS[NUM_CHARTS];
constexpr const char* EosExplorerWidget::CHART_YLABELS_NORM[NUM_CHARTS];

EosExplorerWidget::EosExplorerWidget(const QString &workingDir, QWidget *parent)
    : QWidget(parent), m_workingDir(workingDir) {
  setupUi();
}

// ── Helpers: human-readable label / unit for the active scan variable ──────
QString EosExplorerWidget::scanVarLabel() const {
  int idx = m_comboScanVar ? m_comboScanVar->currentIndex() : 0;
  switch (idx) {
    case 1:  return QStringLiteral("µB");
    case 2:  return QStringLiteral("µQ");
    default: return QStringLiteral("Temperature");
  }
}

QString EosExplorerWidget::scanVarUnit() const {
  return QStringLiteral("[MeV]");
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
  m_comboEos->addItems({"Free QGP (0)", "Lattice QCD (1)", "Interpolated Table (2)", "Entropy Contour (3)", "Entropy Contour Param (4)"});
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

  // ── Scan Variable Selector ─────────────────────────────────────────
  m_comboScanVar = new QComboBox();
  m_comboScanVar->addItems({"Temperature (T)", "Baryon Chem. Pot. (µB)", "Charge Chem. Pot. (µQ)"});
  connect(m_comboScanVar, &QComboBox::currentIndexChanged, this, &EosExplorerWidget::onScanVarChanged);
  grid->addWidget(new QLabel("Scan Variable:"), row, 0);
  grid->addWidget(m_comboScanVar, row++, 1);

  // ── Fixed Parameters (two of {T, µB, µQ}) ─────────────────────────
  auto makeSpin = [](double val, double min, double max, int decimals) -> QDoubleSpinBox* {
    QDoubleSpinBox *spin = new QDoubleSpinBox();
    spin->setDecimals(decimals);
    spin->setRange(min, max);
    spin->setValue(val);
    return spin;
  };

  m_labelFixed1 = new QLabel("Fix µB [MeV]:");
  m_spinFixed1 = makeSpin(0.0, -10000.0, 10000.0, 2);
  grid->addWidget(m_labelFixed1, row, 0);
  grid->addWidget(m_spinFixed1, row++, 1);

  m_labelFixed2 = new QLabel("Fix µQ [MeV]:");
  m_spinFixed2 = makeSpin(0.0, -10000.0, 10000.0, 2);
  grid->addWidget(m_labelFixed2, row, 0);
  grid->addWidget(m_spinFixed2, row++, 1);

  // ── Scan Range ─────────────────────────────────────────────────────
  grid->addWidget(new QLabel("── Scan Range ──"), row++, 0, 1, 2, Qt::AlignCenter);

  m_labelScanMin = new QLabel("T min [MeV]:");
  m_spinScanMin = makeSpin(80.0, 0.1, 10000.0, 1);
  grid->addWidget(m_labelScanMin, row, 0);
  grid->addWidget(m_spinScanMin, row++, 1);

  m_labelScanMax = new QLabel("T max [MeV]:");
  m_spinScanMax = makeSpin(250.0, 0.1, 10000.0, 1);
  grid->addWidget(m_labelScanMax, row, 0);
  grid->addWidget(m_spinScanMax, row++, 1);

  m_labelScanStep = new QLabel("T step [MeV]:");
  m_spinDScan = makeSpin(2.0, 0.01, 100.0, 2);
  grid->addWidget(m_labelScanStep, row, 0);
  grid->addWidget(m_spinDScan, row++, 1);

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
    chart->setTitle(QString("%1 vs %2").arg(CHART_YNAMES[i], scanVarLabel()));
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

  QAction *actAxisFont = plotMenu->addAction("Configure Axis Fonts...");
  connect(actAxisFont, &QAction::triggered, this, &EosExplorerWidget::onConfigureAxisFontsClicked);

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

  // Fire once so labels are correct for the default scan variable (T)
  onScanVarChanged(0);
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

  m_axesX[chartIdx]->setTitleText(scanVarLabel() + " " + scanVarUnit());
  m_axesY[chartIdx]->setTitleText(m_isNormalizedT3 ? CHART_YLABELS_NORM[chartIdx] : CHART_YLABELS[chartIdx]);

  // Update chart title dynamically
  chart->setTitle(QString("%1 vs %2").arg(CHART_YNAMES[chartIdx], scanVarLabel()));

  chart->addAxis(m_axesX[chartIdx], Qt::AlignBottom);
  chart->addAxis(m_axesY[chartIdx], Qt::AlignLeft);

  if (m_axisFontValid) {
    m_axesX[chartIdx]->setLabelsFont(m_axisFont);
    m_axesX[chartIdx]->setTitleFont(m_axisFont);
    m_axesY[chartIdx]->setLabelsFont(m_axisFont);
    m_axesY[chartIdx]->setTitleFont(m_axisFont);
  }

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
      for (int j = 0; j < points.size(); ++j) {
        const auto& pt = points[j];
        double y = pt.y();
        if (m_isNormalizedT3 && j < data.T_values.size()) {
            double T = data.T_values[j];
            double T3 = T * T * T;
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
      const double vMin = m_spinScanMin ? m_spinScanMin->value() : 80.0;
      const double vMax = m_spinScanMax ? m_spinScanMax->value() : 250.0;
      m_axesX[chartIdx]->setRange(vMin, vMax);
      m_axesY[chartIdx]->setRange(0.0, 1.0);
    }
  }
}

void EosExplorerWidget::onEosChanged(int index) {
  bool isTable = (index == 2);
  m_labelEosPath->setVisible(isTable);
  m_eosPathWidget->setVisible(isTable);
}

void EosExplorerWidget::onScanVarChanged(int index) {
  // index: 0=T, 1=µB, 2=µQ
  // Update the two fixed-parameter labels and the scan range labels
  switch (index) {
    case 0: // scan T → fix µB, µQ
      m_labelFixed1->setText("Fix µB [MeV]:");
      m_spinFixed1->setValue(0.0);
      m_spinFixed1->setRange(-10000.0, 10000.0);
      m_labelFixed2->setText("Fix µQ [MeV]:");
      m_spinFixed2->setValue(0.0);
      m_spinFixed2->setRange(-10000.0, 10000.0);
      m_labelScanMin->setText("T min [MeV]:");
      m_spinScanMin->setValue(80.0);
      m_spinScanMin->setRange(0.1, 10000.0);
      m_labelScanMax->setText("T max [MeV]:");
      m_spinScanMax->setValue(250.0);
      m_spinScanMax->setRange(0.1, 10000.0);
      m_labelScanStep->setText("T step [MeV]:");
      m_spinDScan->setValue(2.0);
      break;
    case 1: // scan µB → fix T, µQ
      m_labelFixed1->setText("Fix T [MeV]:");
      m_spinFixed1->setValue(150.0);
      m_spinFixed1->setRange(0.1, 10000.0);
      m_labelFixed2->setText("Fix µQ [MeV]:");
      m_spinFixed2->setValue(0.0);
      m_spinFixed2->setRange(-10000.0, 10000.0);
      m_labelScanMin->setText("µB min [MeV]:");
      m_spinScanMin->setValue(0.0);
      m_spinScanMin->setRange(-10000.0, 10000.0);
      m_labelScanMax->setText("µB max [MeV]:");
      m_spinScanMax->setValue(500.0);
      m_spinScanMax->setRange(-10000.0, 10000.0);
      m_labelScanStep->setText("µB step [MeV]:");
      m_spinDScan->setValue(5.0);
      break;
    case 2: // scan µQ → fix T, µB
      m_labelFixed1->setText("Fix T [MeV]:");
      m_spinFixed1->setValue(150.0);
      m_spinFixed1->setRange(0.1, 10000.0);
      m_labelFixed2->setText("Fix µB [MeV]:");
      m_spinFixed2->setValue(0.0);
      m_spinFixed2->setRange(-10000.0, 10000.0);
      m_labelScanMin->setText("µQ min [MeV]:");
      m_spinScanMin->setValue(-200.0);
      m_spinScanMin->setRange(-10000.0, 10000.0);
      m_labelScanMax->setText("µQ max [MeV]:");
      m_spinScanMax->setValue(200.0);
      m_spinScanMax->setRange(-10000.0, 10000.0);
      m_labelScanStep->setText("µQ step [MeV]:");
      m_spinDScan->setValue(5.0);
      break;
  }

  // Update chart titles to reflect the new scan variable
  for (int i = 0; i < NUM_CHARTS; ++i) {
    if (m_chartViews[i] && m_chartViews[i]->chart()) {
      m_chartViews[i]->chart()->setTitle(
        QString("%1 vs %2").arg(CHART_YNAMES[i], scanVarLabel()));
    }
  }
  rebuildAllAxes();
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
    int scanVar = m_comboScanVar->currentIndex(); // 0=T, 1=µB, 2=µQ
    
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
          logMessage(QString("  Table loaded. T range: %1 – %2 MeV")
                            .arg(InterpolatedEoS::getTmin(), 0, 'f', 1)
                            .arg(InterpolatedEoS::getTmax(), 0, 'f', 1));
      }
    }

    logMessage(QString("Setting EoS: %1").arg(eosName));

    // Use the logic already in QCDTherm.cpp for setting up the EoS
    std::string baseDir = m_workingDir.toStdString();
    QCD::setEoS(eos, baseDir, nf);

    // ── Resolve the three thermodynamic variables from the UI ──────
    double fixedVal1 = m_spinFixed1->value();
    double fixedVal2 = m_spinFixed2->value();
    double scanMin = m_spinScanMin->value();
    double scanMax = m_spinScanMax->value();
    double dScan   = m_spinDScan->value();

    // Map fixed values to T, muB, muQ depending on scan variable
    double fixedT = 0, fixedMuB = 0, fixedMuQ = 0;
    switch (scanVar) {
      case 0: // scan T → fixed1=µB, fixed2=µQ
        fixedMuB = fixedVal1; fixedMuQ = fixedVal2; break;
      case 1: // scan µB → fixed1=T, fixed2=µQ
        fixedT = fixedVal1; fixedMuQ = fixedVal2; break;
      case 2: // scan µQ → fixed1=T, fixed2=µB
        fixedT = fixedVal1; fixedMuB = fixedVal2; break;
    }

    // Clamp scan range to the loaded table range when scanning T (warn if outside)
    if (scanVar == 0 && eos == 2 && InterpolatedEoS::isLoaded()) {
      double tableTmin = InterpolatedEoS::getTmin();
      double tableTmax = InterpolatedEoS::getTmax();

      if (scanMin < tableTmin) {
        logMessage(QString("<font color='#ffc107'><b>Warning:</b> Requested Tmin (%1 MeV) is below table minimum (%2 MeV). Clamping to table minimum.</font>")
                       .arg(scanMin, 0, 'f', 1).arg(tableTmin, 0, 'f', 1));
        scanMin = tableTmin;
      } else if (scanMin > tableTmax) {
        logMessage(QString("<font color='#ffc107'><b>Warning:</b> Requested Tmin (%1 MeV) exceeds table maximum (%2 MeV). Clamping to table maximum.</font>")
                       .arg(scanMin, 0, 'f', 1).arg(tableTmax, 0, 'f', 1));
        scanMin = tableTmax;
      }

      if (scanMax > tableTmax) {
        logMessage(QString("<font color='#ffc107'><b>Warning:</b> Requested Tmax (%1 MeV) exceeds table maximum (%2 MeV). Clamping to table maximum.</font>")
                       .arg(scanMax, 0, 'f', 1).arg(tableTmax, 0, 'f', 1));
        scanMax = tableTmax;
      } else if (scanMax < tableTmin) {
        logMessage(QString("<font color='#ffc107'><b>Warning:</b> Requested Tmax (%1 MeV) is below table minimum (%2 MeV). Clamping to table minimum.</font>")
                       .arg(scanMax, 0, 'f', 1).arg(tableTmin, 0, 'f', 1));
        scanMax = tableTmin;
      }
    }

    // Build human-readable description
    static const QStringList scanNames = {"T", "µB", "µQ"};
    QString scanName = scanNames[scanVar];
    
    QString fixedDesc;
    switch (scanVar) {
      case 0: fixedDesc = QString("µB=%1, µQ=%2").arg(fixedMuB).arg(fixedMuQ); break;
      case 1: fixedDesc = QString("T=%1, µQ=%2").arg(fixedT).arg(fixedMuQ); break;
      case 2: fixedDesc = QString("T=%1, µB=%2").arg(fixedT).arg(fixedMuB); break;
    }
    logMessage(QString("Parameters: %1").arg(fixedDesc));
    
    if (scanMin > scanMax) std::swap(scanMin, scanMax);
    if (dScan <= 0) dScan = 1.0;
    int totalSteps = static_cast<int>(std::abs(scanMax - scanMin) / dScan) + 1;

    logMessage("─────────────────────────────────────────");
    logMessage(QString("Scanning %1 = %2 → %3 MeV  (step %4 MeV, %5 steps)")
                        .arg(scanName)
                        .arg(scanMin, 0, 'f', 1)
                        .arg(scanMax, 0, 'f', 1)
                        .arg(dScan, 0, 'f', 2)
                        .arg(totalSteps));

    QString seriesName = QString("%1-scan (%2)").arg(scanName, fixedDesc);
    
    SeriesData newData;
    newData.label = seriesName;
    newData.scanVar = scanVar;
    newData.fixedT = fixedT;
    newData.muB = fixedMuB;
    newData.muQ = fixedMuQ;

    QLineSeries *series_arr[NUM_CHARTS];
    QColor c = nextColor(m_colorIndex);

    for (int i = 0; i < NUM_CHARTS; ++i) {
      series_arr[i] = new QLineSeries();
      series_arr[i]->setName(seriesName);
      series_arr[i]->setColor(c);
    }

    int count = 0;
    for (double v = scanMin; v <= scanMax; v += dScan) {
      // Resolve T, muB, muQ for this scan step
      double T, muB, muQ;
      switch (scanVar) {
        case 0: T = v; muB = fixedMuB; muQ = fixedMuQ; break;
        case 1: T = fixedT; muB = v; muQ = fixedMuQ; break;
        case 2: T = fixedT; muB = fixedMuB; muQ = v; break;
        default: T = v; muB = fixedMuB; muQ = fixedMuQ; break;
      }

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
        series_arr[i]->append(v, yVal);
      }
      
      // Store raw data with scan variable as x
      newData.nB_points.append(QPointF(v, nB));
      newData.nQ_points.append(QPointF(v, nQ));
      newData.s_points.append(QPointF(v, s));
      newData.T_values.append(T);

      // Pressure and energy density (QCD sector only; no leptons in EoS Explorer)
      double p = QCD::pQCD(muB, muQ, T, nf);
      double e = QCD::eQCD(muB, muQ, T, nf);
      newData.p_QCD.append(p);
      newData.e_QCD.append(e);
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
        for (int j = 0; j < points.size(); ++j) {
          const auto& pt = points[j];
          double y = pt.y();
          if (m_isNormalizedT3 && j < m_allSeries[i].T_values.size()) {
              double T = m_allSeries[i].T_values[j];
              double T3 = T * T * T;
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
           "\tp_QCD\tp_tot\te_QCD\te_tot"
           "\tne\tnmu\tntau\tnnue\tnnumu\tnnutau\n";

    for (const auto &series : m_allSeries) {
      out << QString("# %1\n").arg(series.label);
      const int n = series.nB_points.size();
      for (int i = 0; i < n; ++i) {
        // Reconstruct T, muB, muQ from the stored data
        double T, muB, muQ;
        switch (series.scanVar) {
          case 0: // scan T
            T = series.nB_points[i].x();
            muB = series.muB;
            muQ = series.muQ;
            break;
          case 1: // scan µB
            T = series.fixedT;
            muB = series.nB_points[i].x();
            muQ = series.muQ;
            break;
          case 2: // scan µQ
            T = series.fixedT;
            muB = series.muB;
            muQ = series.nB_points[i].x();
            break;
          default:
            T = series.nB_points[i].x();
            muB = series.muB;
            muQ = series.muQ;
            break;
        }
        const double nB = series.nB_points[i].y();
        const double nQ = (i < series.nQ_points.size()) ? series.nQ_points[i].y() : 0.0;
        const double s  = (i < series.s_points.size())  ? series.s_points[i].y()  : 0.0;
        const double p  = (i < series.p_QCD.size())     ? series.p_QCD[i]         : 0.0;
        const double e  = (i < series.e_QCD.size())     ? series.e_QCD[i]         : 0.0;
        out << T
            << "\t" << muB << "\t" << muQ
            << "\t" << 0.0 << "\t" << 0.0 << "\t" << 0.0           // munue, munumu, mnutau
            << "\t" << nB << "\t" << nQ << "\t" << s << "\t" << s  // s_QCD == s_tot
            << "\t" << p << "\t" << p                              // p_QCD == p_tot (no leptons)
            << "\t" << e << "\t" << e                              // e_QCD == e_tot (no leptons)
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

void EosExplorerWidget::onConfigureAxisFontsClicked() {
    bool ok = false;
    QFont currentFont = this->font();
    if (m_axesX[0]) {
        currentFont = m_axesX[0]->labelsFont();
    }
    QFont font = QFontDialog::getFont(&ok, currentFont, this);
    if (ok) {
        m_axisFont = font;
        m_axisFontValid = true;
        applyAxisFonts();
    }
}

void EosExplorerWidget::applyAxisFonts() {
    if (!m_axisFontValid) return;
    for (int i = 0; i < NUM_CHARTS; ++i) {
        if (m_axesX[i]) {
            m_axesX[i]->setLabelsFont(m_axisFont);
            m_axesX[i]->setTitleFont(m_axisFont);
        }
        if (m_axesY[i]) {
            m_axesY[i]->setLabelsFont(m_axisFont);
            m_axesY[i]->setTitleFont(m_axisFont);
        }
    }
}
