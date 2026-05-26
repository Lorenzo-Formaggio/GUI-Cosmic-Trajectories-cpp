#include "RunFromFileWidget.h"

#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QTableWidget>
#include <QHeaderView>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QLabel>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QTabWidget>
#include <QTextEdit>
#include <QProgressBar>
#include <QDialog>
#include <QMessageBox>
#include <QFontDialog>
#include <QFileDialog>
#include <QColorDialog>
#include <QThread>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QPainter>
#include <QRegularExpression>
#include <QBrush>
#include <QPdfWriter>
#include <QPageSize>
#include <QPageLayout>
#include <QMenu>
#include <QAction>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QLogValueAxis>
#include <QtCharts/QLegendMarker>

#include <cmath>
#include <algorithm>

// ── Default palette for new sources ────────────────────────────────────────
static const QColor DEFAULT_COLORS[] = {
    QColor( 31, 119, 180),  // Blue
    QColor(214,  39,  40),  // Red
    QColor( 44, 160,  44),  // Green
    QColor(255, 127,  14),  // Orange
    QColor(148, 103, 189),  // Purple
    QColor(140,  86,  75),  // Brown
    QColor(227, 119, 194),  // Pink
    QColor(127, 127, 127),  // Gray
};
static constexpr int NUM_DEFAULT_COLORS = sizeof(DEFAULT_COLORS) / sizeof(DEFAULT_COLORS[0]);

// Table columns
enum TableColumn {
  COL_FILE = 0,
  COL_BROWSE_FILE,
  COL_EOS,
  COL_NF,
  COL_EOS_TABLE,
  COL_LINE_STYLE,
  COL_COLOR,
  COL_STATUS,
  NUM_COLS
};

// ──────────────────────────────────────────────────────────────────────────
RunFromFileWidget::RunFromFileWidget(const QString &workingDir, QWidget *parent)
    : QWidget(parent), m_workingDir(workingDir) {
  setupUi();
}

RunFromFileWidget::~RunFromFileWidget() {
  teardownWorker();
}

void RunFromFileWidget::teardownWorker() {
  if (m_workerThread) {
    if (m_worker) m_worker->stop();
    m_workerThread->quit();
    m_workerThread->wait();
    delete m_worker;
    delete m_workerThread;
    m_worker = nullptr;
    m_workerThread = nullptr;
  }
}

// ── UI ─────────────────────────────────────────────────────────────────────
void RunFromFileWidget::setupUi() {
  QHBoxLayout *outerLayout = new QHBoxLayout(this);
  outerLayout->setContentsMargins(4, 4, 4, 4);

  QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
  outerLayout->addWidget(splitter);

  // ── Left panel: settings + sources table + console ────────────────
  QSplitter *leftPanel = new QSplitter(Qt::Vertical);

  QWidget *topLeftPanel = new QWidget(leftPanel);
  QVBoxLayout *topLeftLayout = new QVBoxLayout(topLeftPanel);
  topLeftLayout->setContentsMargins(0, 0, 0, 0);

  // Common settings
  QGroupBox *groupCommon = new QGroupBox("Common Settings", topLeftPanel);
  QGridLayout *gridCommon = new QGridLayout(groupCommon);

  int row = 0;
  auto addSpin = [&](const QString &lbl, double val, double lo, double hi, int dec) {
    gridCommon->addWidget(new QLabel(lbl), row, 0);
    QDoubleSpinBox *sp = new QDoubleSpinBox();
    sp->setDecimals(dec);
    sp->setRange(lo, hi);
    sp->setValue(val);
    gridCommon->addWidget(sp, row, 1);
    row++;
    return sp;
  };

  m_spinTmin = addSpin("Tmin (MeV)", 80.0,   0.1,  10000.0, 1);
  m_spinTmax = addSpin("Tmax (MeV)", 250.0,  0.1,  10000.0, 1);
  m_spinDT   = addSpin("dT (MeV)",   1.0,     0.01, 100.0,   2);

  gridCommon->addWidget(new QLabel("Scan Direction"), row, 0);
  m_comboScan = new QComboBox();
  m_comboScan->addItems({"Low → High (0)", "High → Low (1)"});
  gridCommon->addWidget(m_comboScan, row++, 1);

  gridCommon->addWidget(new QLabel("Guess Method"), row, 0);
  m_comboGuess = new QComboBox();
  m_comboGuess->addItems({"Constant (0)", "Linear Extrap (1)"});
  m_comboGuess->setCurrentIndex(1);
  gridCommon->addWidget(m_comboGuess, row++, 1);

  m_btnSolverSettings = new QPushButton("Solver Settings...");
  m_btnSolverSettings->setObjectName("BtnSolver");
  connect(m_btnSolverSettings, &QPushButton::clicked, this, &RunFromFileWidget::onSolverSettingsClicked);
  gridCommon->addWidget(m_btnSolverSettings, row++, 0, 1, 2);

  topLeftLayout->addWidget(groupCommon);

  // Sources table
  QGroupBox *groupSources = new QGroupBox("Trajectory Sources", topLeftPanel);
  QVBoxLayout *srcLayout = new QVBoxLayout(groupSources);

  m_table = new QTableWidget(0, NUM_COLS, groupSources);
  QStringList headers;
  headers << "File" << "" << "EoS" << "nf" << "EoS Table" << "Line Style" << "Color" << "Status";
  m_table->setHorizontalHeaderLabels(headers);
  m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  m_table->horizontalHeader()->setStretchLastSection(true);
  m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setSelectionMode(QAbstractItemView::SingleSelection);
  m_table->setColumnWidth(COL_FILE,        160);
  m_table->setColumnWidth(COL_BROWSE_FILE, 28);
  m_table->setColumnWidth(COL_EOS,         150);
  m_table->setColumnWidth(COL_NF,          50);
  m_table->setColumnWidth(COL_EOS_TABLE,   150);
  m_table->setColumnWidth(COL_LINE_STYLE,  90);
  m_table->setColumnWidth(COL_COLOR,       70);
  
  // Allow the table to shrink so the splitters can be resized
  m_table->setMinimumWidth(200);
  m_table->setMinimumHeight(100);
  
  srcLayout->addWidget(m_table);

  QHBoxLayout *tblBtnLayout = new QHBoxLayout();
  m_btnAdd = new QPushButton("Add Source");
  connect(m_btnAdd, &QPushButton::clicked, this, &RunFromFileWidget::onAddSource);
  tblBtnLayout->addWidget(m_btnAdd);

  m_btnRemove = new QPushButton("Remove Selected");
  connect(m_btnRemove, &QPushButton::clicked, this, &RunFromFileWidget::onRemoveSource);
  tblBtnLayout->addWidget(m_btnRemove);

  tblBtnLayout->addStretch();

  m_btnRun = new QPushButton("Run All");
  m_btnRun->setObjectName("BtnRun");
  connect(m_btnRun, &QPushButton::clicked, this, &RunFromFileWidget::onRunAll);
  tblBtnLayout->addWidget(m_btnRun);

  m_btnStop = new QPushButton("Stop");
  m_btnStop->setObjectName("BtnStop");
  connect(m_btnStop, &QPushButton::clicked, this, &RunFromFileWidget::onStopAll);
  tblBtnLayout->addWidget(m_btnStop);

  m_btnClear = new QPushButton("Clear Plots");
  connect(m_btnClear, &QPushButton::clicked, this, &RunFromFileWidget::onClearAll);
  tblBtnLayout->addWidget(m_btnClear);

  srcLayout->addLayout(tblBtnLayout);
  topLeftLayout->addWidget(groupSources, 1);

  // Console
  QGroupBox *groupConsole = new QGroupBox("Console", leftPanel);
  QVBoxLayout *consoleLayout = new QVBoxLayout(groupConsole);

  m_statusLabel = new QLabel("Ready");
  m_statusLabel->setStyleSheet("color: #17a2b8; font-weight: bold;");
  consoleLayout->addWidget(m_statusLabel);

  m_progressBar = new QProgressBar();
  m_progressBar->setValue(0);
  consoleLayout->addWidget(m_progressBar);

  m_console = new QTextEdit();
  m_console->setReadOnly(true);
  m_console->setFontFamily("Courier");
  m_console->setStyleSheet("background-color: #1e1e1e; color: #ffffff; border: 1px solid #333;");
  m_console->setMinimumHeight(120);
  consoleLayout->addWidget(m_console);

  // Console group is directly managed by the leftPanel splitter
  leftPanel->addWidget(topLeftPanel);
  leftPanel->addWidget(groupConsole);
  leftPanel->setSizes({400, 200}); // Default vertical sizes

  // ── Right panel: charts ───────────────────────────────────────────
  QWidget *rightPanel = new QWidget();
  QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
  rightLayout->setContentsMargins(0, 0, 0, 0);

  createChartPanel(rightPanel);
  rightLayout->addWidget(m_chartTabs);


  // ── Plot Settings Menu (Inherits Global Style) ───────────────────────────
  QPushButton *btnPlotSettings = new QPushButton("Plot Settings", rightPanel);
  
  QMenu *plotMenu = new QMenu(this);

  // Initialize visibility checkboxes early to avoid null pointer issues.
  // They live on `this` until the dialog reparents them, so hide them now to
  // keep them from showing as floating widgets in the corner.
  auto makeChk = [this](const QString &label, bool checked) -> QCheckBox* {
    QCheckBox *chk = new QCheckBox(label, this);
    chk->setChecked(checked);
    chk->hide();
    connect(chk, &QCheckBox::toggled, this, [this]{ updateSeriesVisibility(); });
    return chk;
  };
  m_chknB      = makeChk("nB",     true);
  m_chkS       = makeChk("s",      true);
  m_chknQ      = makeChk("nQ", true);
  m_chkNe      = makeChk("ne",     true);
  m_chkNmu     = makeChk("nμ",     true);
  m_chkNtau    = makeChk("nτ",     true);
  m_chkNnue    = makeChk("nνe",    true);
  m_chkNnumu   = makeChk("nνμ",    true);
  m_chkNnutau  = makeChk("nντ",    true);
  m_chkMuB     = makeChk("μB",     true);
  m_chkMuQ     = makeChk("μQ",     true);
  m_chkMunue   = makeChk("μνe",    true);
  m_chkMunumu  = makeChk("μνμ",    true);
  m_chkMnutau  = makeChk("μντ",    true);

  // Per-quantity abs checkboxes
  auto makeAbs = [this](bool checked) -> QCheckBox* {
    QCheckBox *chk = new QCheckBox("|·|", this);
    chk->setChecked(checked);
    chk->setToolTip("Plot absolute value of this quantity. Forced ON in log mode.");
    chk->setEnabled(!m_isLogScale);
    chk->hide();
    return chk;
  };
  m_absnB     = makeAbs(m_useAbsnB);
  m_absS      = makeAbs(m_useAbsS);
  m_absnQ     = makeAbs(m_useAbsnQ);
  m_absNe     = makeAbs(m_useAbsNe);
  m_absNmu    = makeAbs(m_useAbsNmu);
  m_absNtau   = makeAbs(m_useAbsNtau);
  m_absNnue   = makeAbs(m_useAbsNnue);
  m_absNnumu  = makeAbs(m_useAbsNnumu);
  m_absNnutau = makeAbs(m_useAbsNnutau);
  m_absMuB    = makeAbs(m_useAbsMuB);
  m_absMuQ    = makeAbs(m_useAbsMuQ);
  m_absMunue  = makeAbs(m_useAbsMunue);
  m_absMunumu = makeAbs(m_useAbsMunumu);
  m_absMnutau = makeAbs(m_useAbsMnutau);

  // Wire abs checkboxes — replot from cached data
  connect(m_absnB,     &QCheckBox::toggled, this, [this](bool on){ m_useAbsnB = on;     replotAll(); });
  connect(m_absS,      &QCheckBox::toggled, this, [this](bool on){ m_useAbsS = on;      replotAll(); });
  connect(m_absnQ,     &QCheckBox::toggled, this, [this](bool on){ m_useAbsnQ = on;     replotAll(); });
  connect(m_absNe,     &QCheckBox::toggled, this, [this](bool on){ m_useAbsNe = on;     replotAll(); });
  connect(m_absNmu,    &QCheckBox::toggled, this, [this](bool on){ m_useAbsNmu = on;    replotAll(); });
  connect(m_absNtau,   &QCheckBox::toggled, this, [this](bool on){ m_useAbsNtau = on;   replotAll(); });
  connect(m_absNnue,   &QCheckBox::toggled, this, [this](bool on){ m_useAbsNnue = on;   replotAll(); });
  connect(m_absNnumu,  &QCheckBox::toggled, this, [this](bool on){ m_useAbsNnumu = on;  replotAll(); });
  connect(m_absNnutau, &QCheckBox::toggled, this, [this](bool on){ m_useAbsNnutau = on; replotAll(); });
  connect(m_absMuB,    &QCheckBox::toggled, this, [this](bool on){ m_useAbsMuB = on;    replotAll(); });
  connect(m_absMuQ,    &QCheckBox::toggled, this, [this](bool on){ m_useAbsMuQ = on;    replotAll(); });
  connect(m_absMunue,  &QCheckBox::toggled, this, [this](bool on){ m_useAbsMunue = on;  replotAll(); });
  connect(m_absMunumu, &QCheckBox::toggled, this, [this](bool on){ m_useAbsMunumu = on; replotAll(); });
  connect(m_absMnutau, &QCheckBox::toggled, this, [this](bool on){ m_useAbsMnutau = on; replotAll(); });

  // Section: Visibility
  QAction *actShowHide = plotMenu->addAction("Show/Hide Quantities...");
  connect(actShowHide, &QAction::triggered, this, [this]() {
    if (!m_visDialog) {
      m_visDialog = new QDialog(this);
      m_visDialog->setWindowTitle("Show/Hide Series Across All Sources");
      m_visDialog->setMinimumWidth(420);
      QVBoxLayout *dlgLayout = new QVBoxLayout(m_visDialog);

      auto addRow = [](QGridLayout *grid, int row, QCheckBox *vis, QCheckBox *abs) {
        grid->addWidget(vis, row, 0); vis->show();
        grid->addWidget(abs, row, 1); abs->show();
      };

      // ── Densities group ────────────────────────────────────
      QGroupBox *grpDens = new QGroupBox("Densities");
      QGridLayout *layDens = new QGridLayout(grpDens);
      addRow(layDens, 0, m_chknB, m_absnB);
      addRow(layDens, 1, m_chkS,  m_absS);
      addRow(layDens, 2, m_chknQ, m_absnQ);
      dlgLayout->addWidget(grpDens);

      // ── Lepton Densities group ────────────────────────────
      QGroupBox *grpLepDens = new QGroupBox("Lepton Densities");
      QGridLayout *layLepDens = new QGridLayout(grpLepDens);
      addRow(layLepDens, 0, m_chkNe,     m_absNe);
      addRow(layLepDens, 1, m_chkNmu,    m_absNmu);
      addRow(layLepDens, 2, m_chkNtau,   m_absNtau);
      addRow(layLepDens, 3, m_chkNnue,   m_absNnue);
      addRow(layLepDens, 4, m_chkNnumu,  m_absNnumu);
      addRow(layLepDens, 5, m_chkNnutau, m_absNnutau);
      dlgLayout->addWidget(grpLepDens);

      // ── Chemical Potentials group ──────────────────────────
      QGroupBox *grpMu = new QGroupBox("Chemical Potentials");
      QGridLayout *layMu = new QGridLayout(grpMu);
      addRow(layMu, 0, m_chkMuB, m_absMuB);
      addRow(layMu, 1, m_chkMuQ, m_absMuQ);
      dlgLayout->addWidget(grpMu);

      // ── Lepton Chem. Pot. group ────────────────────────────
      QGroupBox *grpLep = new QGroupBox("Lepton Chemical Potentials");
      QGridLayout *layLep = new QGridLayout(grpLep);
      addRow(layLep, 0, m_chkMunue,   m_absMunue);
      addRow(layLep, 1, m_chkMunumu,  m_absMunumu);
      addRow(layLep, 2, m_chkMnutau,  m_absMnutau);
      dlgLayout->addWidget(grpLep);
    }
    auto syncAbs = [this](QCheckBox *c){ if (c) c->setEnabled(!m_isLogScale); };
    syncAbs(m_absnB); syncAbs(m_absS); syncAbs(m_absnQ);
    syncAbs(m_absNe); syncAbs(m_absNmu); syncAbs(m_absNtau);
    syncAbs(m_absNnue); syncAbs(m_absNnumu); syncAbs(m_absNnutau);
    syncAbs(m_absMuB); syncAbs(m_absMuQ);
    syncAbs(m_absMunue); syncAbs(m_absMunumu); syncAbs(m_absMnutau);
    m_visDialog->show();
    m_visDialog->raise();
    m_visDialog->activateWindow();
  });

  plotMenu->addSeparator();

  // Section: View Controls
  QAction *actAxisLimits = plotMenu->addAction("Set Axis Limits...");
  connect(actAxisLimits, &QAction::triggered, this, &RunFromFileWidget::onAxisLimitsClicked);

  QAction *actAxis = plotMenu->addAction("Toggle Axes");
  connect(actAxis, &QAction::triggered, this, &RunFromFileWidget::onAxisToggle);

  QAction *actScale = plotMenu->addAction("Toggle Log/Linear");
  connect(actScale, &QAction::triggered, this, &RunFromFileWidget::onScaleToggle);

  QAction *actTheme = plotMenu->addAction("Toggle Plot Theme");
  connect(actTheme, &QAction::triggered, this, &RunFromFileWidget::onThemeToggle);

  QAction *actAxisFont = plotMenu->addAction("Configure Axis Fonts...");
  connect(actAxisFont, &QAction::triggered, this, &RunFromFileWidget::onConfigureAxisFontsClicked);

  // Show/hide chart legend
  QAction *actLegend = plotMenu->addAction("Show Legend");
  actLegend->setCheckable(true);
  actLegend->setChecked(m_legendVisible);
  connect(actLegend, &QAction::toggled, this, [this](bool on) {
    m_legendVisible = on;
    refreshLegendVisibility();
  });

  // Configure which prefix/parameters appear in legend names
  QAction *actLegendCfg = plotMenu->addAction("Configure Legend...");
  connect(actLegendCfg, &QAction::triggered, this, [this]() {
    QDialog dlg(this);
    dlg.setWindowTitle("Legend Content");
    QVBoxLayout *v = new QVBoxLayout(&dlg);
    auto *cTag  = new QCheckBox("Show source tag (\"S\")"); cTag->setChecked(m_legendShowSourceTag);
    v->addWidget(cTag);
    v->addWidget(new QLabel("Append per-row run parameters to legend entries:"));
    auto *cB    = new QCheckBox("B  (baryon number)");        cB->setChecked(m_legendShowB);
    auto *cLe   = new QCheckBox("Le (electron lepton)");      cLe->setChecked(m_legendShowLe);
    auto *cLmu  = new QCheckBox("Lμ (muon lepton)");          cLmu->setChecked(m_legendShowLmu);
    auto *cLtau = new QCheckBox("Lτ (tau lepton)");           cLtau->setChecked(m_legendShowLtau);
    v->addWidget(cB); v->addWidget(cLe); v->addWidget(cLmu); v->addWidget(cLtau);
    QPushButton *ok = new QPushButton("OK");
    connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    v->addWidget(ok);
    if (dlg.exec() == QDialog::Accepted) {
      m_legendShowSourceTag = cTag->isChecked();
      m_legendShowB    = cB->isChecked();
      m_legendShowLe   = cLe->isChecked();
      m_legendShowLmu  = cLmu->isChecked();
      m_legendShowLtau = cLtau->isChecked();
      refreshSeriesNames();
    }
  });

  plotMenu->addSeparator();

  // Section: Export
  QAction *actExportPlot = plotMenu->addAction("Export Active Plot...");
  connect(actExportPlot, &QAction::triggered, this, &RunFromFileWidget::onExportActivePlot);

  QAction *actExportData = plotMenu->addAction("Export Full Data...");
  connect(actExportData, &QAction::triggered, this, &RunFromFileWidget::onExportFullData);

  btnPlotSettings->setMenu(plotMenu);

  // ── Auto-Fit Limits button (next to Plot Settings) ───────────────────
  QPushButton *btnAutoFit = new QPushButton("Auto-Fit Limits", rightPanel);
  btnAutoFit->setToolTip("Fit axis ranges to the currently visible curves "
                         "in the active chart (uses log/linear margins).");
  connect(btnAutoFit, &QPushButton::clicked, this, [this]() {
    QWidget *current = m_chartTabs->currentWidget();
    auto *view = qobject_cast<TooltipChartView*>(current);
    if (view && view->chart()) {
      autoFitChartFromVisibleSeries(view->chart());
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
  splitter->setSizes({500, 700});

  // Seed with a default first source pointing at input/input_traj.txt
  RunSource first;
  first.filePath = m_workingDir + "/input/input_traj.txt";
  first.color    = DEFAULT_COLORS[0];
  first.colorEnd = DEFAULT_COLORS[0].lighter(170);  // visible default gradient
  m_sources.append(first);
  addSourceRow(first);
}

void RunFromFileWidget::createChartPanel(QWidget *parent) {
  m_chartTabs = new QTabWidget(parent);

  auto makeChart = [&](TooltipChartView *&view, QChart *&chart,
                       QAbstractAxis *&axX, QAbstractAxis *&axY,
                       const QString &title, const QString &valLabel) {
    chart = new QChart();
    chart->setTitle(title);
    chart->setTheme(QChart::ChartThemeLight);
    chart->setAnimationOptions(QChart::NoAnimation);
    chart->legend()->setMarkerShape(QLegend::MarkerShapeFromSeries);
    chart->legend()->setAlignment(Qt::AlignRight);

    if (m_isLogScale) {
      axX = new QLogValueAxis();
      static_cast<QLogValueAxis*>(axX)->setBase(10.0);
      static_cast<QLogValueAxis*>(axX)->setLabelFormat("%g");
      axY = new QLogValueAxis();
      static_cast<QLogValueAxis*>(axY)->setBase(10.0);
      static_cast<QLogValueAxis*>(axY)->setLabelFormat("%g");
    } else {
      axX = new QValueAxis();
      static_cast<QValueAxis*>(axX)->setLabelFormat("%g");
      axY = new QValueAxis();
      static_cast<QValueAxis*>(axY)->setLabelFormat("%g");
    }

    axX->setTitleText(m_tempIsVertical ? valLabel : "Temperature [MeV]");
    chart->addAxis(axX, Qt::AlignBottom);
    axY->setTitleText(m_tempIsVertical ? "Temperature [MeV]" : valLabel);
    chart->addAxis(axY, Qt::AlignLeft);

    if (m_axisFontValid) {
      axX->setLabelsFont(m_axisFont);
      axX->setTitleFont(m_axisFont);
      axY->setLabelsFont(m_axisFont);
      axY->setTitleFont(m_axisFont);
    }

    // Default range so the axes render with tick marks before any data is
    // plotted. Once the user runs the batch, updateChartAxes() overrides.
    const double tMin = m_spinTmin ? m_spinTmin->value() : 80.0;
    const double tMax = m_spinTmax ? m_spinTmax->value() : 250.0;
    if (m_isLogScale) {
      axX->setRange(m_tempIsVertical ? 1e-3 : tMin,
                    m_tempIsVertical ? 1e3  : tMax);
      axY->setRange(m_tempIsVertical ? tMin : 1e-3,
                    m_tempIsVertical ? tMax : 1e3);
    } else {
      axX->setRange(m_tempIsVertical ? 0.0 : tMin,
                    m_tempIsVertical ? 1.0 : tMax);
      axY->setRange(m_tempIsVertical ? tMin : 0.0,
                    m_tempIsVertical ? tMax : 1.0);
    }

    view = new TooltipChartView(chart);
    view->setRenderHint(QPainter::Antialiasing);
    m_chartTabs->addTab(view, title);
  };

  QChart *c1, *c2, *c3, *c4;
  makeChart(m_densView, c1, m_densAxisX, m_densAxisY, "Densities",            "Densities [MeV³]");
  makeChart(m_lepDensView, c4, m_lepDensAxisX, m_lepDensAxisY, "Lepton Densities", "Densities [MeV³]");
  makeChart(m_muView,   c2, m_muAxisX,   m_muAxisY,   "Baryon & Electric μ",  "Chem. Pot. [MeV]");
  makeChart(m_lepView,  c3, m_lepAxisX,  m_lepAxisY,  "Lepton μ",             "Chem. Pot. [MeV]");
}

// ── Source row helpers ─────────────────────────────────────────────────────
void RunFromFileWidget::addSourceRow(const RunSource &src) {
  int row = m_table->rowCount();
  m_table->insertRow(row);

  // File path
  QTableWidgetItem *fileItem = new QTableWidgetItem(QFileInfo(src.filePath).fileName());
  fileItem->setToolTip(src.filePath);
  m_table->setItem(row, COL_FILE, fileItem);

  // Browse file
  QToolButton *btnBrowse = new QToolButton();
  btnBrowse->setText("…");
  btnBrowse->setToolTip("Pick parameter file");
  connect(btnBrowse, &QToolButton::clicked, this, [this]() {
    int r = m_table->indexAt(QPoint(0, 0)).row();
    // Find the row by walking up from the sender's parent — use the focused row instead.
    QToolButton *btn = qobject_cast<QToolButton*>(sender());
    if (btn) {
      for (int rr = 0; rr < m_table->rowCount(); ++rr) {
        if (m_table->cellWidget(rr, COL_BROWSE_FILE) == btn) { r = rr; break; }
      }
    }
    onBrowseFile(r);
  });
  m_table->setCellWidget(row, COL_BROWSE_FILE, btnBrowse);

  // EoS combo
  QComboBox *comboEos = new QComboBox();
  comboEos->addItems({"Free QGP (0)", "Lattice QCD (1)", "Interpolated Table (2)", "Entropy Contour (3)", "Entropy Contour Param (4)"});
  comboEos->setCurrentIndex(src.eos);
  connect(comboEos, &QComboBox::currentIndexChanged, this, [this, comboEos](int idx) {
    for (int rr = 0; rr < m_table->rowCount(); ++rr) {
      if (m_table->cellWidget(rr, COL_EOS) == comboEos) { onTableEosChanged(rr, idx); break; }
    }
  });
  m_table->setCellWidget(row, COL_EOS, comboEos);

  // nf combo
  QComboBox *comboNf = new QComboBox();
  comboNf->addItems({"2", "3", "4"});
  comboNf->setCurrentText(QString::number(src.nf));
  m_table->setCellWidget(row, COL_NF, comboNf);

  // EoS table path widget
  QWidget *tblPathWidget = new QWidget();
  QHBoxLayout *tblPathLayout = new QHBoxLayout(tblPathWidget);
  tblPathLayout->setContentsMargins(0, 0, 0, 0);
  QLineEdit *eosPathEdit = new QLineEdit();
  eosPathEdit->setText(src.eosTablePath);
  eosPathEdit->setPlaceholderText("EoS table path…");
  QToolButton *btnBrowseEos = new QToolButton();
  btnBrowseEos->setText("…");
  connect(btnBrowseEos, &QToolButton::clicked, this, [this]() {
    QToolButton *btn = qobject_cast<QToolButton*>(sender());
    int r = -1;
    for (int rr = 0; rr < m_table->rowCount(); ++rr) {
      QWidget *w = m_table->cellWidget(rr, COL_EOS_TABLE);
      if (w && w->findChildren<QToolButton*>().contains(btn)) { r = rr; break; }
    }
    if (r >= 0) onBrowseEosTable(r);
  });
  tblPathLayout->addWidget(eosPathEdit);
  tblPathLayout->addWidget(btnBrowseEos);
  tblPathWidget->setEnabled(src.eos == 2);
  m_table->setCellWidget(row, COL_EOS_TABLE, tblPathWidget);

  // Line style combo
  QComboBox *comboStyle = new QComboBox();
  comboStyle->addItem("Solid",  static_cast<int>(Qt::SolidLine));
  comboStyle->addItem("Dashed", static_cast<int>(Qt::DashLine));
  comboStyle->addItem("Dotted", static_cast<int>(Qt::DotLine));
  for (int i = 0; i < comboStyle->count(); ++i) {
    if (comboStyle->itemData(i).toInt() == static_cast<int>(src.penStyle)) {
      comboStyle->setCurrentIndex(i);
      break;
    }
  }
  connect(comboStyle, &QComboBox::currentIndexChanged, this, [this, comboStyle](int idx) {
    for (int rr = 0; rr < m_table->rowCount(); ++rr) {
      if (m_table->cellWidget(rr, COL_LINE_STYLE) == comboStyle) {
        m_sources[rr].penStyle = static_cast<Qt::PenStyle>(comboStyle->itemData(idx).toInt());
        refreshSourceSeriesColors(rr);
        return;
      }
    }
  });
  m_table->setCellWidget(row, COL_LINE_STYLE, comboStyle);

  // Color cell: two side-by-side buttons (start / end of gradient)
  QWidget *colorCell = new QWidget();
  QHBoxLayout *colorLayout = new QHBoxLayout(colorCell);
  colorLayout->setContentsMargins(2, 2, 2, 2);
  colorLayout->setSpacing(2);

  auto makeColorButton = [&](const QColor &c, const QString &tipPrefix) {
    QPushButton *b = new QPushButton();
    b->setMinimumWidth(28);
    b->setMaximumWidth(40);
    b->setStyleSheet(QString("background-color: %1; border: 1px solid #555;").arg(c.name()));
    b->setToolTip(QString("%1  RGB: %2, %3, %4")
                       .arg(tipPrefix).arg(c.red()).arg(c.green()).arg(c.blue()));
    return b;
  };

  QPushButton *btnColorStart = makeColorButton(src.color,    "First trajectory");
  QPushButton *btnColorEnd   = makeColorButton(src.colorEnd, "Last trajectory");
  colorLayout->addWidget(btnColorStart);
  colorLayout->addWidget(btnColorEnd);

  auto rowOfColorButton = [this](QWidget *btn) {
    for (int rr = 0; rr < m_table->rowCount(); ++rr) {
      QWidget *cell = m_table->cellWidget(rr, COL_COLOR);
      if (cell && cell->isAncestorOf(btn)) return rr;
    }
    return -1;
  };
  connect(btnColorStart, &QPushButton::clicked, this, [this, rowOfColorButton, btnColorStart]() {
    int r = rowOfColorButton(btnColorStart);
    if (r >= 0) onPickColor(r, false);
  });
  connect(btnColorEnd, &QPushButton::clicked, this, [this, rowOfColorButton, btnColorEnd]() {
    int r = rowOfColorButton(btnColorEnd);
    if (r >= 0) onPickColor(r, true);
  });

  m_table->setCellWidget(row, COL_COLOR, colorCell);

  // Status item
  QTableWidgetItem *statusItem = new QTableWidgetItem("–");
  statusItem->setForeground(QBrush(QColor("gray")));
  m_table->setItem(row, COL_STATUS, statusItem);
}

void RunFromFileWidget::onAddSource() {
  RunSource s;
  s.color    = DEFAULT_COLORS[m_sources.size() % NUM_DEFAULT_COLORS];
  s.colorEnd = s.color.lighter(170);
  // Cycle line styles too if many sources
  static const Qt::PenStyle styles[3] = {Qt::SolidLine, Qt::DashLine, Qt::DotLine};
  s.penStyle = styles[(m_sources.size() / NUM_DEFAULT_COLORS) % 3];
  m_sources.append(s);
  addSourceRow(s);
}

void RunFromFileWidget::onRemoveSource() {
  int row = m_table->currentRow();
  if (row < 0 || row >= m_table->rowCount()) return;
  // Detach and delete chart series for this source
  RunSource &src = m_sources[row];
  auto removeAll = [&](QChart *chart, QVector<QLineSeries*> &vec) {
    for (auto *s : vec) {
      if (s) {
        chart->removeSeries(s);
        delete s;
      }
    }
    vec.clear();
  };
  removeAll(m_densView->chart(), src.series_nB);
  removeAll(m_densView->chart(), src.series_s);
  removeAll(m_densView->chart(), src.series_nQ);
  removeAll(m_lepDensView->chart(), src.series_ne);
  removeAll(m_lepDensView->chart(), src.series_nmu);
  removeAll(m_lepDensView->chart(), src.series_ntau);
  removeAll(m_lepDensView->chart(), src.series_nnue);
  removeAll(m_lepDensView->chart(), src.series_nnumu);
  removeAll(m_lepDensView->chart(), src.series_nnutau);
  removeAll(m_muView->chart(),   src.series_muB);
  removeAll(m_muView->chart(),   src.series_muQ);
  removeAll(m_lepView->chart(),  src.series_munue);
  removeAll(m_lepView->chart(),  src.series_munumu);
  removeAll(m_lepView->chart(),  src.series_mnutau);

  m_sources.removeAt(row);
  m_table->removeRow(row);
  updateChartAxes();
}

void RunFromFileWidget::onBrowseFile(int row) {
  if (row < 0 || row >= m_sources.size()) return;
  QString file = QFileDialog::getOpenFileName(this, "Select Parameter File",
      m_workingDir + "/input", "Text Files (*.txt);;All Files (*)");
  if (file.isEmpty()) return;
  m_sources[row].filePath = file;
  QTableWidgetItem *item = m_table->item(row, COL_FILE);
  if (item) {
    item->setText(QFileInfo(file).fileName());
    item->setToolTip(file);
  }
}

void RunFromFileWidget::onBrowseEosTable(int row) {
  if (row < 0 || row >= m_sources.size()) return;
  QString file = QFileDialog::getOpenFileName(this, "Select EoS Table File",
      m_workingDir, "Text Files (*.txt);;All Files (*)");
  if (file.isEmpty()) return;
  QWidget *w = m_table->cellWidget(row, COL_EOS_TABLE);
  if (w) {
    QLineEdit *edit = w->findChild<QLineEdit*>();
    if (edit) edit->setText(file);
  }
}

void RunFromFileWidget::onTableEosChanged(int row, int newIndex) {
  if (row < 0 || row >= m_sources.size()) return;
  QWidget *w = m_table->cellWidget(row, COL_EOS_TABLE);
  if (w) w->setEnabled(newIndex == 2);
}

void RunFromFileWidget::onPickColor(int row, bool isEnd) {
  if (row < 0 || row >= m_sources.size()) return;
  RunSource &src = m_sources[row];
  QColor initial = isEnd ? src.colorEnd : src.color;
  QColor c = QColorDialog::getColor(initial, this,
                                    isEnd ? "Pick Last Trajectory Color"
                                          : "Pick First Trajectory Color",
                                    QColorDialog::ShowAlphaChannel);
  if (!c.isValid()) return;
  if (isEnd) src.colorEnd = c;
  else       src.color    = c;

  // Refresh the corresponding cell button
  QWidget *cell = m_table->cellWidget(row, COL_COLOR);
  if (cell && cell->layout()) {
    QLayoutItem *it = cell->layout()->itemAt(isEnd ? 1 : 0);
    if (it) {
      if (auto *btn = qobject_cast<QPushButton*>(it->widget())) {
        btn->setStyleSheet(QString("background-color: %1; border: 1px solid #555;").arg(c.name()));
        btn->setToolTip(QString("%1  RGB: %2, %3, %4")
                            .arg(isEnd ? "Last trajectory" : "First trajectory")
                            .arg(c.red()).arg(c.green()).arg(c.blue()));
      }
    }
  }
  refreshSourceSeriesColors(row);
}

// ── Color helpers ──────────────────────────────────────────────────────────
QColor RunFromFileWidget::interpolatedColor(const RunSource &src, int rowIdx, int total) const {
  if (total <= 1) return src.color;
  double t = static_cast<double>(rowIdx) / static_cast<double>(total - 1);
  if (t < 0.0) t = 0.0; if (t > 1.0) t = 1.0;
  auto lerp = [t](int a, int b) {
    return static_cast<int>(std::round(a + (b - a) * t));
  };
  int r = lerp(src.color.red(),   src.colorEnd.red());
  int g = lerp(src.color.green(), src.colorEnd.green());
  int b = lerp(src.color.blue(),  src.colorEnd.blue());
  int a = lerp(src.color.alpha(), src.colorEnd.alpha());
  return QColor(r, g, b, a);
}

void RunFromFileWidget::refreshSourceSeriesColors(int sourceIdx) {
  if (sourceIdx < 0 || sourceIdx >= m_sources.size()) return;
  RunSource &src = m_sources[sourceIdx];
  int total = src.rows.size();
  if (total == 0) return;  // no series built yet

  auto applyToVec = [&](QVector<QLineSeries*> &vec, Qt::PenStyle style) {
    for (int j = 0; j < vec.size(); ++j) {
      if (!vec[j]) continue;
      QPen pen(interpolatedColor(src, j, total));
      pen.setStyle(style);
      pen.setWidthF(2.0);
      vec[j]->setPen(pen);
    }
  };
  // Distinguish quantities by line style within the same source color
  applyToVec(src.series_nB,      Qt::SolidLine);
  applyToVec(src.series_s,       Qt::DashLine);
  applyToVec(src.series_nQ,      Qt::DotLine);
  
  applyToVec(src.series_ne,      Qt::SolidLine);
  applyToVec(src.series_nmu,     Qt::DashLine);
  applyToVec(src.series_ntau,    Qt::DotLine);
  applyToVec(src.series_nnue,    Qt::DashDotLine);
  applyToVec(src.series_nnumu,   Qt::DashDotDotLine);
  applyToVec(src.series_nnutau,  Qt::SolidLine);
  
  applyToVec(src.series_muB,     Qt::SolidLine);
  applyToVec(src.series_muQ,     Qt::DashLine); // As requested: muQ dashed
  
  applyToVec(src.series_munue,   Qt::SolidLine);
  applyToVec(src.series_munumu,  Qt::DashLine);
  applyToVec(src.series_mnutau,  Qt::DotLine);
}

// Read all settings from the current table widgets back into m_sources.
void RunFromFileWidget::readRowIntoSource(int row) {
  if (row < 0 || row >= m_sources.size()) return;
  RunSource &src = m_sources[row];

  QComboBox *comboEos = qobject_cast<QComboBox*>(m_table->cellWidget(row, COL_EOS));
  if (comboEos) src.eos = comboEos->currentIndex();

  QComboBox *comboNf = qobject_cast<QComboBox*>(m_table->cellWidget(row, COL_NF));
  if (comboNf) src.nf = comboNf->currentText().toInt();

  QWidget *tblPathWidget = m_table->cellWidget(row, COL_EOS_TABLE);
  if (tblPathWidget) {
    QLineEdit *edit = tblPathWidget->findChild<QLineEdit*>();
    if (edit) src.eosTablePath = edit->text();
  }

  QComboBox *comboStyle = qobject_cast<QComboBox*>(m_table->cellWidget(row, COL_LINE_STYLE));
  if (comboStyle) src.penStyle = static_cast<Qt::PenStyle>(comboStyle->currentData().toInt());
  // color and filePath already set via dedicated handlers
}

// ── Parameter file parser ──────────────────────────────────────────────────
bool RunFromFileWidget::parseParamFile(const QString &path,
                                       QVector<TrajParamRow> &out, QString &err) {
  out.clear();
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    err = QString("Cannot open file: %1").arg(path);
    return false;
  }
  QTextStream in(&f);
  int lineNo = 0;
  while (!in.atEnd()) {
    QString line = in.readLine().trimmed();
    lineNo++;
    if (line.isEmpty()) continue;
    if (line.startsWith('#')) continue;
    // Allow comma or whitespace separators
    QStringList toks = line.split(QRegularExpression("[\\s,]+"), Qt::SkipEmptyParts);
    if (toks.size() < 4) {
      err = QString("Line %1: expected 4 columns (b le lmu ltau), got %2: %3")
                .arg(lineNo).arg(toks.size()).arg(line);
      return false;
    }
    bool ok[4] = {false, false, false, false};
    TrajParamRow r;
    r.b    = toks[0].toDouble(&ok[0]);
    r.le   = toks[1].toDouble(&ok[1]);
    r.lmu  = toks[2].toDouble(&ok[2]);
    r.ltau = toks[3].toDouble(&ok[3]);
    if (!ok[0] || !ok[1] || !ok[2] || !ok[3]) {
      err = QString("Line %1: failed to parse numeric values: %2").arg(lineNo).arg(line);
      return false;
    }
    out.append(r);
  }
  return true;
}

// ── Run flow ───────────────────────────────────────────────────────────────
void RunFromFileWidget::onRunAll() {
  if (m_running) return;

  // Pull table widgets into m_sources
  for (int i = 0; i < m_sources.size(); ++i) readRowIntoSource(i);

  // Build run queue and parse files
  m_runQueue.clear();
  for (int i = 0; i < m_sources.size(); ++i) {
    RunSource &src = m_sources[i];
    if (src.filePath.isEmpty()) {
      logMessage(QString("<font color='#ffc107'>Source %1: no file selected, skipping.</font>").arg(i + 1));
      setSourceStatus(i, "Skipped (no file)", "#ffc107");
      continue;
    }
    QString err;
    QVector<TrajParamRow> rows;
    if (!parseParamFile(src.filePath, rows, err)) {
      logMessage(QString("<font color='#dc3545'>Source %1: %2</font>").arg(i + 1).arg(err));
      setSourceStatus(i, "Parse error", "#dc3545");
      continue;
    }
    if (rows.isEmpty()) {
      logMessage(QString("<font color='#ffc107'>Source %1: file %2 contains no parameter rows.</font>")
                     .arg(i + 1).arg(src.filePath));
      setSourceStatus(i, "Empty", "#ffc107");
      continue;
    }
    src.rows = rows;
    src.data.clear();
    src.data.resize(rows.size());
    // Free any old series (re-run case)
    auto wipe = [&](QChart *chart, QVector<QLineSeries*> &vec) {
      for (auto *s : vec) {
        if (s) { chart->removeSeries(s); delete s; }
      }
      vec.clear();
    };
    wipe(m_densView->chart(), src.series_nB);
    wipe(m_densView->chart(), src.series_s);
    wipe(m_densView->chart(), src.series_nQ);
    wipe(m_lepDensView->chart(), src.series_ne);
    wipe(m_lepDensView->chart(), src.series_nmu);
    wipe(m_lepDensView->chart(), src.series_ntau);
    wipe(m_lepDensView->chart(), src.series_nnue);
    wipe(m_lepDensView->chart(), src.series_nnumu);
    wipe(m_lepDensView->chart(), src.series_nnutau);
    wipe(m_muView->chart(),   src.series_muB);
    wipe(m_muView->chart(),   src.series_muQ);
    wipe(m_lepView->chart(),  src.series_munue);
    wipe(m_lepView->chart(),  src.series_munumu);
    wipe(m_lepView->chart(),  src.series_mnutau);

    src.series_nB.resize(rows.size());
    src.series_s.resize(rows.size());
    src.series_nQ.resize(rows.size());
    src.series_ne.resize(rows.size());
    src.series_nmu.resize(rows.size());
    src.series_ntau.resize(rows.size());
    src.series_nnue.resize(rows.size());
    src.series_nnumu.resize(rows.size());
    src.series_nnutau.resize(rows.size());
    src.series_muB.resize(rows.size());
    src.series_muQ.resize(rows.size());
    src.series_munue.resize(rows.size());
    src.series_munumu.resize(rows.size());
    src.series_mnutau.resize(rows.size());

    // Pre-create the QLineSeries for each row so we can plot live.
    auto buildSeries = [&](QChart *chart, QAbstractAxis *ax, QAbstractAxis *ay,
                           QVector<QLineSeries*> &vec, const QString &qtyLabel,
                           bool showInLegend) {
      for (int j = 0; j < rows.size(); ++j) {
        QLineSeries *ls = new QLineSeries();
        QPen pen(interpolatedColor(src, j, rows.size()));
        pen.setStyle(src.penStyle);
        pen.setWidthF(2.0);
        ls->setPen(pen);
        QString name = QString("S%1·R%2 %3").arg(i + 1).arg(j + 1).arg(qtyLabel);
        ls->setName(name);
        chart->addSeries(ls);
        ls->attachAxis(ax);
        ls->attachAxis(ay);
        // Hide all but the first row's marker per quantity to keep legend tidy.
        if (!showInLegend || j > 0) {
          const auto markers = chart->legend()->markers(ls);
          for (auto *m : markers) m->setVisible(false);
        } else {
          ls->setName(QString("S%1 %2").arg(i + 1).arg(qtyLabel));
        }
        vec[j] = ls;
      }
    };
    // showInLegend=true makes the row-0 marker visible in the legend; only
    // higher rows have their markers hidden to avoid clutter.
    buildSeries(m_densView->chart(), m_densAxisX, m_densAxisY, src.series_nB, "nB", true);
    buildSeries(m_densView->chart(), m_densAxisX, m_densAxisY, src.series_s,  "s",  true);
    buildSeries(m_densView->chart(), m_densAxisX, m_densAxisY, src.series_nQ, "nQ", true);

    buildSeries(m_lepDensView->chart(), m_lepDensAxisX, m_lepDensAxisY, src.series_ne, "ne", true);
    buildSeries(m_lepDensView->chart(), m_lepDensAxisX, m_lepDensAxisY, src.series_nmu, "nμ", true);
    buildSeries(m_lepDensView->chart(), m_lepDensAxisX, m_lepDensAxisY, src.series_ntau, "nτ", true);
    buildSeries(m_lepDensView->chart(), m_lepDensAxisX, m_lepDensAxisY, src.series_nnue, "nνe", true);
    buildSeries(m_lepDensView->chart(), m_lepDensAxisX, m_lepDensAxisY, src.series_nnumu, "nνμ", true);
    buildSeries(m_lepDensView->chart(), m_lepDensAxisX, m_lepDensAxisY, src.series_nnutau, "nντ", true);

    buildSeries(m_muView->chart(),   m_muAxisX,   m_muAxisY,   src.series_muB, "μB", true);
    buildSeries(m_muView->chart(),   m_muAxisX,   m_muAxisY,   src.series_muQ, "μQ", true);
    buildSeries(m_lepView->chart(),  m_lepAxisX,  m_lepAxisY,  src.series_munue,  "μνe", true);
    buildSeries(m_lepView->chart(),  m_lepAxisX,  m_lepAxisY,  src.series_munumu, "μνμ", true);
    buildSeries(m_lepView->chart(),  m_lepAxisX,  m_lepAxisY,  src.series_mnutau, "μντ", true);

    for (int j = 0; j < rows.size(); ++j) {
      QueueEntry q{i, j};
      m_runQueue.append(q);
    }
    setSourceStatus(i, QString("Queued: %1 trajectories").arg(rows.size()), "#17a2b8");
  }

  // Apply current show/hide state and abs/legend names to fresh series.
  updateSeriesVisibility();
  refreshSeriesNames();
  refreshLegendVisibility();

  if (m_runQueue.isEmpty()) {
    logMessage("<font color='#ffc107'>Nothing to run.</font>");
    return;
  }

  m_running        = true;
  m_stopRequested  = false;
  m_queueIndex     = 0;
  m_btnRun->setEnabled(false);
  m_btnStop->setEnabled(true);
  m_btnAdd->setEnabled(false);
  m_btnRemove->setEnabled(false);
  m_btnClear->setEnabled(false);
  m_progressBar->setValue(0);
  m_statusLabel->setText("Running…");
  m_statusLabel->setStyleSheet("color: #ffc107; font-weight: bold;");
  logMessage(QString("─── Starting batch: %1 trajectories total ───").arg(m_runQueue.size()));
  startNextTrajectory();
}

void RunFromFileWidget::startNextTrajectory() {
  if (m_stopRequested || m_queueIndex >= m_runQueue.size()) {
    m_running = false;
    m_btnRun->setEnabled(true);
    m_btnStop->setEnabled(false);
    m_btnAdd->setEnabled(true);
    m_btnRemove->setEnabled(true);
    m_btnClear->setEnabled(true);
    m_progressBar->setValue(100);
    if (m_stopRequested) {
      m_statusLabel->setText("Stopped");
      m_statusLabel->setStyleSheet("color: #dc3545; font-weight: bold;");
      logMessage("🛑 Batch stopped by user.");
    } else {
      m_statusLabel->setText("Complete");
      m_statusLabel->setStyleSheet("color: #28a745; font-weight: bold;");
      logMessage("✓ Batch complete.");
    }
    return;
  }

  const QueueEntry &q = m_runQueue[m_queueIndex];
  m_currentSourceIdx = q.sourceIdx;
  m_currentRowIdx    = q.rowIdx;

  RunSource &src = m_sources[q.sourceIdx];
  const TrajParamRow &p = src.rows[q.rowIdx];

  setSourceStatus(q.sourceIdx,
      QString("Running %1/%2  (b=%3, le=%4, lmu=%5, ltau=%6)")
          .arg(q.rowIdx + 1).arg(src.rows.size())
          .arg(p.b).arg(p.le).arg(p.lmu).arg(p.ltau),
      "#ffc107");

  logMessage(QString("[Source %1, row %2/%3]  b=%4 le=%5 lmu=%6 ltau=%7   EoS=%8 nf=%9")
                 .arg(q.sourceIdx + 1)
                 .arg(q.rowIdx + 1).arg(src.rows.size())
                 .arg(p.b).arg(p.le).arg(p.lmu).arg(p.ltau)
                 .arg(src.eos).arg(src.nf));

  // Spin up worker thread
  teardownWorker();
  m_workerThread = new QThread();
  m_worker = new SimulationWorker();

  m_worker->b    = p.b;
  m_worker->le   = p.le;
  m_worker->lmu  = p.lmu;
  m_worker->ltau = p.ltau;
  m_worker->dT   = m_spinDT->value();
  m_worker->Tmin = m_spinTmin->value();
  m_worker->Tmax = m_spinTmax->value();

  m_worker->nf  = src.nf;
  m_worker->eos = src.eos;
  if (m_worker->eos == 1 && m_worker->nf == 2) {
    logMessage("<font color='#ffc107'><b>Warning:</b> Lattice QCD has no nf=2 EoS; defaulting to nf=3.</font>");
    m_worker->nf = 3;
  }
  m_worker->scanDirection = m_comboScan->currentIndex();
  m_worker->guessMethod   = m_comboGuess->currentIndex();
  m_worker->eosTableFilePath = src.eosTablePath;

  m_worker->tolerance = m_tolerance;
  m_worker->maxIter   = m_maxIter;
  m_worker->initialGuessType   = m_initialGuessType;
  m_worker->customGuessLowHigh = m_customGuessLowHigh;
  m_worker->customGuessHighLow = m_customGuessHighLow;
  m_worker->metropolisMode      = m_metropolisMode;
  m_worker->metropolisSteps     = m_metropolisSteps;
  m_worker->metropolisStepSigma = m_metropolisStepSigma;
  m_worker->metropolisT         = m_metropolisT;
  m_worker->metropolisRetries   = m_metropolisRetries;
  m_worker->latticeInterpType   = m_latticeInterpType;

  m_worker->workingDir = m_workingDir;

  m_worker->moveToThread(m_workerThread);
  connect(m_workerThread, &QThread::started, m_worker, &SimulationWorker::run);
  connect(m_worker, &SimulationWorker::stepCompleted, this, &RunFromFileWidget::onWorkerStepCompleted);
  connect(m_worker, &SimulationWorker::logMessage,    this, &RunFromFileWidget::onWorkerLog);
  connect(m_worker, &SimulationWorker::simulationFinished, this, &RunFromFileWidget::onWorkerFinished);
  connect(m_worker, &SimulationWorker::simulationError,    this, &RunFromFileWidget::onWorkerError);

  m_workerThread->start();
}

void RunFromFileWidget::onWorkerStepCompleted(TrajectoryPoint pt) {
  if (m_currentSourceIdx < 0 || m_currentRowIdx < 0) return;
  if (m_currentSourceIdx >= m_sources.size()) return;
  RunSource &src = m_sources[m_currentSourceIdx];
  if (m_currentRowIdx >= src.data.size()) return;

  src.data[m_currentRowIdx].append(pt);

  auto append = [&](QLineSeries *s, double xVal, double yVal) {
    if (!s) return;
    if (m_tempIsVertical) s->append(yVal, xVal);  // X = quantity, Y = T
    else                  s->append(xVal, yVal);  // X = T,        Y = quantity
  };

  append(src.series_nB[m_currentRowIdx], pt.T, absVal(pt.nB, m_useAbsnB));
  append(src.series_s[m_currentRowIdx],  pt.T, absVal(pt.s, m_useAbsS));
  append(src.series_nQ[m_currentRowIdx], pt.T, absVal(pt.nQ, m_useAbsnQ));

  append(src.series_ne[m_currentRowIdx],     pt.T, absVal(pt.ne,     m_useAbsNe));
  append(src.series_nmu[m_currentRowIdx],    pt.T, absVal(pt.nmu,    m_useAbsNmu));
  append(src.series_ntau[m_currentRowIdx],   pt.T, absVal(pt.ntau,   m_useAbsNtau));
  append(src.series_nnue[m_currentRowIdx],   pt.T, absVal(pt.nnue,   m_useAbsNnue));
  append(src.series_nnumu[m_currentRowIdx],  pt.T, absVal(pt.nnumu,  m_useAbsNnumu));
  append(src.series_nnutau[m_currentRowIdx], pt.T, absVal(pt.nnutau, m_useAbsNnutau));

  append(src.series_muB[m_currentRowIdx],    pt.T, absVal(pt.muB,    m_useAbsMuB));
  append(src.series_muQ[m_currentRowIdx],    pt.T, absVal(pt.muQ,    m_useAbsMuQ));
  append(src.series_munue[m_currentRowIdx],  pt.T, absVal(pt.munue,  m_useAbsMunue));
  append(src.series_munumu[m_currentRowIdx], pt.T, absVal(pt.munumu, m_useAbsMunumu));
  append(src.series_mnutau[m_currentRowIdx], pt.T, absVal(pt.mnutau, m_useAbsMnutau));

  updateChartAxes();
}

void RunFromFileWidget::onWorkerFinished() {
  RunSource &src = m_sources[m_currentSourceIdx];
  setSourceStatus(m_currentSourceIdx,
      QString("Done %1/%2").arg(m_currentRowIdx + 1).arg(src.rows.size()),
      m_currentRowIdx + 1 == src.rows.size() ? "#28a745" : "#17a2b8");

  m_queueIndex++;
  int pct = static_cast<int>(100.0 * m_queueIndex / std::max<qsizetype>(1, m_runQueue.size()));
  m_progressBar->setValue(pct);
  startNextTrajectory();
}

void RunFromFileWidget::onWorkerError(const QString &msg) {
  logMessage(QString("<font color='#dc3545'>[Source %1, row %2] %3</font>")
                 .arg(m_currentSourceIdx + 1).arg(m_currentRowIdx + 1).arg(msg));
  setSourceStatus(m_currentSourceIdx, "Error", "#dc3545");
  m_queueIndex++;
  int pct = static_cast<int>(100.0 * m_queueIndex / std::max<qsizetype>(1, m_runQueue.size()));
  m_progressBar->setValue(pct);
  startNextTrajectory();
}

void RunFromFileWidget::onWorkerLog(const QString &msg) {
  logMessage(msg, m_currentSourceIdx, m_currentRowIdx);
}

void RunFromFileWidget::onStopAll() {
  m_stopRequested = true;
  if (m_worker) m_worker->stop();
  m_btnStop->setEnabled(false);
  m_statusLabel->setText("Stopping…");
}

void RunFromFileWidget::onClearAll() {
  if (m_running) return;
  for (auto &src : m_sources) {
    auto wipe = [&](QChart *chart, QVector<QLineSeries*> &vec) {
      for (auto *s : vec) { if (s) { chart->removeSeries(s); delete s; } }
      vec.clear();
    };
    wipe(m_densView->chart(), src.series_nB);
    wipe(m_densView->chart(), src.series_s);
    wipe(m_densView->chart(), src.series_nQ);
    wipe(m_lepDensView->chart(), src.series_ne);
    wipe(m_lepDensView->chart(), src.series_nmu);
    wipe(m_lepDensView->chart(), src.series_ntau);
    wipe(m_lepDensView->chart(), src.series_nnue);
    wipe(m_lepDensView->chart(), src.series_nnumu);
    wipe(m_lepDensView->chart(), src.series_nnutau);
    wipe(m_muView->chart(),   src.series_muB);
    wipe(m_muView->chart(),   src.series_muQ);
    wipe(m_lepView->chart(),  src.series_munue);
    wipe(m_lepView->chart(),  src.series_munumu);
    wipe(m_lepView->chart(),  src.series_mnutau);
    src.data.clear();
  }
  m_console->clear();
  for (int i = 0; i < m_sources.size(); ++i) setSourceStatus(i, "–", "gray");
  m_progressBar->setValue(0);
  m_statusLabel->setText("Ready");
  m_statusLabel->setStyleSheet("color: #17a2b8; font-weight: bold;");
  updateChartAxes();
}

// ── Plot helpers ───────────────────────────────────────────────────────────
void RunFromFileWidget::replotAll() {
  // Re-fill all series from m_sources (used after axis/scale/abs toggle).
  auto fill = [&](QLineSeries *s, const QVector<TrajectoryPoint> &pts,
                  double TrajectoryPoint::*qty, bool useAbs) {
    if (!s) return;
    s->clear();
    for (const auto &pt : pts) {
      double v = absVal(pt.*qty, useAbs);
      double x, y;
      if (m_tempIsVertical) { x = v;    y = pt.T; }
      else                  { x = pt.T; y = v; }
      s->append(x, y);
    }
  };

  for (auto &src : m_sources) {
    for (int j = 0; j < src.data.size(); ++j) {
      const auto &pts = src.data[j];
      if (j < src.series_nB.size())     fill(src.series_nB[j],     pts, &TrajectoryPoint::nB,     m_useAbsnB);
      if (j < src.series_s.size())      fill(src.series_s[j],      pts, &TrajectoryPoint::s,      m_useAbsS);
      if (j < src.series_nQ.size())     fill(src.series_nQ[j],     pts, &TrajectoryPoint::nQ,     m_useAbsnQ);
      if (j < src.series_ne.size())     fill(src.series_ne[j],     pts, &TrajectoryPoint::ne,     m_useAbsNe);
      if (j < src.series_nmu.size())    fill(src.series_nmu[j],    pts, &TrajectoryPoint::nmu,    m_useAbsNmu);
      if (j < src.series_ntau.size())   fill(src.series_ntau[j],   pts, &TrajectoryPoint::ntau,   m_useAbsNtau);
      if (j < src.series_nnue.size())   fill(src.series_nnue[j],   pts, &TrajectoryPoint::nnue,   m_useAbsNnue);
      if (j < src.series_nnumu.size())  fill(src.series_nnumu[j],  pts, &TrajectoryPoint::nnumu,  m_useAbsNnumu);
      if (j < src.series_nnutau.size()) fill(src.series_nnutau[j], pts, &TrajectoryPoint::nnutau, m_useAbsNnutau);
      if (j < src.series_muB.size())    fill(src.series_muB[j],    pts, &TrajectoryPoint::muB,    m_useAbsMuB);
      if (j < src.series_muQ.size())    fill(src.series_muQ[j],    pts, &TrajectoryPoint::muQ,    m_useAbsMuQ);
      if (j < src.series_munue.size())  fill(src.series_munue[j],  pts, &TrajectoryPoint::munue,  m_useAbsMunue);
      if (j < src.series_munumu.size()) fill(src.series_munumu[j], pts, &TrajectoryPoint::munumu, m_useAbsMunumu);
      if (j < src.series_mnutau.size()) fill(src.series_mnutau[j], pts, &TrajectoryPoint::mnutau, m_useAbsMnutau);
    }
  }
  refreshSeriesNames();
  refreshLegendVisibility();
  updateChartAxes();
}

void RunFromFileWidget::updateChartAxes() {
  double minT = 1e99, maxT = -1e99;
  double minDens = 1e99, maxDens = -1e99;
  double minMu   = 1e99, maxMu   = -1e99;
  double minLep  = 1e99, maxLep  = -1e99;
  bool hasData = false;

  for (const auto &src : m_sources) {
    for (const auto &pts : src.data) {
      for (const auto &p : pts) {
        hasData = true;
        minT = std::min(minT, p.T);    maxT = std::max(maxT, p.T);
        minDens = std::min({minDens, absVal(p.nB, m_useAbsnB), absVal(p.s, m_useAbsS), absVal(p.nQ, m_useAbsnQ)});
        maxDens = std::max({maxDens, absVal(p.nB, m_useAbsnB), absVal(p.s, m_useAbsS), absVal(p.nQ, m_useAbsnQ)});
        minMu = std::min({minMu, absVal(p.muB, m_useAbsMuB), absVal(p.muQ, m_useAbsMuQ)});
        maxMu = std::max({maxMu, absVal(p.muB, m_useAbsMuB), absVal(p.muQ, m_useAbsMuQ)});
        minLep = std::min({minLep, absVal(p.munue, m_useAbsMunue), absVal(p.munumu, m_useAbsMunumu), absVal(p.mnutau, m_useAbsMnutau)});
        maxLep = std::max({maxLep, absVal(p.munue, m_useAbsMunue), absVal(p.munumu, m_useAbsMunumu), absVal(p.mnutau, m_useAbsMnutau)});
      }
    }
  }
  if (!hasData) return;
  if (minT >= maxT) maxT = minT + 1.0;

  auto setRange = [this](QAbstractAxis *axis, double lo, double hi, bool isQuantity) {
    if (m_isLogScale) {
      axis->setRange(std::max(lo * 0.5, 1e-30), hi * 2.0);
    } else if (isQuantity) {
      axis->setRange(std::min(0.0, lo * 1.1), hi * 1.1);
    } else {
      axis->setRange(lo * 0.9, hi * 1.1);
    }
  };

  // Apply ranges, respecting manual overrides per axis.
  auto applyAxis = [&](QAbstractAxis *axis, const AxisLimit &lim,
                       double autoLo, double autoHi, bool isQuantity) {
    if (!lim.autoRange) {
      axis->setRange(lim.lo, lim.hi);
    } else {
      setRange(axis, autoLo, autoHi, isQuantity);
    }
  };

  if (m_tempIsVertical) {
    applyAxis(m_densAxisY, m_densY, minT, maxT, false);
    applyAxis(m_muAxisY,   m_muY,   minT, maxT, false);
    applyAxis(m_lepAxisY,  m_lepY,  minT, maxT, false);
    applyAxis(m_densAxisX, m_densX, minDens, maxDens, true);
    applyAxis(m_muAxisX,   m_muX,   minMu,   maxMu,   true);
    applyAxis(m_lepAxisX,  m_lepX,  minLep,  maxLep,  true);
  } else {
    applyAxis(m_densAxisX, m_densX, minT, maxT, false);
    applyAxis(m_muAxisX,   m_muX,   minT, maxT, false);
    applyAxis(m_lepAxisX,  m_lepX,  minT, maxT, false);
    applyAxis(m_densAxisY, m_densY, minDens, maxDens, true);
    applyAxis(m_muAxisY,   m_muY,   minMu,   maxMu,   true);
    applyAxis(m_lepAxisY,  m_lepY,  minLep,  maxLep,  true);
  }
}

// ── Series visibility ──────────────────────────────────────────────────────
void RunFromFileWidget::updateSeriesVisibility() {
  auto setVec = [](QVector<QLineSeries*> &vec, bool on) {
    for (auto *s : vec) if (s) s->setVisible(on);
  };
  for (auto &src : m_sources) {
    setVec(src.series_nB,     m_chknB     && m_chknB->isChecked());
    setVec(src.series_s,      m_chkS      && m_chkS->isChecked());
    setVec(src.series_nQ,     m_chknQ     && m_chknQ->isChecked());
    setVec(src.series_muB,    m_chkMuB    && m_chkMuB->isChecked());
    setVec(src.series_muQ,    m_chkMuQ    && m_chkMuQ->isChecked());
    setVec(src.series_ne,     m_chkNe     && m_chkNe->isChecked());
    setVec(src.series_nmu,    m_chkNmu    && m_chkNmu->isChecked());
    setVec(src.series_ntau,   m_chkNtau   && m_chkNtau->isChecked());
    setVec(src.series_nnue,   m_chkNnue   && m_chkNnue->isChecked());
    setVec(src.series_nnumu,  m_chkNnumu  && m_chkNnumu->isChecked());
    setVec(src.series_nnutau, m_chkNnutau && m_chkNnutau->isChecked());
  }
}

// ── Axis-limits dialog ─────────────────────────────────────────────────────
void RunFromFileWidget::onAxisLimitsClicked() {
  QDialog dlg(this);
  dlg.setWindowTitle("Axis Limits");
  dlg.setMinimumWidth(440);
  QVBoxLayout *vbox = new QVBoxLayout(&dlg);

  struct Row {
    QCheckBox *chkAuto;
    QDoubleSpinBox *spinLo;
    QDoubleSpinBox *spinHi;
    AxisLimit *target;
  };
  QVector<Row> rows;

  auto addAxisRow = [&](QGridLayout *grid, int r, const QString &label,
                        AxisLimit *target) {
    grid->addWidget(new QLabel(label), r, 0);
    QCheckBox *chk = new QCheckBox("Auto", &dlg);
    chk->setChecked(target->autoRange);
    grid->addWidget(chk, r, 1);
    QDoubleSpinBox *lo = new QDoubleSpinBox(&dlg);
    lo->setRange(-1e30, 1e30); lo->setDecimals(6); lo->setValue(target->lo);
    grid->addWidget(new QLabel("Min:"), r, 2);
    grid->addWidget(lo, r, 3);
    QDoubleSpinBox *hi = new QDoubleSpinBox(&dlg);
    hi->setRange(-1e30, 1e30); hi->setDecimals(6); hi->setValue(target->hi);
    grid->addWidget(new QLabel("Max:"), r, 4);
    grid->addWidget(hi, r, 5);
    auto enable = [lo, hi, chk]{
      bool en = !chk->isChecked();
      lo->setEnabled(en); hi->setEnabled(en);
    };
    QObject::connect(chk, &QCheckBox::toggled, [enable]{ enable(); });
    enable();
    rows.append({chk, lo, hi, target});
  };

  auto addGroup = [&](const QString &title, AxisLimit *xLim, AxisLimit *yLim) {
    QGroupBox *g = new QGroupBox(title, &dlg);
    QGridLayout *grid = new QGridLayout(g);
    addAxisRow(grid, 0, "X-axis:", xLim);
    addAxisRow(grid, 1, "Y-axis:", yLim);
    vbox->addWidget(g);
  };
  addGroup("Densities chart",  &m_densX, &m_densY);
  addGroup("Lepton Densities", &m_lepDensX, &m_lepDensY);
  addGroup("Chem. Pot. chart", &m_muX,   &m_muY);
  addGroup("Lepton μ chart",   &m_lepX,  &m_lepY);

  // Helper: pre-fill the lo/hi from current data range so the user has a
  // sensible starting point when they uncheck Auto.
  auto seedFromData = [this, &rows]() {
    double minT = 1e99, maxT = -1e99;
    double minDens = 1e99, maxDens = -1e99;
    double minLepDens = 1e99, maxLepDens = -1e99;
    double minMu   = 1e99, maxMu   = -1e99;
    double minLep  = 1e99, maxLep  = -1e99;
    bool any = false;
    auto val = [this](double v) {
      if (m_isLogScale) return std::max(std::abs(v), 1e-15);
      return v;
    };
    (void)val; // silenced — we use absVal() below for per-quantity behavior
    for (const auto &src : m_sources) for (const auto &pts : src.data) for (const auto &p : pts) {
      any = true;
      minT = std::min(minT, p.T);   maxT = std::max(maxT, p.T);
      minDens = std::min({minDens, absVal(p.nB, m_useAbsnB), absVal(p.s, m_useAbsS), absVal(p.nQ, m_useAbsnQ)});
      maxDens = std::max({maxDens, absVal(p.nB, m_useAbsnB), absVal(p.s, m_useAbsS), absVal(p.nQ, m_useAbsnQ)});
      minLepDens = std::min({minLepDens, absVal(p.ne, m_useAbsNe), absVal(p.nmu, m_useAbsNmu), absVal(p.ntau, m_useAbsNtau),
                                          absVal(p.nnue, m_useAbsNnue), absVal(p.nnumu, m_useAbsNnumu), absVal(p.nnutau, m_useAbsNnutau)});
      maxLepDens = std::max({maxLepDens, absVal(p.ne, m_useAbsNe), absVal(p.nmu, m_useAbsNmu), absVal(p.ntau, m_useAbsNtau),
                                          absVal(p.nnue, m_useAbsNnue), absVal(p.nnumu, m_useAbsNnumu), absVal(p.nnutau, m_useAbsNnutau)});
      minMu = std::min({minMu, absVal(p.muB, m_useAbsMuB), absVal(p.muQ, m_useAbsMuQ)});
      maxMu = std::max({maxMu, absVal(p.muB, m_useAbsMuB), absVal(p.muQ, m_useAbsMuQ)});
      minLep = std::min({minLep, absVal(p.munue, m_useAbsMunue), absVal(p.munumu, m_useAbsMunumu), absVal(p.mnutau, m_useAbsMnutau)});
      maxLep = std::max({maxLep, absVal(p.munue, m_useAbsMunue), absVal(p.munumu, m_useAbsMunumu), absVal(p.mnutau, m_useAbsMnutau)});
    }
    if (!any) return;
    auto seed = [&](Row &r, double lo, double hi) {
      if (r.target->autoRange) { r.spinLo->setValue(lo); r.spinHi->setValue(hi); }
    };
    if (m_tempIsVertical) {
      seed(rows[0], minDens, maxDens); seed(rows[1], minT, maxT);   // dens X=qty, Y=T
      seed(rows[2], minLepDens, maxLepDens); seed(rows[3], minT, maxT); // lep dens
      seed(rows[4], minMu,   maxMu);   seed(rows[5], minT, maxT);   // mu
      seed(rows[6], minLep,  maxLep);  seed(rows[7], minT, maxT);   // lep
    } else {
      seed(rows[0], minT, maxT);    seed(rows[1], minDens, maxDens);
      seed(rows[2], minT, maxT);    seed(rows[3], minLepDens, maxLepDens);
      seed(rows[4], minT, maxT);    seed(rows[5], minMu,   maxMu);
      seed(rows[6], minT, maxT);    seed(rows[7], minLep,  maxLep);
    }
  };
  seedFromData();

  QHBoxLayout *btnRow = new QHBoxLayout();
  QPushButton *btnReset = new QPushButton("Reset All to Auto", &dlg);
  QPushButton *btnApply = new QPushButton("Apply", &dlg);
  btnApply->setStyleSheet("QPushButton { background-color: #007bff; color: white; "
                          "border-radius: 4px; font-weight: bold; padding: 6px; }");
  QPushButton *btnCancel = new QPushButton("Cancel", &dlg);
  btnRow->addWidget(btnReset);
  btnRow->addStretch();
  btnRow->addWidget(btnCancel);
  btnRow->addWidget(btnApply);
  vbox->addLayout(btnRow);

  QObject::connect(btnReset, &QPushButton::clicked, [&rows]{
    for (auto &r : rows) r.chkAuto->setChecked(true);
  });
  QObject::connect(btnCancel, &QPushButton::clicked, &dlg, &QDialog::reject);
  QObject::connect(btnApply,  &QPushButton::clicked, &dlg, &QDialog::accept);

  if (dlg.exec() != QDialog::Accepted) return;

  for (auto &r : rows) {
    r.target->autoRange = r.chkAuto->isChecked();
    r.target->lo = r.spinLo->value();
    r.target->hi = r.spinHi->value();
    if (!r.target->autoRange && r.target->hi <= r.target->lo) {
      // Ignore invalid, fall back to auto for safety
      r.target->autoRange = true;
    }
  }
  updateChartAxes();
}

void RunFromFileWidget::onScaleToggle() {
  m_isLogScale = !m_isLogScale;

  auto swapAxes = [this](TooltipChartView *view, QAbstractAxis* &axisX, QAbstractAxis* &axisY,
                          const QString &valLabel) {
    QChart *chart = view->chart();
    chart->removeAxis(axisX);
    chart->removeAxis(axisY);
    delete axisX;
    delete axisY;
    if (m_isLogScale) {
      axisX = new QLogValueAxis();
      static_cast<QLogValueAxis*>(axisX)->setBase(10.0);
      static_cast<QLogValueAxis*>(axisX)->setLabelFormat("%g");
      axisY = new QLogValueAxis();
      static_cast<QLogValueAxis*>(axisY)->setBase(10.0);
      static_cast<QLogValueAxis*>(axisY)->setLabelFormat("%g");
    } else {
      axisX = new QValueAxis();
      static_cast<QValueAxis*>(axisX)->setLabelFormat("%g");
      axisY = new QValueAxis();
      static_cast<QValueAxis*>(axisY)->setLabelFormat("%g");
    }
    axisX->setTitleText(m_tempIsVertical ? valLabel : "Temperature [MeV]");
    chart->addAxis(axisX, Qt::AlignBottom);
    axisY->setTitleText(m_tempIsVertical ? "Temperature [MeV]" : valLabel);
    chart->addAxis(axisY, Qt::AlignLeft);
  };
  swapAxes(m_densView, m_densAxisX, m_densAxisY, "Densities [MeV³]");
  swapAxes(m_lepDensView, m_lepDensAxisX, m_lepDensAxisY, "Densities [MeV³]");
  swapAxes(m_muView,   m_muAxisX,   m_muAxisY,   "Chem. Pot. [MeV]");
  swapAxes(m_lepView,  m_lepAxisX,  m_lepAxisY,  "Chem. Pot. [MeV]");

  // Reattach all existing series
  for (auto &src : m_sources) {
    auto reattach = [](QVector<QLineSeries*> &vec, QAbstractAxis *ax, QAbstractAxis *ay) {
      for (auto *s : vec) {
        if (!s) continue;
        s->attachAxis(ax);
        s->attachAxis(ay);
      }
    };
    reattach(src.series_nB,     m_densAxisX, m_densAxisY);
    reattach(src.series_s,      m_densAxisX, m_densAxisY);
    reattach(src.series_nQ,     m_densAxisX, m_densAxisY);
    reattach(src.series_ne,     m_lepDensAxisX, m_lepDensAxisY);
    reattach(src.series_nmu,    m_lepDensAxisX, m_lepDensAxisY);
    reattach(src.series_ntau,   m_lepDensAxisX, m_lepDensAxisY);
    reattach(src.series_nnue,   m_lepDensAxisX, m_lepDensAxisY);
    reattach(src.series_nnumu,  m_lepDensAxisX, m_lepDensAxisY);
    reattach(src.series_nnutau, m_lepDensAxisX, m_lepDensAxisY);
    reattach(src.series_muB,    m_muAxisX,   m_muAxisY);
    reattach(src.series_muQ,    m_muAxisX,   m_muAxisY);
    reattach(src.series_munue,  m_lepAxisX,  m_lepAxisY);
    reattach(src.series_munumu, m_lepAxisX,  m_lepAxisY);
    reattach(src.series_mnutau, m_lepAxisX,  m_lepAxisY);
  }

  // Sync abs-checkbox enabled state with the new scale
  auto syncAbs = [this](QCheckBox *c){ if (c) c->setEnabled(!m_isLogScale); };
  syncAbs(m_absnB); syncAbs(m_absS); syncAbs(m_absnQ);
  syncAbs(m_absNe); syncAbs(m_absNmu); syncAbs(m_absNtau);
  syncAbs(m_absNnue); syncAbs(m_absNnumu); syncAbs(m_absNnutau);
  syncAbs(m_absMuB); syncAbs(m_absMuQ);
  syncAbs(m_absMunue); syncAbs(m_absMunumu); syncAbs(m_absMnutau);

  applyAxisFonts();
  replotAll();
}

void RunFromFileWidget::onAxisToggle() {
  m_tempIsVertical = !m_tempIsVertical;

  auto updateTitles = [this](QAbstractAxis *axisX, QAbstractAxis *axisY, const QString &valLabel) {
    axisX->setTitleText(m_tempIsVertical ? valLabel : "Temperature [MeV]");
    axisY->setTitleText(m_tempIsVertical ? "Temperature [MeV]" : valLabel);
  };
  updateTitles(m_densAxisX, m_densAxisY, "Densities [MeV³]");
  updateTitles(m_lepDensAxisX, m_lepDensAxisY, "Densities [MeV³]");
  updateTitles(m_muAxisX,   m_muAxisY,   "Chem. Pot. [MeV]");
  updateTitles(m_lepAxisX,  m_lepAxisY,  "Chem. Pot. [MeV]");

  replotAll();
}

void RunFromFileWidget::onThemeToggle() {
  auto toggle = [](QChart *chart) {
    QChart::ChartTheme cur = chart->theme();
    chart->setTheme(cur == QChart::ChartThemeLight ? QChart::ChartThemeDark : QChart::ChartThemeLight);
  };
  toggle(m_densView->chart());
  toggle(m_lepDensView->chart());
  toggle(m_muView->chart());
  toggle(m_lepView->chart());
  // Restore per-source pens (theme can override colors)
  for (int i = 0; i < m_sources.size(); ++i) refreshSourceSeriesColors(i);
}

// ── Logging / status ────────────────────────────────────────────────────────
void RunFromFileWidget::logMessage(const QString &msg, int sourceIdx, int rowIdx) {
  if (!m_console) return;
  QString prefix;
  if (sourceIdx >= 0) {
    QColor c = (sourceIdx < m_sources.size()) ? m_sources[sourceIdx].color : QColor("white");
    prefix = QString("<b style=\"color:%1;\">[S%2%3]</b> ")
                 .arg(c.name())
                 .arg(sourceIdx + 1)
                 .arg(rowIdx >= 0 ? QString("·R%1").arg(rowIdx + 1) : QString());
  }
  if (msg.contains("<font ") || msg.contains("<span ")) {
    m_console->append(prefix + msg);
  } else {
    m_console->append(prefix + "<span style=\"color:#ffffff;\">" + msg + "</span>");
  }
}

void RunFromFileWidget::setSourceStatus(int sourceIdx, const QString &text, const QString &cssColor) {
  if (sourceIdx < 0 || sourceIdx >= m_table->rowCount()) return;
  QTableWidgetItem *item = m_table->item(sourceIdx, COL_STATUS);
  if (!item) {
    item = new QTableWidgetItem();
    m_table->setItem(sourceIdx, COL_STATUS, item);
  }
  item->setText(text);
  item->setForeground(QBrush(QColor(cssColor)));
}

void RunFromFileWidget::clearAllSeriesAndData() {
  onClearAll();
}

// ── Export full data ───────────────────────────────────────────────────────
void RunFromFileWidget::onExportFullData() {
  // Make sure there is something to export
  bool anyData = false;
  for (const auto &src : m_sources) {
    for (const auto &pts : src.data) {
      if (!pts.isEmpty()) { anyData = true; break; }
    }
    if (anyData) break;
  }
  if (!anyData) {
    QMessageBox::warning(this, "No Data", "There is no trajectory data to export. Run the batch first.");
    return;
  }

  QString fileName = QFileDialog::getSaveFileName(this, "Export Full Data",
      m_workingDir, "Text Files (*.txt);;CSV Files (*.csv);;All Files (*)");
  if (fileName.isEmpty()) return;

  QFile file(fileName);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::critical(this, "Error", "Could not open file for writing.");
    return;
  }
  QTextStream out(&file);

  // Canonical Single-Run format. Trajectories from each (source, row) are
  // written sequentially, separated by a "# Source N, Row M ..." comment
  // line that carries the per-row run parameters.
  out << "T\tmuB\tmuQ\tmunue\tmunumu\tmnutau\tnB\tnQ\ts_QCD\ts_tot"
         "\tne\tnmu\tntau\tnnue\tnnumu\tnnutau\n";

  for (int i = 0; i < m_sources.size(); ++i) {
    const RunSource &src = m_sources[i];
    const QString srcName = QFileInfo(src.filePath).fileName();
    for (int j = 0; j < src.data.size(); ++j) {
      const auto &pts = src.data[j];
      if (pts.isEmpty()) continue;
      const TrajParamRow &p = (j < src.rows.size()) ? src.rows[j] : TrajParamRow{0, 0, 0, 0};
      out << QString("# Source S%1·R%2 [%3] (B=%4, Le=%5, Lmu=%6, Ltau=%7, eos=%8, nf=%9)\n")
                  .arg(i + 1).arg(j + 1).arg(srcName)
                  .arg(p.b).arg(p.le).arg(p.lmu).arg(p.ltau)
                  .arg(src.eos).arg(src.nf);
      for (const auto &pt : pts) {
        out << pt.T << "\t" << pt.muB << "\t" << pt.muQ
            << "\t" << pt.munue << "\t" << pt.munumu << "\t" << pt.mnutau
            << "\t" << pt.nB << "\t" << pt.nQ << "\t" << pt.s_QCD << "\t" << pt.s
            << "\t" << pt.ne << "\t" << pt.nmu << "\t" << pt.ntau
            << "\t" << pt.nnue << "\t" << pt.nnumu << "\t" << pt.nnutau
            << "\n";
      }
    }
  }
  file.close();
  QMessageBox::information(this, "Success",
      QString("Full dataset exported to:\n%1").arg(fileName));
}

void RunFromFileWidget::onExportActivePlot() {
  bool anyData = false;
  for (const auto &src : m_sources) {
    for (const auto &pts : src.data) {
      if (!pts.isEmpty()) { anyData = true; break; }
    }
    if (anyData) break;
  }
  if (!anyData) {
    QMessageBox::warning(this, "No Data", "There is no plot to export.");
    return;
  }

  int tab = m_chartTabs->currentIndex();
  TooltipChartView *view = nullptr;
  if      (tab == 0) view = m_densView;
  else if (tab == 1) view = m_muView;
  else if (tab == 2) view = m_lepView;
  if (!view) return;

  QString fileName = QFileDialog::getSaveFileName(this, "Save Active Plot",
      m_workingDir, "PDF Files (*.pdf)");
  if (fileName.isEmpty()) return;

  QPdfWriter writer(fileName);
  writer.setCreator("Cosmic Trajectories");
  writer.setPageSize(QPageSize(QPageSize::A4));
  writer.setPageOrientation(QPageLayout::Landscape);
  QPainter painter(&writer);
  view->render(&painter);
  painter.end();
  QMessageBox::information(this, "Success",
      QString("Active plot exported to:\n%1").arg(fileName));
}

// ── Solver settings dialog (mirrors MainWindow) ────────────────────────────
void RunFromFileWidget::onSolverSettingsClicked() {
  if (!m_solverDialog) {
    m_solverDialog = new QDialog(this);
    m_solverDialog->setWindowTitle("Solver Settings");
    m_solverDialog->setMinimumWidth(720);

    QVBoxLayout *vbox = new QVBoxLayout(m_solverDialog);
    QHBoxLayout *cols = new QHBoxLayout();
    QVBoxLayout *colLeft  = new QVBoxLayout();
    QVBoxLayout *colRight = new QVBoxLayout();
    cols->addLayout(colLeft);
    cols->addLayout(colRight);
    vbox->addLayout(cols);

    // Convergence
    QGroupBox *groupConv = new QGroupBox("Convergence", m_solverDialog);
    QGridLayout *gridConv = new QGridLayout(groupConv);
    gridConv->addWidget(new QLabel("Absolute Tolerance:"), 0, 0);
    QDoubleSpinBox *spinTol = new QDoubleSpinBox(m_solverDialog);
    spinTol->setDecimals(12);
    spinTol->setRange(1e-12, 1.0);
    spinTol->setValue(m_tolerance);
    spinTol->setSingleStep(1e-6);
    gridConv->addWidget(spinTol, 0, 1);
    gridConv->addWidget(new QLabel("Max Iterations:"), 1, 0);
    QSpinBox *spinMaxIter = new QSpinBox(m_solverDialog);
    spinMaxIter->setRange(10, 10000);
    spinMaxIter->setValue(m_maxIter);
    gridConv->addWidget(spinMaxIter, 1, 1);

    // Lattice QCD Interp Order
    gridConv->addWidget(new QLabel("Lattice QCD Interpolation:"), 2, 0);
    QComboBox *comboLatticeInterp = new QComboBox(m_solverDialog);
    comboLatticeInterp->addItems({"Cubic Spline (Default)", "Linear", "Akima", "Steffen"});
    comboLatticeInterp->setCurrentIndex(m_latticeInterpType);
    gridConv->addWidget(comboLatticeInterp, 2, 1);

    colLeft->addWidget(groupConv);

    // Initial guess
    QGroupBox *groupGuess = new QGroupBox("Initial Guess Values", m_solverDialog);
    QVBoxLayout *vboxGuess = new QVBoxLayout(groupGuess);
    QComboBox *comboType = new QComboBox(m_solverDialog);
    comboType->addItems({"Standard Guess", "Custom Guess"});
    comboType->setCurrentIndex(m_initialGuessType);
    vboxGuess->addWidget(comboType);

    auto addSpin = [&](QGridLayout *grid, int row, const QString &lbl, double val) {
      grid->addWidget(new QLabel(lbl), row, 0);
      QDoubleSpinBox *sp = new QDoubleSpinBox(m_solverDialog);
      sp->setRange(-10000, 10000);
      sp->setDecimals(5);
      sp->setValue(val);
      grid->addWidget(sp, row, 1);
      return sp;
    };
    QGroupBox *groupLH = new QGroupBox("Low → High Scan", m_solverDialog);
    QGridLayout *gridLH = new QGridLayout(groupLH);
    QDoubleSpinBox *lh_muB    = addSpin(gridLH, 0, "muB:",    m_customGuessLowHigh[0]);
    QDoubleSpinBox *lh_muQ    = addSpin(gridLH, 1, "muQ:",    m_customGuessLowHigh[1]);
    QDoubleSpinBox *lh_munue  = addSpin(gridLH, 2, "munue:",  m_customGuessLowHigh[2]);
    QDoubleSpinBox *lh_munumu = addSpin(gridLH, 3, "munumu:", m_customGuessLowHigh[3]);
    QDoubleSpinBox *lh_mnutau = addSpin(gridLH, 4, "mnutau:", m_customGuessLowHigh[4]);
    vboxGuess->addWidget(groupLH);
    QGroupBox *groupHL = new QGroupBox("High → Low Scan", m_solverDialog);
    QGridLayout *gridHL = new QGridLayout(groupHL);
    QDoubleSpinBox *hl_muB    = addSpin(gridHL, 0, "muB:",    m_customGuessHighLow[0]);
    QDoubleSpinBox *hl_muQ    = addSpin(gridHL, 1, "muQ:",    m_customGuessHighLow[1]);
    QDoubleSpinBox *hl_munue  = addSpin(gridHL, 2, "munue:",  m_customGuessHighLow[2]);
    QDoubleSpinBox *hl_munumu = addSpin(gridHL, 3, "munumu:", m_customGuessHighLow[3]);
    QDoubleSpinBox *hl_mnutau = addSpin(gridHL, 4, "mnutau:", m_customGuessHighLow[4]);
    vboxGuess->addWidget(groupHL);
    auto updateFields = [=](int idx) {
      bool isCustom = (idx == 1);
      groupLH->setEnabled(isCustom);
      groupHL->setEnabled(isCustom);
    };
    connect(comboType, &QComboBox::currentIndexChanged, updateFields);
    updateFields(m_initialGuessType);
    colRight->addWidget(groupGuess);

    // Metropolis
    QGroupBox *groupMetro = new QGroupBox("Metropolis Pre-Optimizer", m_solverDialog);
    QGridLayout *gridMetro = new QGridLayout(groupMetro);
    QComboBox *comboMetroMode = new QComboBox(m_solverDialog);
    comboMetroMode->addItems({"Off", "First step only (on failure)", "Always retry on failure"});
    comboMetroMode->setCurrentIndex(m_metropolisMode);
    gridMetro->addWidget(new QLabel("Mode:"), 0, 0); gridMetro->addWidget(comboMetroMode, 0, 1);
    QSpinBox *spinMetroSteps = new QSpinBox(m_solverDialog);
    spinMetroSteps->setRange(10, 50000); spinMetroSteps->setValue(m_metropolisSteps);
    gridMetro->addWidget(new QLabel("Steps:"), 1, 0); gridMetro->addWidget(spinMetroSteps, 1, 1);
    QDoubleSpinBox *spinMetroSigma = new QDoubleSpinBox(m_solverDialog);
    spinMetroSigma->setDecimals(4); spinMetroSigma->setRange(1e-4, 1000.0); spinMetroSigma->setValue(m_metropolisStepSigma);
    gridMetro->addWidget(new QLabel("Step σ (MeV):"), 2, 0); gridMetro->addWidget(spinMetroSigma, 2, 1);
    QDoubleSpinBox *spinMetroT = new QDoubleSpinBox(m_solverDialog);
    spinMetroT->setDecimals(6); spinMetroT->setRange(1e-8, 1e6); spinMetroT->setValue(m_metropolisT);
    gridMetro->addWidget(new QLabel("Temperature T_m:"), 3, 0); gridMetro->addWidget(spinMetroT, 3, 1);
    QSpinBox *spinMetroRetries = new QSpinBox(m_solverDialog);
    spinMetroRetries->setRange(1, 50);
    spinMetroRetries->setValue(m_metropolisRetries);
    spinMetroRetries->setToolTip(
        "Number of independent Metropolis chains before giving up on a "
        "failing step. Each retry also gets maxIter × N solver iterations.");
    gridMetro->addWidget(new QLabel("Retries:"), 4, 0);
    gridMetro->addWidget(spinMetroRetries, 4, 1);

    auto updMetro = [=](int idx) {
      bool on = (idx != 0);
      spinMetroSteps->setEnabled(on);
      spinMetroSigma->setEnabled(on);
      spinMetroT->setEnabled(on);
      spinMetroRetries->setEnabled(on);
    };
    connect(comboMetroMode, &QComboBox::currentIndexChanged, updMetro);
    updMetro(m_metropolisMode);
    colLeft->addWidget(groupMetro);
    colLeft->addStretch();

    QPushButton *btnSave = new QPushButton("Save and Close", m_solverDialog);
    connect(btnSave, &QPushButton::clicked, [=]() {
      m_tolerance = spinTol->value();
      m_maxIter   = spinMaxIter->value();
      m_initialGuessType = comboType->currentIndex();
      m_customGuessLowHigh = {lh_muB->value(), lh_muQ->value(), lh_munue->value(), lh_munumu->value(), lh_mnutau->value()};
      m_customGuessHighLow = {hl_muB->value(), hl_muQ->value(), hl_munue->value(), hl_munumu->value(), hl_mnutau->value()};
      m_metropolisMode      = comboMetroMode->currentIndex();
      m_metropolisSteps     = spinMetroSteps->value();
      m_metropolisStepSigma = spinMetroSigma->value();
      m_metropolisT         = spinMetroT->value();
      m_metropolisRetries   = spinMetroRetries->value();
      m_latticeInterpType   = comboLatticeInterp->currentIndex();
      m_solverDialog->accept();
    });
    vbox->addWidget(btnSave);
  }
  m_solverDialog->show();
  m_solverDialog->raise();
  m_solverDialog->activateWindow();
}

// ── Per-quantity abs / legend helpers ─────────────────────────────────────
double RunFromFileWidget::absVal(double v, bool useAbs) const {
  if (m_isLogScale) return std::max(std::abs(v), 1e-15);
  return useAbs ? std::abs(v) : v;
}

QString RunFromFileWidget::legendSuffix(const TrajParamRow &row) const {
  QStringList parts;
  if (m_legendShowB)    parts << QString("B=%1").arg(row.b,    0, 'g', 4);
  if (m_legendShowLe)   parts << QString("Le=%1").arg(row.le,  0, 'g', 4);
  if (m_legendShowLmu)  parts << QString("Lμ=%1").arg(row.lmu, 0, 'g', 4);
  if (m_legendShowLtau) parts << QString("Lτ=%1").arg(row.ltau,0, 'g', 4);
  if (parts.isEmpty()) return QString();
  return QString(" (%1)").arg(parts.join(", "));
}

void RunFromFileWidget::refreshSeriesNames() {
  auto bar = [](bool on, const QString &s){ return on ? QString("|%1|").arg(s) : s; };
  // Every row of every source gets its own legend marker so per-row run
  // parameters (B, Le, Lμ, Lτ) are distinguishable. To keep the legend
  // tidy the user can hide entire quantities via "Show/Hide Quantities".
  for (int i = 0; i < m_sources.size(); ++i) {
    auto &src = m_sources[i];
    auto setNameFor = [&, this](QChart *chart, QVector<QLineSeries*> &vec,
                                const QString &base, bool useAbs) {
      const bool absDisplay = m_isLogScale || useAbs;
      const QString shown = bar(absDisplay, base);
      for (int j = 0; j < vec.size(); ++j) {
        QLineSeries *ls = vec[j];
        if (!ls) continue;
        const TrajParamRow &row = (j < src.rows.size()) ? src.rows[j] : TrajParamRow{};
        const QString srcTag = m_legendShowSourceTag ? QString("S%1").arg(i + 1)
                                                      : QString();
        // Build "S1·R3 ", "R3 ", "S1 ", or "" depending on tag and row count.
        QString prefix;
        const bool needRowTag = (vec.size() > 1);
        if (!srcTag.isEmpty() && needRowTag) {
          prefix = QString("%1·R%2 ").arg(srcTag).arg(j + 1);
        } else if (!srcTag.isEmpty()) {
          prefix = srcTag + QStringLiteral(" ");
        } else if (needRowTag) {
          prefix = QString("R%1 ").arg(j + 1);
        }
        ls->setName(prefix + shown + legendSuffix(row));

        // Show every row's legend marker.
        if (chart && chart->legend()) {
          const auto markers = chart->legend()->markers(ls);
          for (auto *m : markers) m->setVisible(true);
        }
      }
    };
    setNameFor(m_densView->chart(),    src.series_nB,     "nB",     m_useAbsnB);
    setNameFor(m_densView->chart(),    src.series_s,      "s",      m_useAbsS);
    setNameFor(m_densView->chart(),    src.series_nQ,     "nQ", m_useAbsnQ);
    setNameFor(m_lepDensView->chart(), src.series_ne,     "ne",     m_useAbsNe);
    setNameFor(m_lepDensView->chart(), src.series_nmu,    "nμ",     m_useAbsNmu);
    setNameFor(m_lepDensView->chart(), src.series_ntau,   "nτ",     m_useAbsNtau);
    setNameFor(m_lepDensView->chart(), src.series_nnue,   "nνe",    m_useAbsNnue);
    setNameFor(m_lepDensView->chart(), src.series_nnumu,  "nνμ",    m_useAbsNnumu);
    setNameFor(m_lepDensView->chart(), src.series_nnutau, "nντ",    m_useAbsNnutau);
    setNameFor(m_muView->chart(),      src.series_muB,    "μB",     m_useAbsMuB);
    setNameFor(m_muView->chart(),      src.series_muQ,    "μQ",     m_useAbsMuQ);
    setNameFor(m_lepView->chart(),     src.series_munue,  "μνe",    m_useAbsMunue);
    setNameFor(m_lepView->chart(),     src.series_munumu, "μνμ",    m_useAbsMunumu);
    setNameFor(m_lepView->chart(),     src.series_mnutau, "μντ",    m_useAbsMnutau);
  }
}

void RunFromFileWidget::refreshLegendVisibility() {
  TooltipChartView *views[] = {m_densView, m_lepDensView, m_muView, m_lepView};
  for (auto *v : views) {
    if (v && v->chart()) v->chart()->legend()->setVisible(m_legendVisible);
  }
}

void RunFromFileWidget::onConfigureAxisFontsClicked() {
    bool ok = false;
    QFont currentFont = this->font();
    if (m_densAxisX) {
        currentFont = m_densAxisX->labelsFont();
    }
    QFont font = QFontDialog::getFont(&ok, currentFont, this);
    if (ok) {
        m_axisFont = font;
        m_axisFontValid = true;
        applyAxisFonts();
    }
}

void RunFromFileWidget::applyAxisFonts() {
    if (!m_axisFontValid) return;
    QAbstractAxis* axes[] = {
        m_densAxisX, m_densAxisY,
        m_lepDensAxisX, m_lepDensAxisY,
        m_muAxisX, m_muAxisY,
        m_lepAxisX, m_lepAxisY
    };
    for (auto* ax : axes) {
        if (ax) {
            ax->setLabelsFont(m_axisFont);
            ax->setTitleFont(m_axisFont);
        }
    }
}
