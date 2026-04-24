#include "MainWindow.h"
#include "CompareWidget.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QTextEdit>
#include <QTabWidget>
#include <QLabel>
#include <QCheckBox>
#include <QLineEdit>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QSplitter>
#include <QDir>
#include <QPalette>
#include <QFont>
#include <QMessageBox>
#include <QFileDialog>
#include <QPdfWriter>
#include <QPainter>
#include <QTextStream>
#include <QColor>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QLogValueAxis>
#include <QtCharts/QValueAxis>
#include "TooltipChartView.h"

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  setupUi();
  setupStyle();
}

MainWindow::~MainWindow() {
  if (m_workerThread) {
    m_workerThread->quit();
    m_workerThread->wait();
  }
}

void MainWindow::setupUi() {
  QWidget *centralWidget = new QWidget(this);
  setCentralWidget(centralWidget);

  QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

  // Top-level tab widget: Single Run | Compare
  QTabWidget *topTabs = new QTabWidget(centralWidget);
  mainLayout->addWidget(topTabs);

  // ─── Tab 1: Single Run ────────────────────────────────────────────
  QWidget *singleRunWidget = new QWidget();
  QHBoxLayout *srLayout = new QHBoxLayout(singleRunWidget);
  srLayout->setContentsMargins(0, 0, 0, 0);

  QSplitter *splitter = new QSplitter(Qt::Horizontal, singleRunWidget);
  srLayout->addWidget(splitter);

  QWidget *leftPanel = new QWidget(splitter);
  QVBoxLayout *leftLayout = new QVBoxLayout(leftPanel);
  leftLayout->setContentsMargins(0, 0, 0, 0);

  createParameterPanel(leftPanel);
  leftLayout->addWidget(leftPanel->findChild<QGroupBox*>("GroupParams"));

  createConsolePanel(leftPanel);
  leftLayout->addWidget(leftPanel->findChild<QGroupBox*>("GroupConsole"));

  QWidget *rightPanel = new QWidget(splitter);
  QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  createChartPanel(rightPanel);
  rightLayout->addWidget(m_chartTabs);

  // ── Series visibility bar ────────────────────────────────────────────
  QGroupBox *visBox = new QGroupBox("Show/Hide Series", rightPanel);
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
  visLayout->addSpacing(16);
  visLayout->addWidget(new QLabel("Errors:"));
  m_chkErrB    = makeChk("err_b", true);
  m_chkErrQ    = makeChk("err_q", true);
  m_chkErrLe   = makeChk("err_le", true);
  m_chkErrLmu  = makeChk("err_lmu", true);
  m_chkErrLtau = makeChk("err_ltau", true);
  visLayout->addStretch();

  // Wire checkboxes to series visibility
  connect(m_chknB,    &QCheckBox::toggled, [this](bool v){ m_seriesnB->setVisible(v);    });
  connect(m_chkS,     &QCheckBox::toggled, [this](bool v){ m_seriesS->setVisible(v);     });
  connect(m_chknQ,    &QCheckBox::toggled, [this](bool v){ m_seriesnQ->setVisible(v);    });
  connect(m_chkMuB,   &QCheckBox::toggled, [this](bool v){ m_seriesMuB->setVisible(v);   });
  connect(m_chkMuQ,   &QCheckBox::toggled, [this](bool v){ m_seriesMuQ->setVisible(v);   });
  connect(m_chkMunue, &QCheckBox::toggled, [this](bool v){ m_seriesMunue->setVisible(v); });
  connect(m_chkMunumu,&QCheckBox::toggled, [this](bool v){ m_seriesMunumu->setVisible(v);});
  connect(m_chkMnutau,&QCheckBox::toggled, [this](bool v){ m_seriesMnutau->setVisible(v);});
  connect(m_chkErrB,   &QCheckBox::toggled, [this](bool v){ m_seriesErrB->setVisible(v);   });
  connect(m_chkErrQ,   &QCheckBox::toggled, [this](bool v){ m_seriesErrQ->setVisible(v);   });
  connect(m_chkErrLe,  &QCheckBox::toggled, [this](bool v){ m_seriesErrLe->setVisible(v);  });
  connect(m_chkErrLmu, &QCheckBox::toggled, [this](bool v){ m_seriesErrLmu->setVisible(v); });
  connect(m_chkErrLtau,&QCheckBox::toggled, [this](bool v){ m_seriesErrLtau->setVisible(v);});

  rightLayout->addWidget(visBox);

  QHBoxLayout *bottomRightLayout = new QHBoxLayout();
  bottomRightLayout->addStretch();
  
  QPushButton *btnExport = new QPushButton("📤 Export Active Plot", rightPanel);
  connect(btnExport, &QPushButton::clicked, this, &MainWindow::onExportClicked);
  bottomRightLayout->addWidget(btnExport);

  QPushButton *btnAxisToggle = new QPushButton("Toggle Axes", rightPanel);
  connect(btnAxisToggle, &QPushButton::clicked, this, &MainWindow::onAxisToggleClicked);
  bottomRightLayout->addWidget(btnAxisToggle);

  m_btnThemeToggle = new QPushButton("Toggle Plot Theme", rightPanel);
  connect(m_btnThemeToggle, &QPushButton::clicked, this, &MainWindow::onThemeToggleClicked);
  bottomRightLayout->addWidget(m_btnThemeToggle);

  m_btnScaleToggle = new QPushButton("Toggle Log/Linear", rightPanel);
  connect(m_btnScaleToggle, &QPushButton::clicked, this, &MainWindow::onScaleToggleClicked);
  bottomRightLayout->addWidget(m_btnScaleToggle);
  
  rightLayout->addLayout(bottomRightLayout);

  splitter->addWidget(leftPanel);
  splitter->addWidget(rightPanel);
  
  // Give charts more space
  splitter->setSizes({300, 700});

  topTabs->addTab(singleRunWidget, "Single Run");

  // ─── Tab 2: Compare ───────────────────────────────────────────────
  QDir wdir(QCoreApplication::applicationDirPath());
  wdir.cdUp(); // gui/
  wdir.cdUp(); // project root
  topTabs->addTab(new CompareWidget(wdir.absolutePath()), "Compare Runs");
}

void MainWindow::setupStyle() {
  setStyleSheet("QToolTip { color: #ffffff; background-color: #2a82da; border: 1px solid white; } "
                "QGroupBox { border: 1px solid gray; border-radius: 5px; margin-top: 1ex; font-weight: bold; } "
                "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 3px 0 3px; }");
}

void MainWindow::createParameterPanel(QWidget *parent) {
  QGroupBox *group = new QGroupBox("Parameters", parent);
  group->setObjectName("GroupParams");
  QVBoxLayout *layout = new QVBoxLayout(group);

  QGridLayout *grid = new QGridLayout();

  // Helper macro
  int row = 0;
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

  m_spinB    = addSpin("b", 8.6e-11, 1e-15, 1.0, 12);
  m_spinLe   = addSpin("le", -0.01, -1.0, 1.0, 12);
  m_spinLmu  = addSpin("lmu", -0.01, -1.0, 1.0, 12);
  m_spinLtau = addSpin("ltau", -0.01, -1.0, 1.0, 12);
  
  m_spinDT   = addSpin("dT (MeV)", 1.0, 0.01, 100.0, 2);
  m_spinTmin = addSpin("Tmin (MeV)", 30.0, 0.1, 10000.0, 1);
  m_spinTmax = addSpin("Tmax (MeV)", 2000.0, 0.1, 10000.0, 1);

  m_comboNf = new QComboBox();
  m_comboNf->addItems({"2", "3", "4"});
  m_comboNf->setCurrentIndex(1); // Default to 3
  grid->addWidget(new QLabel("Flavors (nf)"), row, 0);
  grid->addWidget(m_comboNf, row++, 1);

  m_comboEos = new QComboBox();
  m_comboEos->addItems({"Free QGP (0)", "Lattice QCD (1)", "Interpolated Table (2)"});
  connect(m_comboEos, &QComboBox::currentIndexChanged, this, &MainWindow::onEosChanged);
  grid->addWidget(new QLabel("EoS"), row, 0);
  grid->addWidget(m_comboEos, row++, 1);

  m_eosPathWidget = new QWidget();
  QHBoxLayout *eosPathLayout = new QHBoxLayout(m_eosPathWidget);
  eosPathLayout->setContentsMargins(0, 0, 0, 0);
  m_lineEditEosPath = new QLineEdit();
  m_lineEditEosPath->setPlaceholderText("Path to EoS table...");
  m_btnBrowseEos = new QPushButton("Browse");
  eosPathLayout->addWidget(m_lineEditEosPath);
  eosPathLayout->addWidget(m_btnBrowseEos);
  
  m_labelEosPath = new QLabel("Table Path");
  grid->addWidget(m_labelEosPath, row, 0);
  grid->addWidget(m_eosPathWidget, row++, 1);

  m_labelEosPath->setVisible(false);
  m_eosPathWidget->setVisible(false);

  connect(m_btnBrowseEos, &QPushButton::clicked, this, [this]() {
      QString file = QFileDialog::getOpenFileName(this, "Select EoS Table File", QDir::currentPath(), "Text Files (*.txt);;All Files (*)");
      if (!file.isEmpty()) {
          m_lineEditEosPath->setText(file);
      }
  });

  m_comboGuess = new QComboBox();
  m_comboGuess->addItems({"Simple (0)", "Linear Extrap (1)"});
  grid->addWidget(new QLabel("Guess Method"), row, 0);
  grid->addWidget(m_comboGuess, row++, 1);

  m_comboScan = new QComboBox();
  m_comboScan->addItems({"Low -> High (0)", "High -> Low (1)"});
  grid->addWidget(new QLabel("Scan Direction"), row, 0);
  grid->addWidget(m_comboScan, row++, 1);

  layout->addLayout(grid);

  m_btnCriticalPoint = new QPushButton("📍 Configure Critical Point...");
  m_btnCriticalPoint->setStyleSheet("QPushButton { background-color: #6f42c1; color: white; border-radius: 4px; font-weight: bold; padding: 5px; } "
                                     "QPushButton:hover { background-color: #5a32a3; }");
  connect(m_btnCriticalPoint, &QPushButton::clicked, this, &MainWindow::onCriticalPointButtonClicked);
  layout->addWidget(m_btnCriticalPoint);

  QHBoxLayout *runStopLayout = new QHBoxLayout();
  m_btnRun = new QPushButton("▶ Run Simulation");
  m_btnRun->setMinimumHeight(40);
  m_btnRun->setStyleSheet("QPushButton { background-color: #28a745; color: white; border-radius: 4px; font-weight: bold; } "
                          "QPushButton:hover { background-color: #218838; } "
                          "QPushButton:disabled { background-color: #5a6268; color: #c0c0c0; }");
  connect(m_btnRun, &QPushButton::clicked, this, &MainWindow::onRunClicked);
  
  m_btnStop = new QPushButton("⏹ Stop");
  m_btnStop->setMinimumHeight(40);
  m_btnStop->setEnabled(false);
  m_btnStop->setStyleSheet("QPushButton { background-color: #dc3545; color: white; border-radius: 4px; font-weight: bold; } "
                           "QPushButton:hover { background-color: #c82333; } "
                           "QPushButton:disabled { background-color: #5a6268; color: #c0c0c0; }");
  connect(m_btnStop, &QPushButton::clicked, this, &MainWindow::onStopClicked);

  runStopLayout->addWidget(m_btnRun);
  runStopLayout->addWidget(m_btnStop);
  layout->addLayout(runStopLayout);

  m_btnExportFullData = new QPushButton("💾 Export Full Dataset");
  m_btnExportFullData->setMinimumHeight(30);
  m_btnExportFullData->setStyleSheet("QPushButton { background-color: #17a2b8; color: white; border-radius: 4px; font-weight: bold; } "
                                     "QPushButton:hover { background-color: #138496; } "
                                     "QPushButton:disabled { background-color: #5a6268; color: #c0c0c0; }");
  connect(m_btnExportFullData, &QPushButton::clicked, this, &MainWindow::onExportFullDataClicked);
  layout->addWidget(m_btnExportFullData);
}

void MainWindow::createConsolePanel(QWidget *parent) {
  QGroupBox *group = new QGroupBox("Console", parent);
  group->setObjectName("GroupConsole");
  QVBoxLayout *layout = new QVBoxLayout(group);

  m_statusLabel = new QLabel("Ready");
  m_statusLabel->setStyleSheet("color: #17a2b8; font-weight: bold;");
  layout->addWidget(m_statusLabel);

  m_progressBar = new QProgressBar();
  m_progressBar->setValue(0);
  layout->addWidget(m_progressBar);

  m_console = new QTextEdit();
  m_console->setReadOnly(true);
  m_console->setFontFamily("Courier");
  m_console->setStyleSheet("background-color: #1e1e1e; color: #ffffff; border: 1px solid #333;");
  layout->addWidget(m_console);
}

void MainWindow::createChartPanel(QWidget *parent) {
  m_chartTabs = new QTabWidget(parent);

  auto setupChart = [&](TooltipChartView* &view, QChart* &chart, QAbstractAxis* &axisX, QAbstractAxis* &axisY, const QString& title, const QString& valLabel) {
    chart = new QChart();
    chart->setTitle(title);
    chart->setTheme(QChart::ChartThemeLight); 
    chart->setAnimationOptions(QChart::NoAnimation);
    chart->legend()->setMarkerShape(QLegend::MarkerShapeFromSeries);
    chart->legend()->setAlignment(Qt::AlignRight);
    
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

    view = new TooltipChartView(chart);
    view->setRenderHint(QPainter::Antialiasing);
    m_chartTabs->addTab(view, title);
  };

  QChart *c1, *c2, *c3, *c4;
  
  // Densities tab
  setupChart(m_densityChartView, c1, m_densAxisX, m_densAxisY, "Densities vs Temperature", "Densities [MeV^3]");
  m_seriesnB = new QLineSeries(); m_seriesnB->setName("nB"); m_seriesnB->setColor(Qt::blue);
  m_seriesS  = new QLineSeries(); m_seriesS->setName("s");   m_seriesS->setColor(Qt::green);
  m_seriesnQ = new QLineSeries(); m_seriesnQ->setName("|nQ_QCD|"); m_seriesnQ->setColor(QColor(255, 165, 0)); // Orange
  c1->addSeries(m_seriesnB); m_seriesnB->attachAxis(m_densAxisX); m_seriesnB->attachAxis(m_densAxisY);
  c1->addSeries(m_seriesS);  m_seriesS->attachAxis(m_densAxisX);  m_seriesS->attachAxis(m_densAxisY);
  c1->addSeries(m_seriesnQ); m_seriesnQ->attachAxis(m_densAxisX); m_seriesnQ->attachAxis(m_densAxisY);

  // Chem pots tab
  setupChart(m_muChartView, c2, m_muAxisX, m_muAxisY, "Baryon & Electric Chem Pot", "Chem Pot [MeV] (abs)");
  m_seriesMuB = new QLineSeries(); m_seriesMuB->setName("|μB|"); m_seriesMuB->setColor(Qt::red);
  m_seriesMuQ = new QLineSeries(); m_seriesMuQ->setName("|μQ|"); m_seriesMuQ->setColor(QColor(128, 0, 128));
  c2->addSeries(m_seriesMuB); m_seriesMuB->attachAxis(m_muAxisX); m_seriesMuB->attachAxis(m_muAxisY);
  c2->addSeries(m_seriesMuQ); m_seriesMuQ->attachAxis(m_muAxisX); m_seriesMuQ->attachAxis(m_muAxisY);

  m_seriesCpB = new QScatterSeries(); m_seriesCpB->setName("CP |μB|"); m_seriesCpB->setMarkerShape(QScatterSeries::MarkerShapeStar); m_seriesCpB->setMarkerSize(12.0); m_seriesCpB->setColor(Qt::red); m_seriesCpB->setBorderColor(Qt::black);
  m_seriesCpQ = new QScatterSeries(); m_seriesCpQ->setName("CP |μQ|"); m_seriesCpQ->setMarkerShape(QScatterSeries::MarkerShapeStar); m_seriesCpQ->setMarkerSize(12.0); m_seriesCpQ->setColor(QColor(128, 0, 128)); m_seriesCpQ->setBorderColor(Qt::black);
  c2->addSeries(m_seriesCpB); m_seriesCpB->attachAxis(m_muAxisX); m_seriesCpB->attachAxis(m_muAxisY); m_seriesCpB->setVisible(false);
  c2->addSeries(m_seriesCpQ); m_seriesCpQ->attachAxis(m_muAxisX); m_seriesCpQ->attachAxis(m_muAxisY); m_seriesCpQ->setVisible(false);

  // Lepton chem pots tab
  setupChart(m_leptonChartView, c3, m_lepAxisX, m_lepAxisY, "Lepton Chemical Potentials", "Chem Pot [MeV] (abs)");
  m_seriesMunue  = new QLineSeries(); m_seriesMunue->setName("|μνe|");  m_seriesMunue->setColor(Qt::cyan);
  m_seriesMunumu = new QLineSeries(); m_seriesMunumu->setName("|μνμ|"); m_seriesMunumu->setColor(Qt::magenta);
  m_seriesMnutau = new QLineSeries(); m_seriesMnutau->setName("|μντ|"); m_seriesMnutau->setColor(QColor(200, 200, 0));
  c3->addSeries(m_seriesMunue);  m_seriesMunue->attachAxis(m_lepAxisX);  m_seriesMunue->attachAxis(m_lepAxisY);
  c3->addSeries(m_seriesMunumu); m_seriesMunumu->attachAxis(m_lepAxisX); m_seriesMunumu->attachAxis(m_lepAxisY);
  c3->addSeries(m_seriesMnutau); m_seriesMnutau->attachAxis(m_lepAxisX); m_seriesMnutau->attachAxis(m_lepAxisY);

  // Errors tab
  setupChart(m_errorChartView, c4, m_errAxisX, m_errAxisY, "Residual Errors", "Relative Error");
  m_seriesErrB      = new QLineSeries(); m_seriesErrB->setName("err_b");        m_seriesErrB->setColor(Qt::blue);
  m_seriesErrQ      = new QLineSeries(); m_seriesErrQ->setName("err_charge");   m_seriesErrQ->setColor(QColor(255, 165, 0));
  m_seriesErrLe     = new QLineSeries(); m_seriesErrLe->setName("err_le");      m_seriesErrLe->setColor(Qt::cyan);
  m_seriesErrLmu    = new QLineSeries(); m_seriesErrLmu->setName("err_lmu");    m_seriesErrLmu->setColor(Qt::magenta);
  m_seriesErrLtau   = new QLineSeries(); m_seriesErrLtau->setName("err_ltau");   m_seriesErrLtau->setColor(QColor(200, 200, 0));
  c4->addSeries(m_seriesErrB);    m_seriesErrB->attachAxis(m_errAxisX);    m_seriesErrB->attachAxis(m_errAxisY);
  c4->addSeries(m_seriesErrQ);    m_seriesErrQ->attachAxis(m_errAxisX);    m_seriesErrQ->attachAxis(m_errAxisY);
  c4->addSeries(m_seriesErrLe);   m_seriesErrLe->attachAxis(m_errAxisX);   m_seriesErrLe->attachAxis(m_errAxisY);
  c4->addSeries(m_seriesErrLmu);  m_seriesErrLmu->attachAxis(m_errAxisX);  m_seriesErrLmu->attachAxis(m_errAxisY);
  c4->addSeries(m_seriesErrLtau); m_seriesErrLtau->attachAxis(m_errAxisX); m_seriesErrLtau->attachAxis(m_errAxisY);

  updateAxesTypes(); // Set formats

  // Fix Error plot to be horizontal by default (Temperature on X)
  if (m_tempIsVertical) {
    m_errAxisX->setTitleText("Temperature [MeV]");
    m_errAxisY->setTitleText("Relative Error");
  }
}

void MainWindow::onEosChanged(int index) {
  if (index == 2) {
    onLogMessage("Note: For Interpolated Table (EoS=2), Tmin and Tmax will be read from the table bounds.");
  }
  bool showEosPath = (index == 2);
  m_labelEosPath->setVisible(showEosPath);
  m_eosPathWidget->setVisible(showEosPath);
}

void MainWindow::clearCharts(bool keepData) {
  m_seriesnB->clear();
  m_seriesS->clear();
  m_seriesnQ->clear();
  
  m_seriesMuB->clear();
  m_seriesMuQ->clear();
  
  m_seriesMunue->clear();
  m_seriesMunumu->clear();
  m_seriesMnutau->clear();

  m_seriesErrB->clear();
  m_seriesErrQ->clear();
  m_seriesErrLe->clear();
  m_seriesErrLmu->clear();
  m_seriesErrLtau->clear();
  
  if (!keepData) {
    m_trajectoryData.clear();
  }
}

void MainWindow::onRunClicked() {
  m_btnRun->setEnabled(false);
  m_btnStop->setEnabled(true);
  m_btnExportFullData->setEnabled(false);
  m_console->clear();
  m_progressBar->setValue(0);
  m_statusLabel->setText("Running...");
  m_statusLabel->setStyleSheet("color: #ffc107; font-weight: bold;"); // Yellow
  
  clearCharts();

  if (m_workerThread) {
    m_workerThread->quit();
    m_workerThread->wait();
    delete m_worker;
    delete m_workerThread;
  }

  m_workerThread = new QThread();
  m_worker = new SimulationWorker();

  // Load parameters into worker
  m_worker->b = m_spinB->value();
  m_worker->le = m_spinLe->value();
  m_worker->lmu = m_spinLmu->value();
  m_worker->ltau = m_spinLtau->value();
  m_worker->dT = m_spinDT->value();
  m_worker->Tmin = m_spinTmin->value();
  m_worker->Tmax = m_spinTmax->value();
  m_worker->nf = m_comboNf->currentText().toInt();
  m_worker->eos = m_comboEos->currentIndex();
  m_worker->guessMethod = m_comboGuess->currentIndex();
  m_worker->scanDirection = m_comboScan->currentIndex();
  m_worker->eosTableFilePath = m_lineEditEosPath->text();
  
  // Set working directory to project root (parent of build directory typically)
  // The user launches from gui/build/, so up two levels to get to project root
  QDir dir(QCoreApplication::applicationDirPath());
  dir.cdUp(); // goes to /gui/
  dir.cdUp(); // goes to project root
  m_worker->workingDir = dir.absolutePath();

  m_worker->moveToThread(m_workerThread);

  connect(m_workerThread, &QThread::started, m_worker, &SimulationWorker::run);
  connect(m_worker, &SimulationWorker::stepCompleted, this, &MainWindow::onStepCompleted);
  connect(m_worker, &SimulationWorker::progressUpdated, this, &MainWindow::onProgressUpdated);
  connect(m_worker, &SimulationWorker::logMessage, this, &MainWindow::onLogMessage);
  connect(m_worker, &SimulationWorker::simulationFinished, this, &MainWindow::onSimulationFinished);
  connect(m_worker, &SimulationWorker::simulationError, this, &MainWindow::onSimulationError);

  m_workerThread->start();
}

void MainWindow::onStopClicked() {
  if (m_worker) {
    m_worker->stop();
    m_btnStop->setEnabled(false);
    m_statusLabel->setText("Stopping...");
  }
}

void MainWindow::onStepCompleted(TrajectoryPoint pt) {
  // Store data
  m_trajectoryData.append(pt);
  
  auto val = [this](double v) { 
    if (m_isLogScale) return std::max(std::abs(v), 1e-15);
    return v;
  };

  if (m_tempIsVertical) {
    m_seriesnB->append(val(pt.nB), pt.T);
    m_seriesS->append(val(pt.s), pt.T);
    m_seriesnQ->append(val(pt.nQ), pt.T);

    m_seriesMuB->append(val(pt.muB), pt.T);
    m_seriesMuQ->append(val(pt.muQ), pt.T);

    m_seriesMunue->append(val(pt.munue), pt.T);
    m_seriesMunumu->append(val(pt.munumu), pt.T);
    m_seriesMnutau->append(val(pt.mnutau), pt.T);

    // Errors: Temperature horizontally by default
    m_seriesErrB->append(pt.T, val(pt.err_b));
    m_seriesErrQ->append(pt.T, val(pt.err_charge));
    m_seriesErrLe->append(pt.T, val(pt.err_le));
    m_seriesErrLmu->append(pt.T, val(pt.err_lmu));
    m_seriesErrLtau->append(pt.T, val(pt.err_ltau));
  } else {
    m_seriesnB->append(pt.T, val(pt.nB));
    m_seriesS->append(pt.T, val(pt.s));
    m_seriesnQ->append(pt.T, val(pt.nQ));

    m_seriesMuB->append(pt.T, val(pt.muB));
    m_seriesMuQ->append(pt.T, val(pt.muQ));

    m_seriesMunue->append(pt.T, val(pt.munue));
    m_seriesMunumu->append(pt.T, val(pt.munumu));
    m_seriesMnutau->append(pt.T, val(pt.mnutau));

    // Errors: Flipped
    m_seriesErrB->append(val(pt.err_b), pt.T);
    m_seriesErrQ->append(val(pt.err_charge), pt.T);
    m_seriesErrLe->append(val(pt.err_le), pt.T);
    m_seriesErrLmu->append(val(pt.err_lmu), pt.T);
    m_seriesErrLtau->append(val(pt.err_ltau), pt.T);
  }

  updateChartAxes();
}

void MainWindow::updateChartAxes() {
  if (m_trajectoryData.isEmpty()) return;

  double minT = m_trajectoryData.first().T;
  double maxT = m_trajectoryData.first().T;
  
  double minDens = 1e99, maxDens = -1e99;
  double minMu = 1e99, maxMu = -1e99;
  double minLep = 1e99, maxLep = -1e99;
  double minErr = 1e99, maxErr = -1e99;

  auto val = [this](double v) { 
    if (m_isLogScale) return std::max(std::abs(v), 1e-15);
    return v;
  };

  for (const auto &p : m_trajectoryData) {
    if (p.T < minT) minT = p.T;
    if (p.T > maxT) maxT = p.T;

    minDens = std::min({minDens, val(p.nB), val(p.s), val(p.nQ)});
    maxDens = std::max({maxDens, val(p.nB), val(p.s), val(p.nQ)});

    minMu = std::min({minMu, val(p.muB), val(p.muQ)});
    maxMu = std::max({maxMu, val(p.muB), val(p.muQ)});

    minLep = std::min({minLep, val(p.munue), val(p.munumu), val(p.mnutau)});
    maxLep = std::max({maxLep, val(p.munue), val(p.munumu), val(p.mnutau)});

    minErr = std::min({minErr, val(p.err_b), val(p.err_charge), val(p.err_le), val(p.err_lmu), val(p.err_ltau)});
    maxErr = std::max({maxErr, val(p.err_b), val(p.err_charge), val(p.err_le), val(p.err_lmu), val(p.err_ltau)});
  }

  // Add small margins
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
  };

  if (m_tempIsVertical) {
    setRange(m_densAxisY, minT, maxT, false);
    setRange(m_muAxisY,   minT, maxT, false);
    setRange(m_lepAxisY,  minT, maxT, false);

    setRange(m_densAxisX, minDens, maxDens, true);
    setRange(m_muAxisX,   minMu,   maxMu,   true);
    setRange(m_lepAxisX,  minLep,  maxLep,  true);

    setRange(m_errAxisX, minT, maxT, false);
    setRange(m_errAxisY, minErr, maxErr, true);
  } else {
    setRange(m_densAxisX, minT, maxT, false);
    setRange(m_muAxisX,   minT, maxT, false);
    setRange(m_lepAxisX,  minT, maxT, false);

    setRange(m_densAxisY, minDens, maxDens, true);
    setRange(m_muAxisY,   minMu,   maxMu,   true);
    setRange(m_lepAxisY,  minLep,  maxLep,  true);

    setRange(m_errAxisX, minErr, maxErr, true);
    setRange(m_errAxisY, minT,   maxT,   false);
  }
}

void MainWindow::onLogMessage(const QString &msg) {
  m_console->append(msg);
}

void MainWindow::onProgressUpdated(int pct) {
  m_progressBar->setValue(pct);
}

void MainWindow::onSimulationFinished() {
  m_btnRun->setEnabled(true);
  m_btnStop->setEnabled(false);
  m_btnExportFullData->setEnabled(true);
  m_statusLabel->setText("Complete");
  m_statusLabel->setStyleSheet("color: #28a745; font-weight: bold;"); // Green
}

void MainWindow::onSimulationError(const QString &msg) {
  m_btnRun->setEnabled(true);
  m_btnStop->setEnabled(false);
  m_btnExportFullData->setEnabled(true);
  m_console->append("<font color='#ff4b4b'><b>Error:</b> " + msg + "</font>");
  m_statusLabel->setText("Failed");
  m_statusLabel->setStyleSheet("color: #dc3545; font-weight: bold;"); // Red
}

void MainWindow::onThemeToggleClicked() {
  QChart::ChartTheme currentTheme = m_densityChartView->chart()->theme();
  QChart::ChartTheme newTheme = (currentTheme == QChart::ChartThemeLight) ? QChart::ChartThemeDark : QChart::ChartThemeLight;
  
  m_densityChartView->chart()->setTheme(newTheme);
  m_muChartView->chart()->setTheme(newTheme);
  m_leptonChartView->chart()->setTheme(newTheme);
  m_errorChartView->chart()->setTheme(newTheme);

  // setThemes resets series colors, so we restore them
  m_seriesnB->setColor(Qt::blue);
  m_seriesS->setColor(Qt::green);
  m_seriesnQ->setColor(QColor(255, 165, 0)); // Orange

  m_seriesMuB->setColor(Qt::red);
  m_seriesMuQ->setColor(QColor(128, 0, 128)); // Purple

  m_seriesMunue->setColor(Qt::cyan);
  m_seriesMunumu->setColor(Qt::magenta);
  m_seriesMnutau->setColor(QColor(200, 200, 0)); 

  m_seriesErrB->setColor(Qt::blue);
  m_seriesErrQ->setColor(QColor(255, 165, 0));
  m_seriesErrLe->setColor(Qt::cyan);
  m_seriesErrLmu->setColor(Qt::magenta);
  m_seriesErrLtau->setColor(QColor(200, 200, 0));
}

void MainWindow::onAxisToggleClicked() {
  m_tempIsVertical = !m_tempIsVertical;
  replotData();
  updateCriticalPoint();
}

void MainWindow::replotData() {
  auto updateTitles = [&](QAbstractAxis* axisX, QAbstractAxis* axisY, const QString& valLabel) {
     axisX->setTitleText(m_tempIsVertical ? valLabel : "Temperature [MeV]");
     axisY->setTitleText(m_tempIsVertical ? "Temperature [MeV]" : valLabel);
  };
  
  updateTitles(m_densAxisX, m_densAxisY, "Densities [MeV^3]");
  updateTitles(m_muAxisX, m_muAxisY, "Chem Pot [MeV] (abs)");
  updateTitles(m_lepAxisX, m_lepAxisY, "Chem Pot [MeV] (abs)");
  // Error plot flips opposite to others
  m_errAxisX->setTitleText(m_tempIsVertical ? "Temperature [MeV]" : "Relative Error");
  m_errAxisY->setTitleText(m_tempIsVertical ? "Relative Error" : "Temperature [MeV]");

  // Re-plot data by rebuilding series and keeping underlying memory
  clearCharts(true);
  
  auto val = [this](double v) { 
    if (m_isLogScale) return std::max(std::abs(v), 1e-15);
    return v;
  };

  for (const auto &pt : m_trajectoryData) {
    if (m_tempIsVertical) {
      m_seriesnB->append(val(pt.nB), pt.T);
      m_seriesS->append(val(pt.s), pt.T);
      m_seriesnQ->append(val(pt.nQ), pt.T);
      m_seriesMuB->append(val(pt.muB), pt.T);
      m_seriesMuQ->append(val(pt.muQ), pt.T);
      m_seriesMunue->append(val(pt.munue), pt.T);
      m_seriesMunumu->append(val(pt.munumu), pt.T);
      m_seriesMnutau->append(val(pt.mnutau), pt.T);
      // Errors: Temp on X
      m_seriesErrB->append(pt.T, val(pt.err_b));
      m_seriesErrQ->append(pt.T, val(pt.err_charge));
      m_seriesErrLe->append(pt.T, val(pt.err_le));
      m_seriesErrLmu->append(pt.T, val(pt.err_lmu));
      m_seriesErrLtau->append(pt.T, val(pt.err_ltau));
    } else {
      m_seriesnB->append(pt.T, val(pt.nB));
      m_seriesS->append(pt.T, val(pt.s));
      m_seriesnQ->append(pt.T, val(pt.nQ));
      m_seriesMuB->append(pt.T, val(pt.muB));
      m_seriesMuQ->append(pt.T, val(pt.muQ));
      m_seriesMunue->append(pt.T, val(pt.munue));
      m_seriesMunumu->append(pt.T, val(pt.munumu));
      m_seriesMnutau->append(pt.T, val(pt.mnutau));
      // Errors: Flipped
      m_seriesErrB->append(val(pt.err_b), pt.T);
      m_seriesErrQ->append(val(pt.err_charge), pt.T);
      m_seriesErrLe->append(val(pt.err_le), pt.T);
      m_seriesErrLmu->append(val(pt.err_lmu), pt.T);
      m_seriesErrLtau->append(val(pt.err_ltau), pt.T);
    }
  }
  updateChartAxes();
}

void MainWindow::onScaleToggleClicked() {
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

    swapAxes(m_densityChartView, m_densAxisX, m_densAxisY, "Densities [MeV^3]");
    swapAxes(m_muChartView,      m_muAxisX,   m_muAxisY,   "Chem Pot [MeV] (abs)");
    swapAxes(m_leptonChartView,  m_lepAxisX,  m_lepAxisY,  "Chem Pot [MeV] (abs)");
    
    // Switch for Error plot
    {
        QChart *chart = m_errorChartView->chart();
        chart->removeAxis(m_errAxisX);
        chart->removeAxis(m_errAxisY);
        delete m_errAxisX;
        delete m_errAxisY;

        if (m_isLogScale) {
            m_errAxisX = new QLogValueAxis();
            static_cast<QLogValueAxis*>(m_errAxisX)->setBase(10.0);
            m_errAxisY = new QLogValueAxis();
            static_cast<QLogValueAxis*>(m_errAxisY)->setBase(10.0);
        } else {
            m_errAxisX = new QValueAxis();
            m_errAxisY = new QValueAxis();
        }
        m_errAxisX->setTitleText(m_tempIsVertical ? "Temperature [MeV]" : "Relative Error");
        chart->addAxis(m_errAxisX, Qt::AlignBottom);
        m_errAxisY->setTitleText(m_tempIsVertical ? "Relative Error" : "Temperature [MeV]");
        chart->addAxis(m_errAxisY, Qt::AlignLeft);
    }

    // Attach series to new axes
    m_seriesnB->attachAxis(m_densAxisX); m_seriesnB->attachAxis(m_densAxisY);
    m_seriesS->attachAxis(m_densAxisX);  m_seriesS->attachAxis(m_densAxisY);
    m_seriesnQ->attachAxis(m_densAxisX); m_seriesnQ->attachAxis(m_densAxisY);

    m_seriesMuB->attachAxis(m_muAxisX);  m_seriesMuB->attachAxis(m_muAxisY);
    m_seriesMuQ->attachAxis(m_muAxisX);  m_seriesMuQ->attachAxis(m_muAxisY);
    m_seriesCpB->attachAxis(m_muAxisX);  m_seriesCpB->attachAxis(m_muAxisY);
    m_seriesCpQ->attachAxis(m_muAxisX);  m_seriesCpQ->attachAxis(m_muAxisY);

    m_seriesMunue->attachAxis(m_lepAxisX);  m_seriesMunue->attachAxis(m_lepAxisY);
    m_seriesMunumu->attachAxis(m_lepAxisX); m_seriesMunumu->attachAxis(m_lepAxisY);
    m_seriesMnutau->attachAxis(m_lepAxisX); m_seriesMnutau->attachAxis(m_lepAxisY);

    m_seriesErrB->attachAxis(m_errAxisX);    m_seriesErrB->attachAxis(m_errAxisY);
    m_seriesErrQ->attachAxis(m_errAxisX);    m_seriesErrQ->attachAxis(m_errAxisY);
    m_seriesErrLe->attachAxis(m_errAxisX);   m_seriesErrLe->attachAxis(m_errAxisY);
    m_seriesErrLmu->attachAxis(m_errAxisX);  m_seriesErrLmu->attachAxis(m_errAxisY);
    m_seriesErrLtau->attachAxis(m_errAxisX); m_seriesErrLtau->attachAxis(m_errAxisY);

    updateAxesTypes();

    // Re-render everything
    if (!m_trajectoryData.isEmpty()) {
        replotData();
    }
    updateCriticalPoint();
}

void MainWindow::updateAxesTypes() {
    auto setFmt = [this](QAbstractAxis *axis) {
        if (m_isLogScale) {
            static_cast<QLogValueAxis*>(axis)->setLabelFormat("%g");
        } else {
            static_cast<QValueAxis*>(axis)->setLabelFormat("%g");
        }
    };
    setFmt(m_densAxisX); setFmt(m_densAxisY);
    setFmt(m_muAxisX);   setFmt(m_muAxisY);
    setFmt(m_lepAxisX);  setFmt(m_lepAxisY);
    setFmt(m_errAxisX);  setFmt(m_errAxisY);
}

void MainWindow::onExportClicked() {
  if (m_trajectoryData.isEmpty()) {
    QMessageBox::warning(this, "No Data", "There is no trajectory data to export.");
    return;
  }

  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Export Chart");
  msgBox.setText("Select export format:");
  QPushButton *btnPdf = msgBox.addButton("Export as PDF", QMessageBox::ActionRole);
  QPushButton *btnTxt = msgBox.addButton("Export as TXT", QMessageBox::ActionRole);
  msgBox.addButton(QMessageBox::Cancel);
  msgBox.exec();

  int currentTab = m_chartTabs->currentIndex(); // 0 = Densities, 1 = Chem Pot, 2 = Lepton

  if (msgBox.clickedButton() == btnPdf) {
    QString fileName = QFileDialog::getSaveFileName(this, "Save PDF", QDir::currentPath(), "PDF Files (*.pdf)");
    if (fileName.isEmpty()) return;

    QChartView *activeView = nullptr;
    if (currentTab == 0) activeView = m_densityChartView;
    else if (currentTab == 1) activeView = m_muChartView;
    else if (currentTab == 2) activeView = m_leptonChartView;
    else if (currentTab == 3) activeView = m_errorChartView;

    if (!activeView) return;

    QPdfWriter writer(fileName);
    writer.setCreator("Cosmic Trajectories");
    writer.setPageSize(QPageSize(QPageSize::A4));
    writer.setPageOrientation(QPageLayout::Landscape);
    
    QPainter painter(&writer);
    activeView->render(&painter);
    painter.end();
    
    QMessageBox::information(this, "Success", "Chart successfully exported to PDF.");
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
    if (currentTab == 0) {
      out << "T\tnB\ts\t|nQ_QCD|\n";
      for (const auto &pt : m_trajectoryData) out << pt.T << "\t" << pt.nB << "\t" << pt.s << "\t" << pt.nQ << "\n";
    } else if (currentTab == 1) {
      out << "T\t|muB|\t|muQ|\n";
      for (const auto &pt : m_trajectoryData) out << pt.T << "\t" << pt.muB << "\t" << pt.muQ << "\n";
    } else if (currentTab == 2) {
      out << "T\t|munue|\t|munumu|\t|mnutau|\n";
      for (const auto &pt : m_trajectoryData) out << pt.T << "\t" << pt.munue << "\t" << pt.munumu << "\t" << pt.mnutau << "\n";
    } else if (currentTab == 3) {
      out << "T\terr_b\terr_charge\terr_le\terr_lmu\terr_ltau\n";
      for (const auto &pt : m_trajectoryData) out << pt.T << "\t" << pt.err_b << "\t" << pt.err_charge << "\t" << pt.err_le << "\t" << pt.err_lmu << "\t" << pt.err_ltau << "\n";
    }
    file.close();
    QMessageBox::information(this, "Success", "Chart data successfully exported to TXT.");
  }
}

void MainWindow::onExportFullDataClicked() {
  if (m_trajectoryData.isEmpty()) {
    QMessageBox::warning(this, "No Data", "There is no trajectory data to export. Please run a simulation first.");
    return;
  }

  QString fileName = QFileDialog::getSaveFileName(this, "Export Full Dataset", QDir::currentPath(), "Text Files (*.txt);;CSV Files (*.csv)");
  if (fileName.isEmpty()) return;

  QFile file(fileName);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
    QMessageBox::critical(this, "Error", "Could not open file for writing.");
    return;
  }

  QTextStream out(&file);
  // Header
  out << "T\tmuB\tmuQ\tmunue\tmunumu\tmnutau\tnB\tnQ_QCD\ts_QCD\ts_tot\tne\tnmu\tntau\tnnue\tnnumu\tnnutau\n";
  
  for (const auto &pt : m_trajectoryData) {
    out << pt.T << "\t" 
        << pt.muB << "\t" 
        << pt.muQ << "\t" 
        << pt.munue << "\t" 
        << pt.munumu << "\t" 
        << pt.mnutau << "\t" 
        << pt.nB << "\t" 
        << pt.nQ << "\t" 
        << pt.s_QCD << "\t" 
        << pt.s << "\t" 
        << pt.ne << "\t" 
        << pt.nmu << "\t" 
        << pt.ntau << "\t" 
        << pt.nnue << "\t" 
        << pt.nnumu << "\t" 
        << pt.nnutau << "\n";
  }
  file.close();
  QMessageBox::information(this, "Success", "Full dataset exported successfully.");
}

void MainWindow::onCriticalPointButtonClicked() {
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
    connect(m_chkShowCp, &QCheckBox::toggled, this, &MainWindow::updateCriticalPoint);
  }
  m_cpDialog->show();
  m_cpDialog->raise();
  m_cpDialog->activateWindow();
}

void MainWindow::updateCriticalPoint() {
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


