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
#include <QFileDialog>
#include <QPdfWriter>
#include <QPainter>
#include <QTextStream>
#include <QLineEdit>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QLogValueAxis>
#include <QtCharts/QValueAxis>
#include "TooltipChartView.h"

#include <cmath>
#include <algorithm>

// ── 5 distinct colors ──────────────────────────────────────────────────────
static const QColor SLOT_COLORS[NUM_SLOTS] = {
  QColor(31,  119, 180),   // Blue
  QColor(214,  39,  40),   // Red
  QColor( 44, 160,  44),   // Green
  QColor(255, 127,  14),   // Orange
  QColor(148, 103, 189),   // Purple
};

// ── Construction ───────────────────────────────────────────────────────────
CompareWidget::CompareWidget(const QString &workingDir, QWidget *parent)
    : QWidget(parent), m_workingDir(workingDir)
{
  for (int i = 0; i < NUM_SLOTS; i++)
    m_slots[i].color = SLOT_COLORS[i];
  setupUi();
}

CompareWidget::~CompareWidget() {
  for (int i = 0; i < NUM_SLOTS; i++) {
    if (m_slots[i].thread) {
      m_slots[i].thread->quit();
      m_slots[i].thread->wait();
      delete m_slots[i].worker;
      delete m_slots[i].thread;
    }
  }
}

void CompareWidget::updateSlotSeriesVisibility(int idx) {
  auto &s = m_slots[idx];
  bool hasData = !s.data.isEmpty();
  if (s.sernB)     s.sernB->setVisible(hasData && m_chknB->isChecked());
  if (s.serS)      s.serS->setVisible(hasData && m_chkS->isChecked());
  if (s.sernQ)     s.sernQ->setVisible(hasData && m_chknQ->isChecked());
  if (s.serMuB)    s.serMuB->setVisible(hasData && m_chkMuB->isChecked());
  if (s.serMuQ)    s.serMuQ->setVisible(hasData && m_chkMuQ->isChecked());
  if (s.serMunue)  s.serMunue->setVisible(hasData && m_chkMunue->isChecked());
  if (s.serMunumu) s.serMunumu->setVisible(hasData && m_chkMunumu->isChecked());
  if (s.serMnutau) s.serMnutau->setVisible(hasData && m_chkMnutau->isChecked());
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
  
  m_btnSolverSettings = new QPushButton("⚙ Solver Settings...");
  m_btnSolverSettings->setStyleSheet("QPushButton { background-color: #e83e8c; color: white; border-radius: 4px; font-weight: bold; padding: 4px; margin: 2px 4px 2px 4px; } "
                                      "QPushButton:hover { background-color: #d63384; }");
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
  m_console->setStyleSheet("background-color: #1e1e1e; color: #ffffff; border: 1px solid #333;");
  m_console->setMinimumHeight(150);
  consoleLayout->addWidget(m_console);
  leftPanelLayout->addWidget(groupConsole);

  // Right half: comparison charts
  QWidget *chartsWidget = new QWidget(splitter);
  QVBoxLayout *chartsLayout = new QVBoxLayout(chartsWidget);
  chartsLayout->setContentsMargins(0, 0, 0, 0);
  createChartPanel(chartsWidget);
  chartsLayout->addWidget(m_chartTabs);
  
  QHBoxLayout *bottomRightLayout = new QHBoxLayout();
  bottomRightLayout->addStretch();

  QPushButton *btnExport = new QPushButton("📤 Export Active Plot", chartsWidget);
  connect(btnExport, &QPushButton::clicked, this, &CompareWidget::onExportClicked);
  bottomRightLayout->addWidget(btnExport);

  m_btnAxisToggle = new QPushButton("Toggle Axes", chartsWidget);
  connect(m_btnAxisToggle, &QPushButton::clicked, this, [this]{ onAxisToggleClicked(); });
  bottomRightLayout->addWidget(m_btnAxisToggle);

  m_btnThemeToggle = new QPushButton("Toggle Plot Theme", chartsWidget);
  connect(m_btnThemeToggle, &QPushButton::clicked, this, [this]{ onThemeToggleClicked(); });
  bottomRightLayout->addWidget(m_btnThemeToggle);

  m_btnScaleToggle = new QPushButton("Toggle Log/Linear", chartsWidget);
  connect(m_btnScaleToggle, &QPushButton::clicked, this, [this]{ onScaleToggleClicked(); });
  bottomRightLayout->addWidget(m_btnScaleToggle);

  m_btnCriticalPoint = new QPushButton("📍 Configure Critical Point...", chartsWidget);
  m_btnCriticalPoint->setStyleSheet("QPushButton { background-color: #6f42c1; color: white; border-radius: 4px; font-weight: bold; padding: 4px; margin: 2px 4px 2px 4px; } "
                                     "QPushButton:hover { background-color: #5a32a3; }");
  connect(m_btnCriticalPoint, &QPushButton::clicked, this, &CompareWidget::onCriticalPointButtonClicked);
  bottomRightLayout->addWidget(m_btnCriticalPoint);

  chartsLayout->addLayout(bottomRightLayout);

  // ── Series visibility bar ────────────────────────────────────────────
  QGroupBox *visBox = new QGroupBox("Show/Hide Quantities Across All Slots", chartsWidget);
  QHBoxLayout *visLayout = new QHBoxLayout(visBox);
  visLayout->setSpacing(12);

  auto makeChk = [&](const QString &label, bool checked) -> QCheckBox* {
    QCheckBox *chk = new QCheckBox(label);
    chk->setChecked(checked);
    visLayout->addWidget(chk);
    return chk;
  };

  visLayout->addWidget(new QLabel("Densities:"));
  m_chknB   = makeChk("nB",      true);
  m_chkS    = makeChk("s",       true);
  m_chknQ   = makeChk("|nQ_QCD|", true);
  visLayout->addSpacing(16);
  visLayout->addWidget(new QLabel("Chem. Pot.:"));
  m_chkMuB  = makeChk("|μB|",    true);
  m_chkMuQ  = makeChk("|μQ|",    true);
  visLayout->addSpacing(16);
  visLayout->addWidget(new QLabel("Lepton:"));
  m_chkMunue  = makeChk("|μνe|",  true);
  m_chkMunumu = makeChk("|μνμ|",  true);
  m_chkMnutau = makeChk("|μντ|",  true);
  visLayout->addStretch();

  connect(m_chknB,    &QCheckBox::toggled, [this]{ for(int i=0;i<NUM_SLOTS;i++) updateSlotSeriesVisibility(i); });
  connect(m_chkS,     &QCheckBox::toggled, [this]{ for(int i=0;i<NUM_SLOTS;i++) updateSlotSeriesVisibility(i); });
  connect(m_chknQ,    &QCheckBox::toggled, [this]{ for(int i=0;i<NUM_SLOTS;i++) updateSlotSeriesVisibility(i); });
  connect(m_chkMuB,   &QCheckBox::toggled, [this]{ for(int i=0;i<NUM_SLOTS;i++) updateSlotSeriesVisibility(i); });
  connect(m_chkMuQ,   &QCheckBox::toggled, [this]{ for(int i=0;i<NUM_SLOTS;i++) updateSlotSeriesVisibility(i); });
  connect(m_chkMunue, &QCheckBox::toggled, [this]{ for(int i=0;i<NUM_SLOTS;i++) updateSlotSeriesVisibility(i); });
  connect(m_chkMunumu,&QCheckBox::toggled, [this]{ for(int i=0;i<NUM_SLOTS;i++) updateSlotSeriesVisibility(i); });
  connect(m_chkMnutau,&QCheckBox::toggled, [this]{ for(int i=0;i<NUM_SLOTS;i++) updateSlotSeriesVisibility(i); });

  chartsLayout->addWidget(visBox);

  splitter->addWidget(leftPanel);
  splitter->addWidget(chartsWidget);
  splitter->setSizes({350, 650});
}

// ── Slot Panel ─────────────────────────────────────────────────────────────
void CompareWidget::createSlotPanel(QWidget *parent) {
  QVBoxLayout *outerLayout = new QVBoxLayout(parent);
  outerLayout->setContentsMargins(4, 4, 4, 4);
  outerLayout->setSpacing(8);

  for (int i = 0; i < NUM_SLOTS; i++) {
    auto &s = m_slots[i];

    QGroupBox *box = new QGroupBox(QString("Slot %1").arg(i + 1), parent);
    box->setCheckable(true);
    box->setChecked(i == 0); 
    box->setStyleSheet(
        QString("QGroupBox { border: 2px solid %1; border-radius: 4px; margin-top: 2ex; }"
                "QGroupBox::title { color: %1; font-weight: bold; subcontrol-origin: margin; left: 8px; }")
            .arg(s.color.name()));

    QVBoxLayout *boxLayout = new QVBoxLayout(box);
    QWidget *container = new QWidget(box); 
    QGridLayout *grid = new QGridLayout(container);
    grid->setContentsMargins(0, 0, 0, 0);

    connect(box, &QGroupBox::toggled, container, &QWidget::setVisible);
    container->setVisible(box->isChecked());

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

    s.spinB    = addSpin("b",          8.6e-11, 1e-15, 1.0,    12);
    s.spinLe   = addSpin("le",         -0.01,  -1.0,  1.0,    12);
    s.spinLmu  = addSpin("lmu",        -0.01,  -1.0,  1.0,    12);
    s.spinLtau = addSpin("ltau",       -0.01,  -1.0,  1.0,    12);
    s.spinDT   = addSpin("dT (MeV)",   1.0,     0.01, 100.0,  2);
    s.spinTmin = addSpin("Tmin (MeV)", 30.0,    0.1,  10000.0, 1);
    s.spinTmax = addSpin("Tmax (MeV)", 2000.0,  0.1,  10000.0, 1);

    grid->addWidget(new QLabel("Flavors (nf)"), row, 0);
    s.comboNf = new QComboBox();
    s.comboNf->addItems({"2", "3", "4"});
    s.comboNf->setCurrentIndex(1); 
    grid->addWidget(s.comboNf, row++, 1);

    grid->addWidget(new QLabel("EoS"), row, 0);
    s.comboEos = new QComboBox();
    s.comboEos->addItems({"Free QGP (0)", "Lattice QCD (1)", "Interpolated Table (2)", "Entropy Contour (3)"});
    s.comboEos->setCurrentIndex(1); 
    grid->addWidget(s.comboEos, row++, 1);

    s.eosPathWidget = new QWidget();
    QHBoxLayout *eosPathLayout = new QHBoxLayout(s.eosPathWidget);
    eosPathLayout->setContentsMargins(0, 0, 0, 0);
    s.lineEditEosPath = new QLineEdit();
    s.lineEditEosPath->setPlaceholderText("Path to EoS table...");
    s.btnBrowseEos = new QPushButton("Browse");
    eosPathLayout->addWidget(s.lineEditEosPath);
    eosPathLayout->addWidget(s.btnBrowseEos);

    s.labelEosPath = new QLabel("Table Path");
    grid->addWidget(s.labelEosPath, row, 0);
    grid->addWidget(s.eosPathWidget, row++, 1);

    s.labelEosPath->setVisible(false);
    s.eosPathWidget->setVisible(false);

    int currentIdx = i;
    connect(s.btnBrowseEos, &QPushButton::clicked, this, [this, currentIdx]() {
        QString file = QFileDialog::getOpenFileName(this, "Select EoS Table File", QDir::currentPath(), "Text Files (*.txt);;All Files (*)");
        if (!file.isEmpty()) {
            m_slots[currentIdx].lineEditEosPath->setText(file);
        }
    });

    connect(s.comboEos, &QComboBox::currentIndexChanged, this, [this, currentIdx](int index) {
        bool showEosPath = (index == 2);
        m_slots[currentIdx].labelEosPath->setVisible(showEosPath);
        m_slots[currentIdx].eosPathWidget->setVisible(showEosPath);
    });

    grid->addWidget(new QLabel("Guess Method"), row, 0);
    s.comboGuess = new QComboBox();
    s.comboGuess->addItems({"Constant (0)", "Linear Extrap (1)"});
    s.comboGuess->setCurrentIndex(1);
    grid->addWidget(s.comboGuess, row++, 1);

    grid->addWidget(new QLabel("Scan Direction"), row, 0);
    s.comboScan = new QComboBox();
    s.comboScan->addItems({"Low -> High (0)", "High -> Low (1)"});
    grid->addWidget(s.comboScan, row++, 1);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    
    s.btnRun = new QPushButton("▶ Run Slot");
    s.btnRun->setStyleSheet(
        QString("QPushButton { background-color: %1; color: white; border-radius: 4px; font-weight: bold; padding: 4px; }"
                "QPushButton:disabled { background-color: #aaa; }")
            .arg(s.color.name()));
    int idx = i;
    connect(s.btnRun, &QPushButton::clicked, [this, idx] { runSlot(idx); });
    btnLayout->addWidget(s.btnRun);

    s.btnStop = new QPushButton("⏹ Stop");
    s.btnStop->setEnabled(false);
    s.btnStop->setStyleSheet(
        "QPushButton { background-color: #dc3545; color: white; border-radius: 4px; font-weight: bold; padding: 4px; } "
        "QPushButton:disabled { background-color: #aaa; }");
    connect(s.btnStop, &QPushButton::clicked, [this, idx]() {
        if (m_slots[idx].worker) {
            m_slots[idx].worker->stop();
            m_slots[idx].btnStop->setEnabled(false);
        }
    });
    btnLayout->addWidget(s.btnStop);

    s.btnClear = new QPushButton("✕ Clear");
    connect(s.btnClear, &QPushButton::clicked, [this, idx] { clearSlot(idx); });
    btnLayout->addWidget(s.btnClear);

    grid->addLayout(btnLayout, row++, 0, 1, 2);

    s.statusLabel = new QLabel("Ready");
    s.statusLabel->setAlignment(Qt::AlignCenter);
    s.statusLabel->setStyleSheet("color: gray;");
    grid->addWidget(s.statusLabel, row++, 0, 1, 2);

    boxLayout->addWidget(container);
    outerLayout->addWidget(box);
  }
  outerLayout->addStretch();
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

  static const Qt::PenStyle QTY_STYLES[3] = {Qt::SolidLine, Qt::DashLine, Qt::DotLine};

  for (int i = 0; i < NUM_SLOTS; i++) {
    auto &s   = m_slots[i];
    QColor c  = s.color;
    QString n = QString("S%1").arg(i + 1);

    auto makeSeries = [&](const QString &suffix, QChart *ch,
                           QAbstractAxis *ax, QAbstractAxis *ay,
                           Qt::PenStyle ps) -> QLineSeries * {
      auto *ser = new QLineSeries();
      ser->setName(QString("%1: %2").arg(n).arg(suffix));
      QPen pen(c);
      pen.setStyle(ps);
      pen.setWidthF(2.0);
      ser->setPen(pen);
      ser->setVisible(false); 
      ch->addSeries(ser);
      ser->attachAxis(ax);
      ser->attachAxis(ay);
      return ser;
    };

    s.sernB    = makeSeries("nB",  c1, m_densAxisX, m_densAxisY, QTY_STYLES[0]);
    s.serS     = makeSeries("s",   c1, m_densAxisX, m_densAxisY, QTY_STYLES[1]);
    s.sernQ    = makeSeries("|nQ_QCD|", c1, m_densAxisX, m_densAxisY, QTY_STYLES[2]);

    s.serMuB   = makeSeries("|μB|", c2, m_muAxisX, m_muAxisY, QTY_STYLES[0]);
    s.serMuQ   = makeSeries("|μQ|", c2, m_muAxisX, m_muAxisY, QTY_STYLES[1]);

    s.serMunue  = makeSeries("|μνe|", c3, m_lepAxisX, m_lepAxisY, QTY_STYLES[0]);
    s.serMunumu = makeSeries("|μνμ|", c3, m_lepAxisX, m_lepAxisY, QTY_STYLES[1]);
    s.serMnutau = makeSeries("|μντ|", c3, m_lepAxisX, m_lepAxisY, QTY_STYLES[2]);
  }

  m_seriesCpB = new QScatterSeries(); m_seriesCpB->setName("CP |μB|"); m_seriesCpB->setMarkerShape(QScatterSeries::MarkerShapeStar); m_seriesCpB->setMarkerSize(12.0); m_seriesCpB->setColor(Qt::red); m_seriesCpB->setBorderColor(Qt::black);
  m_seriesCpQ = new QScatterSeries(); m_seriesCpQ->setName("CP |μQ|"); m_seriesCpQ->setMarkerShape(QScatterSeries::MarkerShapeStar); m_seriesCpQ->setMarkerSize(12.0); m_seriesCpQ->setColor(QColor(128, 0, 128)); m_seriesCpQ->setBorderColor(Qt::black);
  c2->addSeries(m_seriesCpB); m_seriesCpB->attachAxis(m_muAxisX); m_seriesCpB->attachAxis(m_muAxisY); m_seriesCpB->setVisible(false);
  c2->addSeries(m_seriesCpQ); m_seriesCpQ->attachAxis(m_muAxisX); m_seriesCpQ->attachAxis(m_muAxisY); m_seriesCpQ->setVisible(false);
}

// ── Run a slot ─────────────────────────────────────────────────────────────
void CompareWidget::runSlot(int idx) {
  auto &s = m_slots[idx];

  if (s.thread) {
    s.thread->quit();
    s.thread->wait();
    delete s.worker;
    delete s.thread;
    s.worker = nullptr;
    s.thread = nullptr;
  }

  clearSlotSeries(idx);
  s.data.clear();

  s.statusLabel->setText("Running…");
  s.statusLabel->setStyleSheet("color: #17a2b8; font-weight: bold;");
  s.btnRun->setEnabled(false);
  s.btnStop->setEnabled(true);

  s.thread = new QThread();
  s.worker = new SimulationWorker();

  s.worker->b             = s.spinB->value();
  s.worker->le            = s.spinLe->value();
  s.worker->lmu           = s.spinLmu->value();
  s.worker->ltau          = s.spinLtau->value();
  s.worker->dT            = s.spinDT->value();
  s.worker->Tmin          = s.spinTmin->value();
  s.worker->Tmax          = s.spinTmax->value();
  s.worker->nf            = s.comboNf->currentText().toInt();
  s.worker->eos           = s.comboEos->currentIndex();
  s.worker->scanDirection = s.comboScan->currentIndex();
  s.worker->workingDir    = m_workingDir;
  s.worker->eosTableFilePath = s.lineEditEosPath->text();

  s.worker->tolerance     = m_tolerance;
  s.worker->maxIter       = m_maxIter;
  s.worker->guessMethod   = s.comboGuess->currentIndex();

  s.worker->metropolisMode      = m_metropolisMode;
  s.worker->metropolisSteps     = m_metropolisSteps;
  s.worker->metropolisStepSigma = m_metropolisStepSigma;
  s.worker->metropolisT         = m_metropolisT;

  s.worker->moveToThread(s.thread);
  connect(s.thread, &QThread::started, s.worker, &SimulationWorker::run);

  connect(s.worker, &SimulationWorker::logMessage, this, [this, idx](const QString &msg) {
      onLogMessage(msg, idx);
  });

  connect(s.worker, &SimulationWorker::stepCompleted,
          [this, idx](TrajectoryPoint pt) {
    auto &sl = m_slots[idx];
    sl.data.append(pt);
    auto val = [this](double x) { 
        if (m_isLogScale) return std::max(std::abs(x), 1e-15);
        return x;
    };

    updateSlotSeriesVisibility(idx);

    if (m_tempIsVertical) {
      sl.sernB->append(val(pt.nB), pt.T);
      sl.serS->append(val(pt.s),   pt.T);
      sl.sernQ->append(val(pt.nQ), pt.T);
      sl.serMuB->append(val(pt.muB), pt.T);
      sl.serMuQ->append(val(pt.muQ), pt.T);
      sl.serMunue->append(val(pt.munue),   pt.T);
      sl.serMunumu->append(val(pt.munumu), pt.T);
      sl.serMnutau->append(val(pt.mnutau), pt.T);
    } else {
      sl.sernB->append(pt.T, val(pt.nB));
      sl.serS->append(pt.T, val(pt.s));
      sl.sernQ->append(pt.T, val(pt.nQ));
      sl.serMuB->append(pt.T, val(pt.muB));
      sl.serMuQ->append(pt.T, val(pt.muQ));
      sl.serMunue->append(pt.T, val(pt.munue));
      sl.serMunumu->append(pt.T, val(pt.munumu));
      sl.serMnutau->append(pt.T, val(pt.mnutau));
    }
    updateChartAxes();
  });

  connect(s.worker, &SimulationWorker::simulationFinished, [this, idx]() {
    m_slots[idx].btnRun->setEnabled(true);
    m_slots[idx].btnStop->setEnabled(false);
    m_slots[idx].statusLabel->setText("✓ Done");
  });

  connect(s.worker, &SimulationWorker::simulationError,
          [this, idx](const QString &msg) {
    m_slots[idx].btnRun->setEnabled(true);
    m_slots[idx].btnStop->setEnabled(false);
    m_slots[idx].statusLabel->setText("✗ Error");
    m_slots[idx].statusLabel->setStyleSheet("color: red; font-weight: bold;");
    m_slots[idx].statusLabel->setToolTip(msg);
  });

  s.thread->start();
}

// ── Clear a slot ───────────────────────────────────────────────────────────
void CompareWidget::clearSlot(int idx) {
  auto &s = m_slots[idx];
  if (s.thread) {
    s.thread->quit();
    s.thread->wait();
    delete s.worker;
    delete s.thread;
    s.worker = nullptr;
    s.thread = nullptr;
  }
  clearSlotSeries(idx);
  s.data.clear();
  s.btnRun->setEnabled(true);
  s.statusLabel->setText("–");
  s.statusLabel->setStyleSheet("color: gray;");
  updateSlotSeriesVisibility(idx);
  updateChartAxes();
}

void CompareWidget::clearSlotSeries(int idx) {
  auto &s = m_slots[idx];
  if (s.sernB)    s.sernB->clear();
  if (s.serS)     s.serS->clear();
  if (s.sernQ)    s.sernQ->clear();
  if (s.serMuB)   s.serMuB->clear();
  if (s.serMuQ)   s.serMuQ->clear();
  if (s.serMunue)  s.serMunue->clear();
  if (s.serMunumu) s.serMunumu->clear();
  if (s.serMnutau) s.serMnutau->clear();
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

  for (int i = 0; i < NUM_SLOTS; i++) {
    for (const auto &p : m_slots[i].data) {
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
    setRange(m_densAxisY, minT, maxT, false);
    setRange(m_muAxisY,   minT, maxT, false);
    setRange(m_lepAxisY,  minT, maxT, false);

    setRange(m_densAxisX, minDens, maxDens, true);
    setRange(m_muAxisX,   minMu,   maxMu,   true);
    setRange(m_lepAxisX,  minLep,  maxLep,  true);
  } else {
    setRange(m_densAxisX, minT, maxT, false);
    setRange(m_muAxisX,   minT, maxT, false);
    setRange(m_lepAxisX,  minT, maxT, false);

    setRange(m_densAxisY, minDens, maxDens, true);
    setRange(m_muAxisY,   minMu,   maxMu,   true);
    setRange(m_lepAxisY,  minLep,  maxLep,  true);
  }
}

void CompareWidget::onLogMessage(const QString &msg, int slotIdx) {
    QString colorName = m_slots[slotIdx].color.name();
    QString prefix = QString("<b style=\"color:%1;\">[Slot %2] </b>").arg(colorName).arg(slotIdx + 1);
    
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
  
  for (int i = 0; i < NUM_SLOTS; i++) {
    auto &s = m_slots[i];
    auto restore = [&](QLineSeries *ser, Qt::PenStyle ps) {
      if (!ser) return;
      QPen pen(s.color);
      pen.setStyle(ps);
      pen.setWidthF(2.0);
      ser->setPen(pen);
    };
    restore(s.sernB,    QTY_STYLES[0]);
    restore(s.serS,     QTY_STYLES[1]);
    restore(s.sernQ,    QTY_STYLES[2]);
    restore(s.serMuB,   QTY_STYLES[0]);
    restore(s.serMuQ,   QTY_STYLES[1]);
    restore(s.serMunue,  QTY_STYLES[0]);
    restore(s.serMunumu, QTY_STYLES[1]);
    restore(s.serMnutau, QTY_STYLES[2]);
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

  for (int i = 0; i < NUM_SLOTS; i++) {
    auto &sl = m_slots[i];
    clearSlotSeries(i);
    
    auto val = [this](double v) { 
        if (m_isLogScale) return std::max(std::abs(v), 1e-15);
        return v;
    };

    for (const auto &pt : sl.data) {
      if (m_tempIsVertical) {
        if(sl.sernB) sl.sernB->append(val(pt.nB), pt.T);
        if(sl.serS) sl.serS->append(val(pt.s),   pt.T);
        if(sl.sernQ) sl.sernQ->append(val(pt.nQ), pt.T);
        if(sl.serMuB) sl.serMuB->append(val(pt.muB), pt.T);
        if(sl.serMuQ) sl.serMuQ->append(val(pt.muQ), pt.T);
        if(sl.serMunue) sl.serMunue->append(val(pt.munue), pt.T);
        if(sl.serMunumu) sl.serMunumu->append(val(pt.munumu), pt.T);
        if(sl.serMnutau) sl.serMnutau->append(val(pt.mnutau), pt.T);
      } else {
        if(sl.sernB) sl.sernB->append(pt.T, val(pt.nB));
        if(sl.serS) sl.serS->append(pt.T, val(pt.s));
        if(sl.sernQ) sl.sernQ->append(pt.T, val(pt.nQ));
        if(sl.serMuB) sl.serMuB->append(pt.T, val(pt.muB));
        if(sl.serMuQ) sl.serMuQ->append(pt.T, val(pt.muQ));
        if(sl.serMunue) sl.serMunue->append(pt.T, val(pt.munue));
        if(sl.serMunumu) sl.serMunumu->append(pt.T, val(pt.munumu));
        if(sl.serMnutau) sl.serMnutau->append(pt.T, val(pt.mnutau));
      }
    }
    updateSlotSeriesVisibility(i);
  }
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

    for (int i = 0; i < NUM_SLOTS; i++) {
        auto &s = m_slots[i];
        if (s.sernB) { s.sernB->attachAxis(m_densAxisX); s.sernB->attachAxis(m_densAxisY); }
        if (s.serS) { s.serS->attachAxis(m_densAxisX); s.serS->attachAxis(m_densAxisY); }
        if (s.sernQ) { s.sernQ->attachAxis(m_densAxisX); s.sernQ->attachAxis(m_densAxisY); }

        if (s.serMuB) { s.serMuB->attachAxis(m_muAxisX); s.serMuB->attachAxis(m_muAxisY); }
        if (s.serMuQ) { s.serMuQ->attachAxis(m_muAxisX); s.serMuQ->attachAxis(m_muAxisY); }

        if (s.serMunue) { s.serMunue->attachAxis(m_lepAxisX); s.serMunue->attachAxis(m_lepAxisY); }
        if (s.serMunumu) { s.serMunumu->attachAxis(m_lepAxisX); s.serMunumu->attachAxis(m_lepAxisY); }
        if (s.serMnutau) { s.serMnutau->attachAxis(m_lepAxisX); s.serMnutau->attachAxis(m_lepAxisY); }
    }

    bool hasAnyData = false;
    for (int i = 0; i < NUM_SLOTS; i++) if (!m_slots[i].data.isEmpty()) { hasAnyData = true; break; }

    if (hasAnyData) {
        replotData();
    } else {
        updateChartAxes();
    }
    
    m_seriesCpB->attachAxis(m_muAxisX); m_seriesCpB->attachAxis(m_muAxisY);
    m_seriesCpQ->attachAxis(m_muAxisX); m_seriesCpQ->attachAxis(m_muAxisY);
    updateCriticalPoint();
}

void CompareWidget::onExportClicked() {
  bool hasAnyData = false;
  for (int i = 0; i < NUM_SLOTS; i++) {
    if (!m_slots[i].data.isEmpty()) {
      hasAnyData = true;
      break;
    }
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

    QVector<int> activeSlots;
    int maxRows = 0;
    for (int i = 0; i < NUM_SLOTS; i++) {
      if (!m_slots[i].data.isEmpty()) {
        activeSlots.append(i);
        maxRows = std::max(maxRows, (int)m_slots[i].data.size());
      }
    }

    QStringList headerParts;
    for (int idx : activeSlots) {
      QString s = QString("S%1").arg(idx + 1);
      if (currentTab == 0) {
        headerParts << ("T_" + s) << ("nB_" + s) << ("s_" + s) << ("|nQ_QCD|_" + s);
      } else if (currentTab == 1) {
        headerParts << ("T_" + s) << ("|muB|_" + s) << ("|muQ|_" + s);
      } else if (currentTab == 2) {
        headerParts << ("T_" + s) << ("|munue|_" + s) << ("|munumu|_" + s) << ("|mnutau|_" + s);
      }
    }
    out << headerParts.join("\t") << "\n";

    for (int row = 0; row < maxRows; row++) {
      QStringList rowParts;
      for (int idx : activeSlots) {
        const auto &data = m_slots[idx].data;
        if (row < data.size()) {
          const auto &pt = data[row];
          if (currentTab == 0) {
            rowParts << QString::number(pt.T) << QString::number(pt.nB) << QString::number(pt.s) << QString::number(pt.nQ);
          } else if (currentTab == 1) {
            rowParts << QString::number(pt.T) << QString::number(pt.muB) << QString::number(pt.muQ);
          } else if (currentTab == 2) {
            rowParts << QString::number(pt.T) << QString::number(pt.munue) << QString::number(pt.munumu) << QString::number(pt.mnutau);
          }
        } else {
          int colCount = (currentTab == 0) ? 4 : ((currentTab == 1) ? 3 : 4);
          for (int c = 0; c < colCount; c++) rowParts << "";
        }
      }
      out << rowParts.join("\t") << "\n";
    }

    file.close();
    QMessageBox::information(this, "Success", "Comparative data successfully exported to TXT (Parallel Columns).");
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
    m_solverSettingsDialog->setMinimumWidth(420);

    QVBoxLayout *vbox = new QVBoxLayout(m_solverSettingsDialog);

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

    vbox->addWidget(groupConv);

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

    vbox->addWidget(groupGuess);

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

    // Grey out controls when mode is Off
    auto updateMetroEnabled = [=](int idx) {
      bool on = (idx != 0);
      spinMetroSteps->setEnabled(on);
      spinMetroSigma->setEnabled(on);
      spinMetroT->setEnabled(on);
    };
    connect(comboMetroMode, &QComboBox::currentIndexChanged, updateMetroEnabled);
    updateMetroEnabled(m_metropolisMode);

    vbox->addWidget(groupMetro);

    // ── Save ──────────────────────────────────────────────────────────
    QPushButton *btnSave = new QPushButton("Save and Close", m_solverSettingsDialog);
    btnSave->setStyleSheet("QPushButton { background-color: #007bff; color: white; border-radius: 4px; font-weight: bold; padding: 6px; } "
                           "QPushButton:hover { background-color: #0056b3; }");
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

      m_solverSettingsDialog->accept();
    });
    vbox->addWidget(btnSave);
  }
  m_solverSettingsDialog->show();
  m_solverSettingsDialog->raise();
  m_solverSettingsDialog->activateWindow();
}
