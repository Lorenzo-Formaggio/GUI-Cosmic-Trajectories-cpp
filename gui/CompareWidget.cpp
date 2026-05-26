#include "CompareWidget.h"

#include <QApplication>
#include <QtWidgets>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QTabWidget>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QThread>
#include <QDir>
#include <QCheckBox>
#include <QMessageBox>
#include <QFontDialog>
#include <QFileDialog>
#include <QPdfWriter>
#include <QPainter>
#include <QTextStream>
#include <QLineEdit>
#include <QMenu>
#include <QAction>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QLogValueAxis>
#include <QtCharts/QValueAxis>
#include "TooltipChartView.h"

#include <cmath>
#include <algorithm>

// ── Slot color palette (cycles for slots beyond the 10th) ─────────────────
QColor CompareWidget::pickSlotColor(int i) const {
  static const QList<QColor> palette = {
    QColor( 31, 119, 180), QColor(214,  39,  40), QColor( 44, 160,  44),
    QColor(255, 127,  14), QColor(148, 103, 189), QColor(140,  86,  75),
    QColor(227, 119, 194), QColor(127, 127, 127), QColor(188, 189,  34),
    QColor( 23, 190, 207),
  };
  return palette[((i % palette.size()) + palette.size()) % palette.size()];
}

int CompareWidget::slotIndex(const SlotConfig *s) const {
  for (int i = 0; i < m_slots.size(); ++i) if (m_slots[i] == s) return i;
  return -1;
}

// ── Construction ───────────────────────────────────────────────────────────
CompareWidget::CompareWidget(const QString &workingDir, QWidget *parent)
    : QWidget(parent), m_workingDir(workingDir)
{
  setupUi();
}

CompareWidget::~CompareWidget() {
  for (auto *s : m_slots) {
    if (s && s->thread) {
      s->thread->quit();
      s->thread->wait();
      delete s->worker;
      delete s->thread;
    }
    delete s;
  }
  m_slots.clear();
}

void CompareWidget::updateSlotSeriesVisibility(SlotConfig *s) {
  if (!s) return;
  bool hasData = !s->data.isEmpty();

  auto isChecked = [](QCheckBox *chk) { return chk && chk->isChecked(); };

  if (s->sernB)     s->sernB->setVisible(hasData && isChecked(m_chknB));
  if (s->serS)      s->serS->setVisible(hasData && isChecked(m_chkS));
  if (s->sernQ)     s->sernQ->setVisible(hasData && isChecked(m_chknQ));
  if (s->serMuB)    s->serMuB->setVisible(hasData && isChecked(m_chkMuB));
  if (s->serMuQ)    s->serMuQ->setVisible(hasData && isChecked(m_chkMuQ));
  if (s->serMunue)  s->serMunue->setVisible(hasData && isChecked(m_chkMunue));
  if (s->serMunumu) s->serMunumu->setVisible(hasData && isChecked(m_chkMunumu));
  if (s->serMnutau) s->serMnutau->setVisible(hasData && isChecked(m_chkMnutau));
}

// ── UI setup ───────────────────────────────────────────────────────────────
void CompareWidget::setupUi() {
  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->setContentsMargins(4, 4, 4, 4);

  QSplitter *splitter = new QSplitter(Qt::Horizontal, this); // Changed to horizontal like Single Run!
  mainLayout->addWidget(splitter);

  // Left half: slot rows (Scroll area)
  QWidget *leftPanel = new QWidget(splitter);
  QVBoxLayout *leftPanelLayout = new QVBoxLayout(leftPanel);
  leftPanelLayout->setContentsMargins(0, 0, 0, 0);
  
  m_btnSolverSettings = new QPushButton("Solver Settings...");
  m_btnSolverSettings->setObjectName("BtnSolver");
  connect(m_btnSolverSettings, &QPushButton::clicked, this, &CompareWidget::onSolverSettingsButtonClicked);
  leftPanelLayout->addWidget(m_btnSolverSettings);
  
  QScrollArea *scroll = new QScrollArea(leftPanel);
  scroll->setWidgetResizable(true);
  QWidget *scrollContent = new QWidget(scroll);
  createSlotPanel(scrollContent);
  scroll->setWidget(scrollContent);
  leftPanelLayout->addWidget(scroll);

  // Console below slots
  QGroupBox *groupConsole = new QGroupBox("Console", leftPanel);
  QVBoxLayout *consoleLayout = new QVBoxLayout(groupConsole);
  m_console = new QTextEdit();
  m_console->setReadOnly(true);
  m_console->setFontFamily("Courier");
  m_console->setMinimumHeight(150);
  consoleLayout->addWidget(m_console);
  leftPanelLayout->addWidget(groupConsole);

  // Right half: comparison charts
  QWidget *chartsWidget = new QWidget(splitter);
  QVBoxLayout *chartsLayout = new QVBoxLayout(chartsWidget);
  chartsLayout->setContentsMargins(0, 0, 0, 0);
  createChartPanel(chartsWidget);
  chartsLayout->addWidget(m_chartTabs);
  
  // ── Plot Settings Menu (Inherits Global Style) ───────────────────────────
  QPushButton *btnPlotSettings = new QPushButton("Plot Settings", chartsWidget);
  
  QMenu *plotMenu = new QMenu(this);

  // Initialize visibility checkboxes early to avoid null pointer issues.
  // They live on `this` until the dialog reparents them, so hide them now to
  // keep them from showing as floating widgets in the corner.
  auto makeChk = [this](const QString &label, bool checked) -> QCheckBox* {
    QCheckBox *chk = new QCheckBox(label, this);
    chk->setChecked(checked);
    chk->hide();
    return chk;
  };
  m_chknB      = makeChk("nB",       true);
  m_chkS       = makeChk("s",        true);
  m_chknQ      = makeChk("nQ",   true);
  m_chkMuB     = makeChk("μB",       true);
  m_chkMuQ     = makeChk("μQ",       true);
  m_chkMunue   = makeChk("μνe",      true);
  m_chkMunumu  = makeChk("μνμ",      true);
  m_chkMnutau  = makeChk("μντ",      true);

  // Per-quantity abs checkboxes
  auto makeAbs = [this](bool checked) -> QCheckBox* {
    QCheckBox *chk = new QCheckBox("|·|", this);
    chk->setChecked(checked);
    chk->setToolTip("Plot absolute value of this quantity. Forced ON in log mode.");
    chk->setEnabled(!m_isLogScale);
    chk->hide();
    return chk;
  };
  m_absnB      = makeAbs(m_useAbsnB);
  m_absS       = makeAbs(m_useAbsS);
  m_absnQ      = makeAbs(m_useAbsnQ);
  m_absMuB     = makeAbs(m_useAbsMuB);
  m_absMuQ     = makeAbs(m_useAbsMuQ);
  m_absMunue   = makeAbs(m_useAbsMunue);
  m_absMunumu  = makeAbs(m_useAbsMunumu);
  m_absMnutau  = makeAbs(m_useAbsMnutau);

  // Wire checkboxes to series visibility updates
  auto updateAll = [this]{ for (auto *s : m_slots) updateSlotSeriesVisibility(s); };
  connect(m_chknB,    &QCheckBox::toggled, this, updateAll);
  connect(m_chkS,     &QCheckBox::toggled, this, updateAll);
  connect(m_chknQ,    &QCheckBox::toggled, this, updateAll);
  connect(m_chkMuB,   &QCheckBox::toggled, this, updateAll);
  connect(m_chkMuQ,   &QCheckBox::toggled, this, updateAll);
  connect(m_chkMunue, &QCheckBox::toggled, this, updateAll);
  connect(m_chkMunumu,&QCheckBox::toggled, this, updateAll);
  connect(m_chkMnutau,&QCheckBox::toggled, this, updateAll);

  // Wire abs checkboxes — replot from cached data
  auto onAbsChanged = [this]() { replotData(); };
  connect(m_absnB,    &QCheckBox::toggled, this, [this](bool on){ m_useAbsnB = on;     replotData(); });
  connect(m_absS,     &QCheckBox::toggled, this, [this](bool on){ m_useAbsS = on;      replotData(); });
  connect(m_absnQ,    &QCheckBox::toggled, this, [this](bool on){ m_useAbsnQ = on;     replotData(); });
  connect(m_absMuB,   &QCheckBox::toggled, this, [this](bool on){ m_useAbsMuB = on;    replotData(); });
  connect(m_absMuQ,   &QCheckBox::toggled, this, [this](bool on){ m_useAbsMuQ = on;    replotData(); });
  connect(m_absMunue, &QCheckBox::toggled, this, [this](bool on){ m_useAbsMunue = on;  replotData(); });
  connect(m_absMunumu,&QCheckBox::toggled, this, [this](bool on){ m_useAbsMunumu = on; replotData(); });
  connect(m_absMnutau,&QCheckBox::toggled, this, [this](bool on){ m_useAbsMnutau = on; replotData(); });
  (void)onAbsChanged;
  
  // Section: Visibility
  QAction *actShowHide = plotMenu->addAction("Show/Hide Quantities...");
  connect(actShowHide, &QAction::triggered, this, [this]() {
    if (!m_visDialog) {
      m_visDialog = new QDialog(this);
      m_visDialog->setWindowTitle("Show/Hide Quantities Across All Slots");
      m_visDialog->setMinimumWidth(380);
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
    // Re-sync abs-checkbox enabled state with current scale before showing
    auto syncAbs = [this](QCheckBox *c){ if (c) c->setEnabled(!m_isLogScale); };
    syncAbs(m_absnB);  syncAbs(m_absS);   syncAbs(m_absnQ);
    syncAbs(m_absMuB); syncAbs(m_absMuQ);
    syncAbs(m_absMunue); syncAbs(m_absMunumu); syncAbs(m_absMnutau);
    m_visDialog->show();
    m_visDialog->raise();
    m_visDialog->activateWindow();
  });

  plotMenu->addSeparator();

  // Section: View Controls
  QAction *actAxis = plotMenu->addAction("Toggle Axes");
  connect(actAxis, &QAction::triggered, this, [this]{ onAxisToggleClicked(); });

  QAction *actScale = plotMenu->addAction("Toggle Log/Linear");
  connect(actScale, &QAction::triggered, this, [this]{ onScaleToggleClicked(); });

  QAction *actTheme = plotMenu->addAction("Toggle Plot Theme");
  connect(actTheme, &QAction::triggered, this, [this]{ onThemeToggleClicked(); });

  QAction *actAxisFont = plotMenu->addAction("Configure Axis Fonts...");
  connect(actAxisFont, &QAction::triggered, this, &CompareWidget::onConfigureAxisFontsClicked);

  // Show/hide chart legend
  QAction *actLegend = plotMenu->addAction("Show Legend");
  actLegend->setCheckable(true);
  actLegend->setChecked(m_legendVisible);
  connect(actLegend, &QAction::toggled, this, [this](bool on) {
    m_legendVisible = on;
    refreshLegendVisibility();
  });

  // Configure which run parameters appear in legend names
  QAction *actLegendCfg = plotMenu->addAction("Configure Legend...");
  connect(actLegendCfg, &QAction::triggered, this, [this]() {
    QDialog dlg(this);
    dlg.setWindowTitle("Legend Content");
    QVBoxLayout *v = new QVBoxLayout(&dlg);
    v->addWidget(new QLabel("Append per-slot run parameters to legend entries:"));
    auto *cB    = new QCheckBox("B  (baryon number)");        cB->setChecked(m_legendShowB);
    auto *cLe   = new QCheckBox("Le (electron lepton)");      cLe->setChecked(m_legendShowLe);
    auto *cLmu  = new QCheckBox("Lμ (muon lepton)");          cLmu->setChecked(m_legendShowLmu);
    auto *cLtau = new QCheckBox("Lτ (tau lepton)");           cLtau->setChecked(m_legendShowLtau);
    v->addWidget(cB); v->addWidget(cLe); v->addWidget(cLmu); v->addWidget(cLtau);
    QPushButton *ok = new QPushButton("OK");
    connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    v->addWidget(ok);
    if (dlg.exec() == QDialog::Accepted) {
      m_legendShowB    = cB->isChecked();
      m_legendShowLe   = cLe->isChecked();
      m_legendShowLmu  = cLmu->isChecked();
      m_legendShowLtau = cLtau->isChecked();
      refreshSeriesNames();
    }
  });

  plotMenu->addSeparator();

  // Section: Tools & Export
  QAction *actCP = plotMenu->addAction("Configure Critical Point...");
  connect(actCP, &QAction::triggered, this, &CompareWidget::onCriticalPointButtonClicked);

  QAction *actExport = plotMenu->addAction("Export Active Plot...");
  connect(actExport, &QAction::triggered, this, &CompareWidget::onExportClicked);

  btnPlotSettings->setMenu(plotMenu);

  // ── Auto-Fit Limits button (next to Plot Settings) ───────────────────
  QPushButton *btnAutoFit = new QPushButton("Auto-Fit Limits", chartsWidget);
  btnAutoFit->setToolTip("Fit axis ranges to the currently visible curves "
                         "in the active chart (uses log/linear margins).");
  connect(btnAutoFit, &QPushButton::clicked, this, [this]() {
    QWidget *current = m_chartTabs->currentWidget();
    auto *view = qobject_cast<TooltipChartView*>(current);
    if (view && view->chart()) {
      autoFitChartFromVisibleSeries(view->chart());
    }
  });

  QHBoxLayout *bottomRightLayout = new QHBoxLayout();
  bottomRightLayout->setContentsMargins(0, 0, 0, 15);
  bottomRightLayout->addStretch();
  bottomRightLayout->addWidget(btnPlotSettings);
  bottomRightLayout->addWidget(btnAutoFit);
  bottomRightLayout->addStretch();
  chartsLayout->addLayout(bottomRightLayout);



  splitter->addWidget(leftPanel);
  splitter->addWidget(chartsWidget);
  splitter->setSizes({350, 650});
}

// ── Slot Panel ─────────────────────────────────────────────────────────────
void CompareWidget::createSlotPanel(QWidget *parent) {
  QVBoxLayout *outerLayout = new QVBoxLayout(parent);
  outerLayout->setContentsMargins(4, 4, 4, 4);
  outerLayout->setSpacing(8);

  // Toolbar with the "Add Slot" button.
  m_btnAddSlot = new QPushButton("+ Add Slot", parent);
  connect(m_btnAddSlot, &QPushButton::clicked, this, [this]() { addSlot(); });
  outerLayout->addWidget(m_btnAddSlot);

  // Slots stack: groupboxes append here, with a stretch at the bottom so
  // they pile from the top.
  m_slotsLayout = new QVBoxLayout();
  m_slotsLayout->setContentsMargins(0, 0, 0, 0);
  m_slotsLayout->setSpacing(8);
  outerLayout->addLayout(m_slotsLayout);
  outerLayout->addStretch();

  // Default to two slots (compare needs at least two to be meaningful).
  addSlot();
  addSlot();
}

void CompareWidget::addSlot() {
  auto *s = new SlotConfig;
  s->color = pickSlotColor(m_slots.size());
  m_slots.append(s);
  buildSlotUi(s);
  if (m_densAxisX) buildSlotSeries(s);
  refreshSeriesNames();
  refreshLegendVisibility();
}

void CompareWidget::removeSlot(SlotConfig *s) {
  if (!s) return;
  // Stop any in-flight worker first.
  if (s->thread) {
    if (s->worker) {
      disconnect(s->worker, nullptr, nullptr, nullptr);
      s->worker->stop();
    }
    s->thread->quit();
    s->thread->wait();
    delete s->worker;
    delete s->thread;
    s->worker = nullptr;
    s->thread = nullptr;
  }
  // Detach + remove the per-slot chart series.
  auto detach = [](QChart *chart, QLineSeries *ls) {
    if (chart && ls) chart->removeSeries(ls);
    delete ls;
  };
  if (m_densChartView) {
    detach(m_densChartView->chart(), s->sernB);
    detach(m_densChartView->chart(), s->serS);
    detach(m_densChartView->chart(), s->sernQ);
  }
  if (m_muChartView) {
    detach(m_muChartView->chart(), s->serMuB);
    detach(m_muChartView->chart(), s->serMuQ);
  }
  if (m_lepChartView) {
    detach(m_lepChartView->chart(), s->serMunue);
    detach(m_lepChartView->chart(), s->serMunumu);
    detach(m_lepChartView->chart(), s->serMnutau);
  }
  // Remove the groupbox from the panel and the slot from the vector.
  if (s->box) {
    if (m_slotsLayout) m_slotsLayout->removeWidget(s->box);
    s->box->deleteLater();
  }
  m_slots.removeAll(s);
  delete s;
  refreshSeriesNames();
  updateChartAxes();
}

void CompareWidget::buildSlotUi(SlotConfig *s) {
  const int slotNum = m_slots.size();   // 1-based number for title
  s->box = new QGroupBox(QString("Slot %1").arg(slotNum));
  s->box->setCheckable(true);
  s->box->setChecked(slotNum <= 2);   // first couple expanded by default
  s->box->setStyleSheet(
      QString("QGroupBox { border: 2px solid %1; border-radius: 4px; margin-top: 2ex; }"
              "QGroupBox::title { color: %1; font-weight: bold; subcontrol-origin: margin; left: 8px; }")
          .arg(s->color.name()));

  QVBoxLayout *boxLayout = new QVBoxLayout(s->box);
  QWidget *container = new QWidget(s->box);
  QGridLayout *grid = new QGridLayout(container);
  grid->setContentsMargins(0, 0, 0, 0);

  connect(s->box, &QGroupBox::toggled, container, &QWidget::setVisible);
  container->setVisible(s->box->isChecked());

  int row = 0;
  auto addSpin = [&](const QString &lbl, double val, double lo, double hi, int dec) -> QDoubleSpinBox* {
    grid->addWidget(new QLabel(lbl), row, 0);
    QDoubleSpinBox *sp = new QDoubleSpinBox();
    sp->setDecimals(dec);
    sp->setRange(lo, hi);
    sp->setValue(val);
    grid->addWidget(sp, row, 1);
    row++;
    return sp;
  };

  s->spinB    = addSpin("b",          8.6e-11, 1e-15, 1.0,    12);
  s->spinLe   = addSpin("le",         -0.01,  -1.0,  1.0,    12);
  s->spinLmu  = addSpin("lmu",        -0.01,  -1.0,  1.0,    12);
  s->spinLtau = addSpin("ltau",       -0.01,  -1.0,  1.0,    12);
  s->spinDT   = addSpin("dT (MeV)",   1.0,     0.01, 100.0,  2);
  s->spinTmin = addSpin("Tmin (MeV)", 80.0,    0.1,  10000.0, 1);
  s->spinTmax = addSpin("Tmax (MeV)", 250.0,   0.1,  10000.0, 1);

  grid->addWidget(new QLabel("Flavors (nf)"), row, 0);
  s->comboNf = new QComboBox();
  s->comboNf->addItems({"2", "3", "4"});
  s->comboNf->setCurrentIndex(1);
  grid->addWidget(s->comboNf, row++, 1);

  grid->addWidget(new QLabel("EoS"), row, 0);
  s->comboEos = new QComboBox();
  s->comboEos->addItems({"Free QGP (0)", "Lattice QCD (1)", "Interpolated Table (2)", "Entropy Contour (3)", "Entropy Contour Param (4)"});
  s->comboEos->setCurrentIndex(1);
  grid->addWidget(s->comboEos, row++, 1);

  s->eosPathWidget = new QWidget();
  QHBoxLayout *eosPathLayout = new QHBoxLayout(s->eosPathWidget);
  eosPathLayout->setContentsMargins(0, 0, 0, 0);
  s->lineEditEosPath = new QLineEdit();
  s->lineEditEosPath->setPlaceholderText("Path to EoS table...");
  s->btnBrowseEos = new QPushButton("Browse");
  eosPathLayout->addWidget(s->lineEditEosPath);
  eosPathLayout->addWidget(s->btnBrowseEos);

  s->labelEosPath = new QLabel("Table Path");
  grid->addWidget(s->labelEosPath, row, 0);
  grid->addWidget(s->eosPathWidget, row++, 1);

  s->labelEosPath->setVisible(false);
  s->eosPathWidget->setVisible(false);

  connect(s->btnBrowseEos, &QPushButton::clicked, this, [this, s]() {
    QString file = QFileDialog::getOpenFileName(this, "Select EoS Table File",
        QDir::currentPath(), "Text Files (*.txt);;All Files (*)");
    if (!file.isEmpty()) s->lineEditEosPath->setText(file);
  });

  connect(s->comboEos, &QComboBox::currentIndexChanged, this, [s](int index) {
    bool showEosPath = (index == 2);
    s->labelEosPath->setVisible(showEosPath);
    s->eosPathWidget->setVisible(showEosPath);
  });

  grid->addWidget(new QLabel("Guess Method"), row, 0);
  s->comboGuess = new QComboBox();
  s->comboGuess->addItems({"Constant (0)", "Linear Extrap (1)"});
  s->comboGuess->setCurrentIndex(1);
  grid->addWidget(s->comboGuess, row++, 1);

  grid->addWidget(new QLabel("Scan Direction"), row, 0);
  s->comboScan = new QComboBox();
  s->comboScan->addItems({"Low -> High (0)", "High -> Low (1)"});
  grid->addWidget(s->comboScan, row++, 1);

  QHBoxLayout *btnLayout = new QHBoxLayout();

  s->btnRun = new QPushButton("Run Slot");
  s->btnRun->setStyleSheet(
      QString("QPushButton { background-color: %1; color: white; border-radius: 6px; font-weight: bold; padding: 4px; border: 1px solid rgba(0,0,0,0.2); }")
          .arg(s->color.name()));
  connect(s->btnRun, &QPushButton::clicked, this, [this, s] { runSlot(s); });
  btnLayout->addWidget(s->btnRun);

  s->btnStop = new QPushButton("Stop");
  s->btnStop->setObjectName("BtnStop");
  connect(s->btnStop, &QPushButton::clicked, this, [s]() {
    if (s->worker) {
      s->worker->stop();
      s->btnStop->setEnabled(false);
    }
  });
  btnLayout->addWidget(s->btnStop);

  s->btnClear = new QPushButton("Clear");
  connect(s->btnClear, &QPushButton::clicked, this, [this, s] { clearSlot(s); });
  btnLayout->addWidget(s->btnClear);

  s->btnRemove = new QPushButton("Remove");
  s->btnRemove->setToolTip("Remove this slot");
  s->btnRemove->setStyleSheet(
      "QPushButton { background-color: #b03030; color: white; "
      "border-radius: 6px; font-weight: bold; padding: 4px 10px; }");
  connect(s->btnRemove, &QPushButton::clicked, this, [this, s] {
    if (m_slots.size() <= 1) {
      QMessageBox::information(this, "Cannot Remove",
          "At least one slot is required.");
      return;
    }
    removeSlot(s);
  });
  btnLayout->addWidget(s->btnRemove);

  grid->addLayout(btnLayout, row++, 0, 1, 2);

  s->statusLabel = new QLabel("Ready");
  s->statusLabel->setAlignment(Qt::AlignCenter);
  s->statusLabel->setStyleSheet("color: gray;");
  grid->addWidget(s->statusLabel, row++, 0, 1, 2);

  boxLayout->addWidget(container);
  if (m_slotsLayout) m_slotsLayout->addWidget(s->box);
}

void CompareWidget::buildSlotSeries(SlotConfig *s) {
  static const Qt::PenStyle QTY_STYLES[3] = {Qt::SolidLine, Qt::DashLine, Qt::DotLine};
  QChart *c1 = m_densChartView->chart();
  QChart *c2 = m_muChartView->chart();
  QChart *c3 = m_lepChartView->chart();

  auto makeSeries = [&](const QString &suffix, QChart *ch,
                         QAbstractAxis *ax, QAbstractAxis *ay,
                         Qt::PenStyle ps) -> QLineSeries * {
    auto *ser = new QLineSeries();
    ser->setName(suffix);
    QPen pen(s->color);
    pen.setStyle(ps);
    pen.setWidthF(2.0);
    ser->setPen(pen);
    ser->setVisible(false);
    ch->addSeries(ser);
    ser->attachAxis(ax);
    ser->attachAxis(ay);
    return ser;
  };

  s->sernB    = makeSeries("nB",    c1, m_densAxisX, m_densAxisY, QTY_STYLES[0]);
  s->serS     = makeSeries("s",     c1, m_densAxisX, m_densAxisY, QTY_STYLES[1]);
  s->sernQ    = makeSeries("|nQ|",  c1, m_densAxisX, m_densAxisY, QTY_STYLES[2]);

  s->serMuB   = makeSeries("|μB|",  c2, m_muAxisX,  m_muAxisY,  QTY_STYLES[0]);
  s->serMuQ   = makeSeries("|μQ|",  c2, m_muAxisX,  m_muAxisY,  QTY_STYLES[1]);

  s->serMunue  = makeSeries("|μνe|", c3, m_lepAxisX, m_lepAxisY, QTY_STYLES[0]);
  s->serMunumu = makeSeries("|μνμ|", c3, m_lepAxisX, m_lepAxisY, QTY_STYLES[1]);
  s->serMnutau = makeSeries("|μντ|", c3, m_lepAxisX, m_lepAxisY, QTY_STYLES[2]);
}

// ── Chart Panel ────────────────────────────────────────────────────────────
void CompareWidget::createChartPanel(QWidget *parent) {
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
        axY = new QLogValueAxis();
        static_cast<QLogValueAxis*>(axY)->setBase(10.0);
    } else {
        axX = new QValueAxis();
        axY = new QValueAxis();
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

    view = new TooltipChartView(chart);
    view->setRenderHint(QPainter::Antialiasing);
    m_chartTabs->addTab(view, title);
  };

  QChart *c1, *c2, *c3;
  makeChart(m_densChartView, c1, m_densAxisX, m_densAxisY,
            "Densities",          "Densities [MeV³]");
  makeChart(m_muChartView,   c2, m_muAxisX,   m_muAxisY,
            "Baryon & Electric μ","Chem. Pot. [MeV]");
  makeChart(m_lepChartView,  c3, m_lepAxisX,  m_lepAxisY,
            "Lepton μ",           "Chem. Pot. [MeV]");

  // Per-slot series are created on demand by buildSlotSeries() — slots can
  // be added/removed dynamically. Walk any pre-existing slots and attach.
  for (auto *s : m_slots) buildSlotSeries(s);

  m_seriesCpB = new QScatterSeries(); m_seriesCpB->setName("CP |μB|"); m_seriesCpB->setMarkerShape(QScatterSeries::MarkerShapeStar); m_seriesCpB->setMarkerSize(12.0); m_seriesCpB->setColor(Qt::red); m_seriesCpB->setBorderColor(Qt::black);
  m_seriesCpQ = new QScatterSeries(); m_seriesCpQ->setName("CP |μQ|"); m_seriesCpQ->setMarkerShape(QScatterSeries::MarkerShapeStar); m_seriesCpQ->setMarkerSize(12.0); m_seriesCpQ->setColor(QColor(128, 0, 128)); m_seriesCpQ->setBorderColor(Qt::black);
  c2->addSeries(m_seriesCpB); m_seriesCpB->attachAxis(m_muAxisX); m_seriesCpB->attachAxis(m_muAxisY); m_seriesCpB->setVisible(false);
  c2->addSeries(m_seriesCpQ); m_seriesCpQ->attachAxis(m_muAxisX); m_seriesCpQ->attachAxis(m_muAxisY); m_seriesCpQ->setVisible(false);
}

// ── Run a slot ─────────────────────────────────────────────────────────────
void CompareWidget::runSlot(SlotConfig *s) {
  if (!s) return;

  if (s->thread) {
    disconnect(s->worker, nullptr, nullptr, nullptr); // Prevent pending signals
    s->thread->quit();
    s->thread->wait();
    delete s->worker;
    delete s->thread;
    s->worker = nullptr;
    s->thread = nullptr;
  }

  clearSlotSeries(s);
  s->data.clear();

  s->statusLabel->setText("Running…");
  s->statusLabel->setStyleSheet("color: #17a2b8; font-weight: bold;");
  s->btnRun->setEnabled(false);
  s->btnStop->setEnabled(true);

  s->thread = new QThread();
  s->worker = new SimulationWorker();

  s->worker->b             = s->spinB->value();
  s->worker->le            = s->spinLe->value();
  s->worker->lmu           = s->spinLmu->value();
  s->worker->ltau          = s->spinLtau->value();

  // Snapshot run params for legend display
  s->B    = s->spinB->value();
  s->Le   = s->spinLe->value();
  s->Lmu  = s->spinLmu->value();
  s->Ltau = s->spinLtau->value();
  s->runParamsValid = true;
  refreshSeriesNames();
  refreshLegendVisibility();
  s->worker->dT            = s->spinDT->value();
  s->worker->Tmin          = s->spinTmin->value();
  s->worker->Tmax          = s->spinTmax->value();
  s->worker->nf            = s->comboNf->currentText().toInt();
  s->worker->eos           = s->comboEos->currentIndex();
  s->worker->scanDirection = s->comboScan->currentIndex();
  s->worker->workingDir    = m_workingDir;
  s->worker->eosTableFilePath = s->lineEditEosPath->text();

  s->worker->tolerance     = m_tolerance;
  s->worker->maxIter       = m_maxIter;
  s->worker->guessMethod   = s->comboGuess->currentIndex();

  s->worker->metropolisMode      = m_metropolisMode;
  s->worker->metropolisSteps     = m_metropolisSteps;
  s->worker->metropolisStepSigma = m_metropolisStepSigma;
  s->worker->metropolisT         = m_metropolisT;
  s->worker->metropolisRetries   = m_metropolisRetries;
  s->worker->latticeInterpType   = m_latticeInterpType;

  s->worker->moveToThread(s->thread);
  connect(s->thread, &QThread::started, s->worker, &SimulationWorker::run);

  connect(s->worker, &SimulationWorker::logMessage, this, [this, s](const QString &msg) {
    onLogMessage(msg, s);
  });

  connect(s->worker, &SimulationWorker::stepCompleted, this,
          [this, s](TrajectoryPoint pt) {
    if (!s->worker) return; // Slot was cleared/stopped
    s->data.append(pt);

    updateSlotSeriesVisibility(s);

    if (m_tempIsVertical) {
      if (s->sernB) s->sernB->append(absVal(pt.nB, m_useAbsnB), pt.T);
      if (s->serS)  s->serS->append(absVal(pt.s, m_useAbsS),    pt.T);
      if (s->sernQ) s->sernQ->append(absVal(pt.nQ, m_useAbsnQ), pt.T);
      if (s->serMuB) s->serMuB->append(absVal(pt.muB, m_useAbsMuB), pt.T);
      if (s->serMuQ) s->serMuQ->append(absVal(pt.muQ, m_useAbsMuQ), pt.T);
      if (s->serMunue) s->serMunue->append(absVal(pt.munue, m_useAbsMunue), pt.T);
      if (s->serMunumu) s->serMunumu->append(absVal(pt.munumu, m_useAbsMunumu), pt.T);
      if (s->serMnutau) s->serMnutau->append(absVal(pt.mnutau, m_useAbsMnutau), pt.T);
    } else {
      if (s->sernB) s->sernB->append(pt.T, absVal(pt.nB, m_useAbsnB));
      if (s->serS)  s->serS->append(pt.T, absVal(pt.s, m_useAbsS));
      if (s->sernQ) s->sernQ->append(pt.T, absVal(pt.nQ, m_useAbsnQ));
      if (s->serMuB) s->serMuB->append(pt.T, absVal(pt.muB, m_useAbsMuB));
      if (s->serMuQ) s->serMuQ->append(pt.T, absVal(pt.muQ, m_useAbsMuQ));
      if (s->serMunue) s->serMunue->append(pt.T, absVal(pt.munue, m_useAbsMunue));
      if (s->serMunumu) s->serMunumu->append(pt.T, absVal(pt.munumu, m_useAbsMunumu));
      if (s->serMnutau) s->serMnutau->append(pt.T, absVal(pt.mnutau, m_useAbsMnutau));
    }
    updateChartAxes();
  });

  connect(s->worker, &SimulationWorker::simulationFinished, this, [s]() {
    if (!s->worker) return;
    s->btnRun->setEnabled(true);
    s->btnStop->setEnabled(false);
    s->statusLabel->setText("✓ Done");
  });

  connect(s->worker, &SimulationWorker::simulationError, this,
          [s](const QString &msg) {
    if (!s->worker) return;
    s->btnRun->setEnabled(true);
    s->btnStop->setEnabled(false);
    s->statusLabel->setText("✗ Error");
    s->statusLabel->setStyleSheet("color: red; font-weight: bold;");
    s->statusLabel->setToolTip(msg);
  });

  s->thread->start();
}

// ── Clear a slot ───────────────────────────────────────────────────────────
void CompareWidget::clearSlot(SlotConfig *s) {
  if (!s) return;
  if (s->thread) {
    disconnect(s->worker, nullptr, nullptr, nullptr);
    s->thread->quit();
    s->thread->wait();
    delete s->worker;
    delete s->thread;
    s->worker = nullptr;
    s->thread = nullptr;
  }
  clearSlotSeries(s);
  s->data.clear();
  s->btnRun->setEnabled(true);
  s->statusLabel->setText("–");
  s->statusLabel->setStyleSheet("color: gray;");
  updateSlotSeriesVisibility(s);
  updateChartAxes();
}

void CompareWidget::clearSlotSeries(SlotConfig *s) {
  if (!s) return;
  if (s->sernB)    s->sernB->clear();
  if (s->serS)     s->serS->clear();
  if (s->sernQ)    s->sernQ->clear();
  if (s->serMuB)   s->serMuB->clear();
  if (s->serMuQ)   s->serMuQ->clear();
  if (s->serMunue)  s->serMunue->clear();
  if (s->serMunumu) s->serMunumu->clear();
  if (s->serMnutau) s->serMnutau->clear();
}

// ── Axis range across all slots ────────────────────────────────────────────
void CompareWidget::updateChartAxes() {
  double minT = 1e99, maxT = -1e99;
  double minDens = 1e99, maxDens = -1e99;
  double minMu   = 1e99, maxMu   = -1e99;
  double minLep  = 1e99, maxLep  = -1e99;
  bool hasData   = false;

  auto val = [this](double x) { 
    if (m_isLogScale) return std::max(std::abs(x), 1e-15);
    return x;
  };

  for (auto *slot : m_slots) {
    if (!slot) continue;
    for (const auto &p : slot->data) {
      hasData = true;
      minT = std::min(minT, p.T);   maxT = std::max(maxT, p.T);
      minDens = std::min({minDens, val(p.nB), val(p.s), val(p.nQ)});
      maxDens = std::max({maxDens, val(p.nB), val(p.s), val(p.nQ)});
      minMu = std::min({minMu, val(p.muB), val(p.muQ)});
      maxMu = std::max({maxMu, val(p.muB), val(p.muQ)});
      minLep = std::min({minLep, val(p.munue), val(p.munumu), val(p.mnutau)});
      maxLep = std::max({maxLep, val(p.munue), val(p.munumu), val(p.mnutau)});
    }
  }

  if (!hasData) return;
  if (minT >= maxT) maxT = minT + 1.0;

  auto setRange = [this](QAbstractAxis *axis, double min, double max, bool isQuantity) {
    if (m_isLogScale) {
        axis->setRange(min * 0.5, max * 2.0);
    } else {
        if (isQuantity) {
            axis->setRange(std::min(0.0, min * 1.1), max * 1.1);
        } else {
            axis->setRange(min * 0.9, max * 1.1);
        }
    }
    if (m_isLogScale) static_cast<QLogValueAxis*>(axis)->setLabelFormat("%g");
    else static_cast<QValueAxis*>(axis)->setLabelFormat("%g");
  };

  if (m_tempIsVertical) {
    if (m_densAxisY) setRange(m_densAxisY, minT, maxT, false);
    if (m_muAxisY)   setRange(m_muAxisY,   minT, maxT, false);
    if (m_lepAxisY)  setRange(m_lepAxisY,  minT, maxT, false);
    if (m_densAxisX) setRange(m_densAxisX, minDens, maxDens, true);
    if (m_muAxisX)   setRange(m_muAxisX,   minMu,   maxMu,   true);
    if (m_lepAxisX)  setRange(m_lepAxisX,  minLep,  maxLep,  true);
  } else {
    if (m_densAxisX) setRange(m_densAxisX, minT, maxT, false);
    if (m_muAxisX)   setRange(m_muAxisX,   minT, maxT, false);
    if (m_lepAxisX)  setRange(m_lepAxisX,  minT, maxT, false);
    if (m_densAxisY) setRange(m_densAxisY, minDens, maxDens, true);
    if (m_muAxisY)   setRange(m_muAxisY,   minMu,   maxMu,   true);
    if (m_lepAxisY)  setRange(m_lepAxisY,  minLep,  maxLep,  true);
  }
}

void CompareWidget::onLogMessage(const QString &msg, SlotConfig *s) {
    if (!s) return;
    const int idx = slotIndex(s);
    QString colorName = s->color.name();
    QString prefix = QString("<b style=\"color:%1;\">[Slot %2] </b>")
                         .arg(colorName).arg(idx + 1);

    if (msg.contains("<font ") || msg.contains("<span ")) {
        m_console->append(prefix + msg);
    } else {
        m_console->append(prefix + "<span style=\"color:#ffffff;\">" + msg + "</span>");
    }
}

void CompareWidget::onThemeToggleClicked() {
  QChart::ChartTheme currentTheme = m_densChartView->chart()->theme();
  QChart::ChartTheme newTheme = (currentTheme == QChart::ChartThemeLight) ? QChart::ChartThemeDark : QChart::ChartThemeLight;
  
  m_densChartView->chart()->setTheme(newTheme);
  m_muChartView->chart()->setTheme(newTheme);
  m_lepChartView->chart()->setTheme(newTheme);

  static const Qt::PenStyle QTY_STYLES[3] = {Qt::SolidLine, Qt::DashLine, Qt::DotLine};
  
  for (auto *s : m_slots) {
    if (!s) continue;
    auto restore = [&](QLineSeries *ser, Qt::PenStyle ps) {
      if (!ser) return;
      QPen pen(s->color);
      pen.setStyle(ps);
      pen.setWidthF(2.0);
      ser->setPen(pen);
    };
    restore(s->sernB,    QTY_STYLES[0]);
    restore(s->serS,     QTY_STYLES[1]);
    restore(s->sernQ,    QTY_STYLES[2]);
    restore(s->serMuB,   QTY_STYLES[0]);
    restore(s->serMuQ,   QTY_STYLES[1]);
    restore(s->serMunue,  QTY_STYLES[0]);
    restore(s->serMunumu, QTY_STYLES[1]);
    restore(s->serMnutau, QTY_STYLES[2]);
  }
}

void CompareWidget::onAxisToggleClicked() {
  m_tempIsVertical = !m_tempIsVertical;
  replotData();
  updateCriticalPoint();
}

void CompareWidget::replotData() {
  auto updateTitles = [&](QAbstractAxis* axisX, QAbstractAxis* axisY, const QString& valLabel) {
     axisX->setTitleText(m_tempIsVertical ? valLabel : "Temperature [MeV]");
     axisY->setTitleText(m_tempIsVertical ? "Temperature [MeV]" : valLabel);
  };
  
  updateTitles(m_densAxisX, m_densAxisY, "Densities [MeV³]");
  updateTitles(m_muAxisX, m_muAxisY, "Chem. Pot. [MeV]");
  updateTitles(m_lepAxisX, m_lepAxisY, "Chem. Pot. [MeV]");

  for (auto *sl : m_slots) {
    clearSlotSeries(sl);

    for (const auto &pt : sl->data) {
      if (m_tempIsVertical) {
        if(sl->sernB) sl->sernB->append(absVal(pt.nB, m_useAbsnB), pt.T);
        if(sl->serS) sl->serS->append(absVal(pt.s, m_useAbsS),    pt.T);
        if(sl->sernQ) sl->sernQ->append(absVal(pt.nQ, m_useAbsnQ), pt.T);
        if(sl->serMuB) sl->serMuB->append(absVal(pt.muB, m_useAbsMuB), pt.T);
        if(sl->serMuQ) sl->serMuQ->append(absVal(pt.muQ, m_useAbsMuQ), pt.T);
        if(sl->serMunue) sl->serMunue->append(absVal(pt.munue, m_useAbsMunue), pt.T);
        if(sl->serMunumu) sl->serMunumu->append(absVal(pt.munumu, m_useAbsMunumu), pt.T);
        if(sl->serMnutau) sl->serMnutau->append(absVal(pt.mnutau, m_useAbsMnutau), pt.T);
      } else {
        if(sl->sernB) sl->sernB->append(pt.T, absVal(pt.nB, m_useAbsnB));
        if(sl->serS) sl->serS->append(pt.T, absVal(pt.s, m_useAbsS));
        if(sl->sernQ) sl->sernQ->append(pt.T, absVal(pt.nQ, m_useAbsnQ));
        if(sl->serMuB) sl->serMuB->append(pt.T, absVal(pt.muB, m_useAbsMuB));
        if(sl->serMuQ) sl->serMuQ->append(pt.T, absVal(pt.muQ, m_useAbsMuQ));
        if(sl->serMunue) sl->serMunue->append(pt.T, absVal(pt.munue, m_useAbsMunue));
        if(sl->serMunumu) sl->serMunumu->append(pt.T, absVal(pt.munumu, m_useAbsMunumu));
        if(sl->serMnutau) sl->serMnutau->append(pt.T, absVal(pt.mnutau, m_useAbsMnutau));
      }
    }
    updateSlotSeriesVisibility(sl);
  }
  refreshSeriesNames();
  refreshLegendVisibility();
  updateChartAxes();
  updateCriticalPoint();
}

void CompareWidget::onScaleToggleClicked() {
    m_isLogScale = !m_isLogScale;

    auto swapAxes = [this](QChartView *view, QAbstractAxis* &axisX, QAbstractAxis* &axisY, const QString& valLabel) {
        QChart *chart = view->chart();
        chart->removeAxis(axisX);
        chart->removeAxis(axisY);
        delete axisX;
        delete axisY;

        if (m_isLogScale) {
            axisX = new QLogValueAxis();
            static_cast<QLogValueAxis*>(axisX)->setBase(10.0);
            axisY = new QLogValueAxis();
            static_cast<QLogValueAxis*>(axisY)->setBase(10.0);
        } else {
            axisX = new QValueAxis();
            axisY = new QValueAxis();
        }

        axisX->setTitleText(m_tempIsVertical ? valLabel : "Temperature [MeV]");
        chart->addAxis(axisX, Qt::AlignBottom);

        axisY->setTitleText(m_tempIsVertical ? "Temperature [MeV]" : valLabel);
        chart->addAxis(axisY, Qt::AlignLeft);
    };

    swapAxes(m_densChartView, m_densAxisX, m_densAxisY, "Densities [MeV³]");
    swapAxes(m_muChartView,   m_muAxisX,   m_muAxisY,   "Chem. Pot. [MeV]");
    swapAxes(m_lepChartView,  m_lepAxisX,  m_lepAxisY,  "Chem. Pot. [MeV]");

    for (auto *s : m_slots) {
        if (s->sernB) { s->sernB->attachAxis(m_densAxisX); s->sernB->attachAxis(m_densAxisY); }
        if (s->serS) { s->serS->attachAxis(m_densAxisX); s->serS->attachAxis(m_densAxisY); }
        if (s->sernQ) { s->sernQ->attachAxis(m_densAxisX); s->sernQ->attachAxis(m_densAxisY); }

        if (s->serMuB) { s->serMuB->attachAxis(m_muAxisX); s->serMuB->attachAxis(m_muAxisY); }
        if (s->serMuQ) { s->serMuQ->attachAxis(m_muAxisX); s->serMuQ->attachAxis(m_muAxisY); }

        if (s->serMunue) { s->serMunue->attachAxis(m_lepAxisX); s->serMunue->attachAxis(m_lepAxisY); }
        if (s->serMunumu) { s->serMunumu->attachAxis(m_lepAxisX); s->serMunumu->attachAxis(m_lepAxisY); }
        if (s->serMnutau) { s->serMnutau->attachAxis(m_lepAxisX); s->serMnutau->attachAxis(m_lepAxisY); }
    }

    bool hasAnyData = false;
    for (auto *s : m_slots) if (s && !s->data.isEmpty()) { hasAnyData = true; break; }

    if (hasAnyData) {
        replotData();
    } else {
        refreshSeriesNames();
        updateChartAxes();
    }

    // Sync abs-checkbox enabled state with the new scale
    auto syncAbs = [this](QCheckBox *c){ if (c) c->setEnabled(!m_isLogScale); };
    syncAbs(m_absnB);  syncAbs(m_absS);   syncAbs(m_absnQ);
    syncAbs(m_absMuB); syncAbs(m_absMuQ);
    syncAbs(m_absMunue); syncAbs(m_absMunumu); syncAbs(m_absMnutau);

    m_seriesCpB->attachAxis(m_muAxisX); m_seriesCpB->attachAxis(m_muAxisY);
    m_seriesCpQ->attachAxis(m_muAxisX); m_seriesCpQ->attachAxis(m_muAxisY);
    updateCriticalPoint();
    applyAxisFonts();
}

void CompareWidget::onExportClicked() {
  bool hasAnyData = false;
  for (auto *s : m_slots) {
    if (s && !s->data.isEmpty()) { hasAnyData = true; break; }
  }

  if (!hasAnyData) {
    QMessageBox::warning(this, "No Data", "There is no trajectory data in any slot to export.");
    return;
  }

  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Export Chart");
  msgBox.setText("Select export format:");
  QPushButton *btnPdf = msgBox.addButton("Export as PDF", QMessageBox::ActionRole);
  QPushButton *btnTxt = msgBox.addButton("Export as TXT", QMessageBox::ActionRole);
  msgBox.addButton(QMessageBox::Cancel);
  msgBox.exec();

  int currentTab = m_chartTabs->currentIndex();

  if (msgBox.clickedButton() == btnPdf) {
    QString fileName = QFileDialog::getSaveFileName(this, "Save PDF", QDir::currentPath(), "PDF Files (*.pdf)");
    if (fileName.isEmpty()) return;

    QChartView *activeView = nullptr;
    if (currentTab == 0) activeView = m_densChartView;
    else if (currentTab == 1) activeView = m_muChartView;
    else if (currentTab == 2) activeView = m_lepChartView;

    if (!activeView) return;

    QPdfWriter writer(fileName);
    writer.setCreator("Cosmic Trajectories");
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageOrientation(QPageLayout::Landscape);
    
    QPainter painter(&writer);
    activeView->render(&painter);
    painter.end();
    
    QMessageBox::information(this, "Success", "Comparative chart successfully exported to PDF.");
  } 
  else if (msgBox.clickedButton() == btnTxt) {
    QString fileName = QFileDialog::getSaveFileName(this, "Save TXT", QDir::currentPath(), "Text Files (*.txt)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
      QMessageBox::critical(this, "Error", "Could not open file for writing.");
      return;
    }

    QTextStream out(&file);
    // Canonical Single-Run format. Slots are written sequentially, separated
    // by a "# Slot N ..." comment line so the trajectories can be told apart.
    out << "T\tmuB\tmuQ\tmunue\tmunumu\tmnutau\tnB\tnQ\ts_QCD\ts_tot"
           "\tne\tnmu\tntau\tnnue\tnnumu\tnnutau\n";

    for (int i = 0; i < m_slots.size(); ++i) {
      const auto *s = m_slots[i];
      if (!s || s->data.isEmpty()) continue;
      out << QString("# Slot S%1").arg(i + 1);
      if (s->runParamsValid) {
        out << QString(" (B=%1, Le=%2, Lmu=%3, Ltau=%4)")
                   .arg(s->B).arg(s->Le).arg(s->Lmu).arg(s->Ltau);
      }
      out << "\n";
      for (const auto &pt : s->data) {
        out << pt.T << "\t" << pt.muB << "\t" << pt.muQ
            << "\t" << pt.munue << "\t" << pt.munumu << "\t" << pt.mnutau
            << "\t" << pt.nB << "\t" << pt.nQ << "\t" << pt.s_QCD << "\t" << pt.s
            << "\t" << pt.ne << "\t" << pt.nmu << "\t" << pt.ntau
            << "\t" << pt.nnue << "\t" << pt.nnumu << "\t" << pt.nnutau
            << "\n";
      }
    }

    file.close();
    QMessageBox::information(this, "Success", "Comparative data exported in Single Run format.");
  }
}

void CompareWidget::onCriticalPointButtonClicked() {
  if (!m_cpDialog) {
    m_cpDialog = new QDialog(this);
    m_cpDialog->setWindowTitle("Configure Critical Point");
    m_cpDialog->setMinimumWidth(300);

    QVBoxLayout *vbox = new QVBoxLayout(m_cpDialog);
    QGridLayout *grid = new QGridLayout();

    m_spinCpT = new QDoubleSpinBox(m_cpDialog); m_spinCpT->setRange(0, 10000); m_spinCpT->setValue(120); m_spinCpT->setDecimals(1);
    m_spinCpMuB = new QDoubleSpinBox(m_cpDialog); m_spinCpMuB->setRange(0, 10000); m_spinCpMuB->setValue(600); m_spinCpMuB->setDecimals(1);
    m_spinCpMuQ = new QDoubleSpinBox(m_cpDialog); m_spinCpMuQ->setRange(-10000, 10000); m_spinCpMuQ->setValue(0); m_spinCpMuQ->setDecimals(1);
    
    grid->addWidget(new QLabel("Temperature T [MeV]:"), 0, 0); grid->addWidget(m_spinCpT, 0, 1);
    grid->addWidget(new QLabel("Baryon Pot. |μB| [MeV]:"), 1, 0); grid->addWidget(m_spinCpMuB, 1, 1);
    grid->addWidget(new QLabel("Charge Pot. |μQ| [MeV]:"), 2, 0); grid->addWidget(m_spinCpMuQ, 2, 1);

    m_chkShowCp = new QCheckBox("Show on Plot", m_cpDialog);
    m_chkShowCp->setChecked(false);
    
    vbox->addLayout(grid);
    vbox->addWidget(m_chkShowCp);

    QPushButton *btnClose = new QPushButton("Close", m_cpDialog);
    connect(btnClose, &QPushButton::clicked, m_cpDialog, &QDialog::accept);
    vbox->addWidget(btnClose);

    auto cpUpdate = [this](double) { updateCriticalPoint(); };
    connect(m_spinCpT, &QDoubleSpinBox::valueChanged, this, cpUpdate);
    connect(m_spinCpMuB, &QDoubleSpinBox::valueChanged, this, cpUpdate);
    connect(m_spinCpMuQ, &QDoubleSpinBox::valueChanged, this, cpUpdate);
    connect(m_chkShowCp, &QCheckBox::toggled, this, &CompareWidget::updateCriticalPoint);
  }
  m_cpDialog->show();
  m_cpDialog->raise();
  m_cpDialog->activateWindow();
}

void CompareWidget::updateCriticalPoint() {
  if (!m_seriesCpB || !m_seriesCpQ) return;
  
  m_seriesCpB->clear();
  m_seriesCpQ->clear();
  
  if (m_chkShowCp && m_chkShowCp->isChecked()) {
    double t = m_spinCpT->value();
    double mub = std::abs(m_spinCpMuB->value());
    double muq = std::abs(m_spinCpMuQ->value());

    auto val = [this](double v) {
      if (m_isLogScale) return std::max(std::abs(v), 1e-15);
      return v;
    };

    if (m_tempIsVertical) {
      m_seriesCpB->append(val(mub), t);
      m_seriesCpQ->append(val(muq), t);
    } else {
      m_seriesCpB->append(t, val(mub));
      m_seriesCpQ->append(t, val(muq));
    }
    
    m_seriesCpB->setVisible(true);
    m_seriesCpQ->setVisible(true);
  } else {
    m_seriesCpB->setVisible(false);
    m_seriesCpQ->setVisible(false);
  }
}

void CompareWidget::onSolverSettingsButtonClicked() {
  if (!m_solverSettingsDialog) {
    m_solverSettingsDialog = new QDialog(this);
    m_solverSettingsDialog->setWindowTitle("Solver Settings");
    m_solverSettingsDialog->setMinimumWidth(720);

    QVBoxLayout *vbox = new QVBoxLayout(m_solverSettingsDialog);
    QHBoxLayout *cols = new QHBoxLayout();
    QVBoxLayout *colLeft  = new QVBoxLayout();
    QVBoxLayout *colRight = new QVBoxLayout();
    cols->addLayout(colLeft);
    cols->addLayout(colRight);
    vbox->addLayout(cols);

    // ── Convergence ───────────────────────────────────────────────────
    QGroupBox *groupConv = new QGroupBox("Convergence", m_solverSettingsDialog);
    QGridLayout *gridConv = new QGridLayout(groupConv);

    gridConv->addWidget(new QLabel("Absolute Tolerance:"), 0, 0);
    QDoubleSpinBox *spinTol = new QDoubleSpinBox(m_solverSettingsDialog);
    spinTol->setDecimals(12);
    spinTol->setRange(1e-12, 1.0);
    spinTol->setValue(m_tolerance);
    spinTol->setSingleStep(1e-6);
    gridConv->addWidget(spinTol, 0, 1);

    gridConv->addWidget(new QLabel("Max Iterations:"), 1, 0);
    QSpinBox *spinMaxIter = new QSpinBox(m_solverSettingsDialog);
    spinMaxIter->setRange(10, 10000);
    spinMaxIter->setValue(m_maxIter);
    gridConv->addWidget(spinMaxIter, 1, 1);
    
    gridConv->addWidget(new QLabel("Lattice QCD Interpolation:"), 2, 0);
    QComboBox *comboLatticeInterp = new QComboBox(m_solverSettingsDialog);
    comboLatticeInterp->addItems({"Cubic Spline (Default)", "Linear", "Akima", "Steffen"});
    comboLatticeInterp->setCurrentIndex(m_latticeInterpType);
    gridConv->addWidget(comboLatticeInterp, 2, 1);

    colLeft->addWidget(groupConv);

    // ── Guess ─────────────────────────────────────────────────────────
    QGroupBox *groupGuess = new QGroupBox("Initial Guess Strategy", m_solverSettingsDialog);
    QVBoxLayout *vboxGuess = new QVBoxLayout(groupGuess);

    QComboBox *comboType = new QComboBox(m_solverSettingsDialog);
    comboType->addItems({"Standard Guess", "Custom Guess"});
    comboType->setCurrentIndex(m_initialGuessType);
    vboxGuess->addWidget(new QLabel("Initial Guess Values:"));
    vboxGuess->addWidget(comboType);

    auto addSpin = [&](QGridLayout* grid, int row, const QString& label, double val) -> QDoubleSpinBox* {
      grid->addWidget(new QLabel(label), row, 0);
      QDoubleSpinBox *spin = new QDoubleSpinBox(m_solverSettingsDialog);
      spin->setRange(-10000, 10000);
      spin->setDecimals(5);
      spin->setValue(val);
      grid->addWidget(spin, row, 1);
      return spin;
    };

    QGroupBox *groupLH = new QGroupBox("Low → High Scan", m_solverSettingsDialog);
    QGridLayout *gridLH = new QGridLayout(groupLH);
    QDoubleSpinBox *lh_muB    = addSpin(gridLH, 0, "muB:",    m_customGuessLowHigh[0]);
    QDoubleSpinBox *lh_muQ    = addSpin(gridLH, 1, "muQ:",    m_customGuessLowHigh[1]);
    QDoubleSpinBox *lh_munue  = addSpin(gridLH, 2, "munue:",  m_customGuessLowHigh[2]);
    QDoubleSpinBox *lh_munumu = addSpin(gridLH, 3, "munumu:", m_customGuessLowHigh[3]);
    QDoubleSpinBox *lh_mnutau = addSpin(gridLH, 4, "mnutau:", m_customGuessLowHigh[4]);
    vboxGuess->addWidget(groupLH);

    QGroupBox *groupHL = new QGroupBox("High → Low Scan", m_solverSettingsDialog);
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

    // ── Metropolis Pre-Optimizer ──────────────────────────────────────
    QGroupBox *groupMetro = new QGroupBox("Metropolis Pre-Optimizer", m_solverSettingsDialog);
    QGridLayout *gridMetro = new QGridLayout(groupMetro);

    QComboBox *comboMetroMode = new QComboBox(m_solverSettingsDialog);
    comboMetroMode->addItems({"Off", "First step only (on failure)", "Always retry on failure"});
    comboMetroMode->setCurrentIndex(m_metropolisMode);
    gridMetro->addWidget(new QLabel("Mode:"), 0, 0);
    gridMetro->addWidget(comboMetroMode, 0, 1);

    QSpinBox *spinMetroSteps = new QSpinBox(m_solverSettingsDialog);
    spinMetroSteps->setRange(10, 50000);
    spinMetroSteps->setValue(m_metropolisSteps);
    gridMetro->addWidget(new QLabel("Steps:"), 1, 0);
    gridMetro->addWidget(spinMetroSteps, 1, 1);

    QDoubleSpinBox *spinMetroSigma = new QDoubleSpinBox(m_solverSettingsDialog);
    spinMetroSigma->setDecimals(4);
    spinMetroSigma->setRange(1e-4, 1000.0);
    spinMetroSigma->setValue(m_metropolisStepSigma);
    gridMetro->addWidget(new QLabel("Step σ (MeV):"), 2, 0);
    gridMetro->addWidget(spinMetroSigma, 2, 1);

    QDoubleSpinBox *spinMetroT = new QDoubleSpinBox(m_solverSettingsDialog);
    spinMetroT->setDecimals(6);
    spinMetroT->setRange(1e-8, 1e6);
    spinMetroT->setValue(m_metropolisT);
    gridMetro->addWidget(new QLabel("Temperature T_m:"), 3, 0);
    gridMetro->addWidget(spinMetroT, 3, 1);

    QSpinBox *spinMetroRetries = new QSpinBox(m_solverSettingsDialog);
    spinMetroRetries->setRange(1, 50);
    spinMetroRetries->setValue(m_metropolisRetries);
    spinMetroRetries->setToolTip(
        "Number of independent Metropolis chains before giving up on a "
        "failing step. Each retry also gets maxIter × N solver iterations.");
    gridMetro->addWidget(new QLabel("Retries:"), 4, 0);
    gridMetro->addWidget(spinMetroRetries, 4, 1);

    // Grey out controls when mode is Off
    auto updateMetroEnabled = [=](int idx) {
      bool on = (idx != 0);
      spinMetroSteps->setEnabled(on);
      spinMetroSigma->setEnabled(on);
      spinMetroT->setEnabled(on);
      spinMetroRetries->setEnabled(on);
    };
    connect(comboMetroMode, &QComboBox::currentIndexChanged, updateMetroEnabled);
    updateMetroEnabled(m_metropolisMode);

    colLeft->addWidget(groupMetro);
    colLeft->addStretch();

    // ── Save ──────────────────────────────────────────────────────────
    QPushButton *btnSave = new QPushButton("Save and Close", m_solverSettingsDialog);
    connect(btnSave, &QPushButton::clicked, [=]() {
      m_tolerance        = spinTol->value();
      m_maxIter          = spinMaxIter->value();

      m_initialGuessType = comboType->currentIndex();
      m_customGuessLowHigh = {lh_muB->value(), lh_muQ->value(), lh_munue->value(), lh_munumu->value(), lh_mnutau->value()};
      m_customGuessHighLow = {hl_muB->value(), hl_muQ->value(), hl_munue->value(), hl_munumu->value(), hl_mnutau->value()};
      
      m_metropolisMode      = comboMetroMode->currentIndex();
      m_metropolisSteps     = spinMetroSteps->value();
      m_metropolisStepSigma = spinMetroSigma->value();
      m_metropolisT         = spinMetroT->value();
      m_metropolisRetries   = spinMetroRetries->value();
      m_latticeInterpType   = comboLatticeInterp->currentIndex();

      m_solverSettingsDialog->accept();
    });
    vbox->addWidget(btnSave);
  }
  m_solverSettingsDialog->show();
  m_solverSettingsDialog->raise();
  m_solverSettingsDialog->activateWindow();
}

// ── Per-quantity abs / legend helpers ─────────────────────────────────────
double CompareWidget::absVal(double v, bool useAbs) const {
  if (m_isLogScale) return std::max(std::abs(v), 1e-15);
  return useAbs ? std::abs(v) : v;
}

QString CompareWidget::legendSuffix(const SlotConfig &s) const {
  if (!s.runParamsValid) return QString();
  QStringList parts;
  if (m_legendShowB)    parts << QString("B=%1").arg(s.B,    0, 'g', 4);
  if (m_legendShowLe)   parts << QString("Le=%1").arg(s.Le,  0, 'g', 4);
  if (m_legendShowLmu)  parts << QString("Lμ=%1").arg(s.Lmu, 0, 'g', 4);
  if (m_legendShowLtau) parts << QString("Lτ=%1").arg(s.Ltau,0, 'g', 4);
  if (parts.isEmpty()) return QString();
  return QString(" (%1)").arg(parts.join(", "));
}

void CompareWidget::refreshSeriesNames() {
  auto fmtName = [](bool useAbs, const QString &base) {
    return useAbs ? QString("|%1|").arg(base) : base;
  };
  for (int i = 0; i < m_slots.size(); ++i) {
    auto *s = m_slots[i];
    if (!s) continue;
    const QString prefix = QString("S%1: ").arg(i + 1);
    const QString sfx = legendSuffix(*s);
    const bool absForceLog = m_isLogScale; // log forces abs display
    auto absnB     = absForceLog ? true : m_useAbsnB;
    auto absS      = absForceLog ? true : m_useAbsS;
    auto absnQ     = absForceLog ? true : m_useAbsnQ;
    auto absMuB    = absForceLog ? true : m_useAbsMuB;
    auto absMuQ    = absForceLog ? true : m_useAbsMuQ;
    auto absMunue  = absForceLog ? true : m_useAbsMunue;
    auto absMunumu = absForceLog ? true : m_useAbsMunumu;
    auto absMnutau = absForceLog ? true : m_useAbsMnutau;
    if (s->sernB)    s->sernB->setName(prefix + fmtName(absnB,    "nB")     + sfx);
    if (s->serS)     s->serS->setName( prefix + fmtName(absS,     "s")      + sfx);
    if (s->sernQ)    s->sernQ->setName(prefix + fmtName(absnQ,    "nQ") + sfx);
    if (s->serMuB)   s->serMuB->setName(prefix + fmtName(absMuB,  "μB")     + sfx);
    if (s->serMuQ)   s->serMuQ->setName(prefix + fmtName(absMuQ,  "μQ")     + sfx);
    if (s->serMunue) s->serMunue->setName(prefix + fmtName(absMunue,  "μνe") + sfx);
    if (s->serMunumu)s->serMunumu->setName(prefix + fmtName(absMunumu, "μνμ") + sfx);
    if (s->serMnutau)s->serMnutau->setName(prefix + fmtName(absMnutau, "μντ") + sfx);
  }
}

void CompareWidget::refreshLegendVisibility() {
  TooltipChartView *views[] = {m_densChartView, m_muChartView, m_lepChartView};
  for (auto *v : views) {
    if (v && v->chart()) v->chart()->legend()->setVisible(m_legendVisible);
  }
}

void CompareWidget::onConfigureAxisFontsClicked() {
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

void CompareWidget::applyAxisFonts() {
    if (!m_axisFontValid) return;
    QAbstractAxis* axes[] = {
        m_densAxisX, m_densAxisY,
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
