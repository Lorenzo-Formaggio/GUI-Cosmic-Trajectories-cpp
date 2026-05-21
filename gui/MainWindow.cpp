#include "MainWindow.h"
#include "CompareWidget.h"
#include "EosExplorerWidget.h"
#include "RunFromFileWidget.h"

#include <QApplication>
#include <QDoubleSpinBox>
#include <QSpinBox>
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
#include <QScrollArea>
#include <QMenu>
#include <QAction>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QLogValueAxis>
#include <QtCharts/QValueAxis>
#include <QtDataVisualization/Q3DInputHandler>
#include <QtDataVisualization/Q3DTheme>
#include <QtDataVisualization/Q3DCamera>
#include <QtDataVisualization/Q3DScene>
#include <QtDataVisualization/QValue3DAxis>
#include <QtDataVisualization/QAbstract3DGraph>
#include <QSlider>
#include <QFile>
#include <QMouseEvent>
#include "TooltipChartView.h"

// ── Custom Input Handler for 3D Plot ──────────────────────────────────────
// Maps LeftButton to RightButton for rotation and vice versa for selection.
class LeftClick3DInputHandler : public Q3DInputHandler {
public:
    explicit LeftClick3DInputHandler(QObject *parent = nullptr) : Q3DInputHandler(parent) {}

    void mousePressEvent(QMouseEvent *event, const QPoint &mousePos) override {
        if (event->button() == Qt::LeftButton) {
            QMouseEvent fakeEvent(event->type(), event->position(), event->scenePosition(), event->globalPosition(),
                                  Qt::RightButton, event->buttons() | Qt::RightButton, event->modifiers());
            Q3DInputHandler::mousePressEvent(&fakeEvent, mousePos);
        } else if (event->button() == Qt::RightButton) {
            QMouseEvent fakeEvent(event->type(), event->position(), event->scenePosition(), event->globalPosition(),
                                  Qt::LeftButton, event->buttons() | Qt::LeftButton, event->modifiers());
            Q3DInputHandler::mousePressEvent(&fakeEvent, mousePos);
        } else {
            Q3DInputHandler::mousePressEvent(event, mousePos);
        }
    }

    void mouseMoveEvent(QMouseEvent *event, const QPoint &mousePos) override {
        Qt::MouseButtons buttons = event->buttons();
        if (buttons & Qt::LeftButton) {
            buttons = (buttons & ~Qt::LeftButton) | Qt::RightButton;
            QMouseEvent fakeEvent(event->type(), event->position(), event->scenePosition(), event->globalPosition(),
                                  Qt::NoButton, buttons, event->modifiers());
            Q3DInputHandler::mouseMoveEvent(&fakeEvent, mousePos);
        } else if (buttons & Qt::RightButton) {
            buttons = (buttons & ~Qt::RightButton) | Qt::LeftButton;
            QMouseEvent fakeEvent(event->type(), event->position(), event->scenePosition(), event->globalPosition(),
                                  Qt::NoButton, buttons, event->modifiers());
            Q3DInputHandler::mouseMoveEvent(&fakeEvent, mousePos);
        } else {
            Q3DInputHandler::mouseMoveEvent(event, mousePos);
        }
    }

    void mouseReleaseEvent(QMouseEvent *event, const QPoint &mousePos) override {
        if (event->button() == Qt::LeftButton) {
            QMouseEvent fakeEvent(event->type(), event->position(), event->scenePosition(), event->globalPosition(),
                                  Qt::RightButton, event->buttons() & ~Qt::RightButton, event->modifiers());
            Q3DInputHandler::mouseReleaseEvent(&fakeEvent, mousePos);
        } else if (event->button() == Qt::RightButton) {
            QMouseEvent fakeEvent(event->type(), event->position(), event->scenePosition(), event->globalPosition(),
                                  Qt::LeftButton, event->buttons() & ~Qt::LeftButton, event->modifiers());
            Q3DInputHandler::mouseReleaseEvent(&fakeEvent, mousePos);
        } else {
            Q3DInputHandler::mouseReleaseEvent(event, mousePos);
        }
    }
};

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
  QGroupBox *groupParams = leftPanel->findChild<QGroupBox*>("GroupParams");
  QScrollArea *scrollArea = new QScrollArea(leftPanel);
  scrollArea->setWidget(groupParams);
  scrollArea->setWidgetResizable(true);
  scrollArea->setFrameShape(QFrame::NoFrame);
  leftLayout->addWidget(scrollArea);

  createConsolePanel(leftPanel);
  leftLayout->addWidget(leftPanel->findChild<QGroupBox*>("GroupConsole"));

  QWidget *rightPanel = new QWidget(splitter);
  QVBoxLayout *rightLayout = new QVBoxLayout(rightPanel);
  rightLayout->setContentsMargins(0, 0, 0, 0);
  createChartPanel(rightPanel);
  rightLayout->addWidget(m_chartTabs);



  // ── Plot Settings Menu (Inherits Global Style) ───────────────────────────
  QPushButton *btnPlotSettings = new QPushButton("Plot Settings", rightPanel);
  
  QMenu *plotMenu = new QMenu(this);
  
  // Section: Visibility
  QAction *actShowHide = plotMenu->addAction("Show/Hide Quantities...");
  connect(actShowHide, &QAction::triggered, this, [this]() {
    if (!m_visDialog) {
      m_visDialog = new QDialog(this);
      m_visDialog->setWindowTitle("Show/Hide Series");
      m_visDialog->setMinimumWidth(420);
      QVBoxLayout *dlgLayout = new QVBoxLayout(m_visDialog);

      // Build a "row" with [visibility] [|abs|] for each quantity.
      // The abs checkbox is disabled in log mode (abs is forced).
      auto findMeta = [this](QXYSeries *s) -> SeriesMeta* {
        for (auto &m : m_seriesMeta) if (m.series == s) return &m;
        return nullptr;
      };
      auto addQuantityRow = [&, this](QGridLayout *grid, int row,
                                      const QString &label, QCheckBox *&visChk,
                                      QCheckBox *&absChk, QXYSeries *series) {
        SeriesMeta *meta = findMeta(series);
        const bool defaultAbs = meta ? meta->useAbs : false;
        visChk = new QCheckBox(label);
        visChk->setChecked(true);
        absChk = new QCheckBox("|·|");
        absChk->setChecked(defaultAbs);
        absChk->setToolTip("Plot absolute value of this quantity. "
                            "Forced ON in log mode.");
        absChk->setEnabled(!m_isLogScale);
        grid->addWidget(visChk, row, 0);
        grid->addWidget(absChk, row, 1);
        connect(visChk, &QCheckBox::toggled, this, [series](bool v){
          if (series) series->setVisible(v);
        });
        connect(absChk, &QCheckBox::toggled, this, [this, series](bool on){
          for (auto &m : m_seriesMeta) {
            if (m.series == series) { m.useAbs = on; break; }
          }
          if (!m_trajectoryData.isEmpty()) replotData();
          else refreshSeriesNames();
        });
      };

      // ── Densities group ────────────────────────────────────
      QGroupBox *grpDens = new QGroupBox("Densities");
      QGridLayout *layDens = new QGridLayout(grpDens);
      addQuantityRow(layDens, 0, "nB",     m_chknB, m_absnB, m_seriesnB);
      addQuantityRow(layDens, 1, "s",      m_chkS,  m_absS,  m_seriesS);
      addQuantityRow(layDens, 2, "nQ", m_chknQ, m_absnQ, m_seriesnQ);
      dlgLayout->addWidget(grpDens);

      // ── Lepton Densities group ────────────────────────────
      QGroupBox *grpLepDens = new QGroupBox("Lepton Densities");
      QGridLayout *layLepDens = new QGridLayout(grpLepDens);
      addQuantityRow(layLepDens, 0, "ne",    m_chkNe,     m_absNe,     m_seriesNe);
      addQuantityRow(layLepDens, 1, "nμ",    m_chkNmu,    m_absNmu,    m_seriesNmu);
      addQuantityRow(layLepDens, 2, "nτ",    m_chkNtau,   m_absNtau,   m_seriesNtau);
      addQuantityRow(layLepDens, 3, "nνe",   m_chkNnue,   m_absNnue,   m_seriesNnue);
      addQuantityRow(layLepDens, 4, "nνμ",   m_chkNnumu,  m_absNnumu,  m_seriesNnumu);
      addQuantityRow(layLepDens, 5, "nντ",   m_chkNnutau, m_absNnutau, m_seriesNnutau);
      dlgLayout->addWidget(grpLepDens);

      // ── Chemical Potentials group ──────────────────────────
      QGroupBox *grpMu = new QGroupBox("Chemical Potentials");
      QGridLayout *layMu = new QGridLayout(grpMu);
      addQuantityRow(layMu, 0, "μB", m_chkMuB, m_absMuB, m_seriesMuB);
      addQuantityRow(layMu, 1, "μQ", m_chkMuQ, m_absMuQ, m_seriesMuQ);
      dlgLayout->addWidget(grpMu);

      // ── Lepton Chem. Pot. group ────────────────────────────
      QGroupBox *grpLep = new QGroupBox("Lepton Chemical Potentials");
      QGridLayout *layLep = new QGridLayout(grpLep);
      addQuantityRow(layLep, 0, "μνe", m_chkMunue,   m_absMunue,   m_seriesMunue);
      addQuantityRow(layLep, 1, "μνμ", m_chkMunumu,  m_absMunumu,  m_seriesMunumu);
      addQuantityRow(layLep, 2, "μντ", m_chkMnutau,  m_absMnutau,  m_seriesMnutau);
      dlgLayout->addWidget(grpLep);

      // ── Errors group ───────────────────────────────────────
      QGroupBox *grpErr = new QGroupBox("Residual Errors");
      QGridLayout *layErr = new QGridLayout(grpErr);
      addQuantityRow(layErr, 0, "err_b",    m_chkErrB,    m_absErrB,    m_seriesErrB);
      addQuantityRow(layErr, 1, "err_q",    m_chkErrQ,    m_absErrQ,    m_seriesErrQ);
      addQuantityRow(layErr, 2, "err_le",   m_chkErrLe,   m_absErrLe,   m_seriesErrLe);
      addQuantityRow(layErr, 3, "err_lmu",  m_chkErrLmu,  m_absErrLmu,  m_seriesErrLmu);
      addQuantityRow(layErr, 4, "err_ltau", m_chkErrLtau, m_absErrLtau, m_seriesErrLtau);
      dlgLayout->addWidget(grpErr);
    }
    // Re-sync abs-checkbox enabled state with current scale before showing
    auto syncAbsEnabled = [this](QCheckBox *c){ if (c) c->setEnabled(!m_isLogScale); };
    syncAbsEnabled(m_absnB);  syncAbsEnabled(m_absS);   syncAbsEnabled(m_absnQ);
    syncAbsEnabled(m_absNe);  syncAbsEnabled(m_absNmu); syncAbsEnabled(m_absNtau);
    syncAbsEnabled(m_absNnue);syncAbsEnabled(m_absNnumu); syncAbsEnabled(m_absNnutau);
    syncAbsEnabled(m_absMuB); syncAbsEnabled(m_absMuQ);
    syncAbsEnabled(m_absMunue); syncAbsEnabled(m_absMunumu); syncAbsEnabled(m_absMnutau);
    syncAbsEnabled(m_absErrB); syncAbsEnabled(m_absErrQ);
    syncAbsEnabled(m_absErrLe); syncAbsEnabled(m_absErrLmu); syncAbsEnabled(m_absErrLtau);
    m_visDialog->show();
    m_visDialog->raise();
    m_visDialog->activateWindow();
  });

  plotMenu->addSeparator();

  // Section: View Controls
  QAction *actAxis = plotMenu->addAction("Toggle Axes");
  connect(actAxis, &QAction::triggered, this, &MainWindow::onAxisToggleClicked);

  QAction *actScale = plotMenu->addAction("Toggle Log/Linear");
  connect(actScale, &QAction::triggered, this, &MainWindow::onScaleToggleClicked);

  QAction *actTheme = plotMenu->addAction("Toggle Plot Theme");
  connect(actTheme, &QAction::triggered, this, &MainWindow::onThemeToggleClicked);

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
    v->addWidget(new QLabel("Append run parameters to legend entries:"));
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
  connect(actCP, &QAction::triggered, this, &MainWindow::onCriticalPointButtonClicked);

  QAction *actExport = plotMenu->addAction("Export Active Plot...");
  connect(actExport, &QAction::triggered, this, &MainWindow::onExportClicked);

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

  QHBoxLayout *bottomRightLayout = new QHBoxLayout();
  bottomRightLayout->setContentsMargins(0, 0, 0, 15);
  bottomRightLayout->addStretch();
  bottomRightLayout->addWidget(btnPlotSettings);
  bottomRightLayout->addWidget(btnAutoFit);
  bottomRightLayout->addStretch();
  
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

  // ─── Tab 3: EoS Explorer ──────────────────────────────────────────
  topTabs->addTab(new EosExplorerWidget(wdir.absolutePath()), "EoS Explorer");

  // ─── Tab 4: Run From File ─────────────────────────────────────────
  topTabs->addTab(new RunFromFileWidget(wdir.absolutePath()), "Run From File");
}

void MainWindow::setupStyle() {
  setStyleSheet(
    "/* Global Background */ "
    "QMainWindow, QWidget#centralWidget { background-color: #2b2b2b; color: #e0e0e0; } "

    "/* ToolTips */ "
    "QToolTip { color: #ffffff; background-color: #444444; border: 1px solid #666666; padding: 2px; } "

    "/* Group Boxes */ "
    "QGroupBox { border: 1px solid #1a1a1a; border-radius: 6px; margin-top: 1.5ex; background-color: #333333; color: #ffffff; font-weight: bold; } "
    "QGroupBox::title { subcontrol-origin: margin; left: 15px; padding: 0 5px; color: #aaaaaa; } "

    "/* Labels */ "
    "QLabel { color: #e0e0e0; } "

    "/* Buttons (Premium Dark Gradient) */ "
    "QPushButton { "
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4e5d6c, stop:1 #2b3e50); "
    "  color: white; "
    "  border: 1px solid #1a252f; "
    "  border-radius: 6px; "
    "  font-weight: bold; "
    "  padding: 6px 16px; "
    "} "
    "QPushButton::menu-indicator { image: none; } "
    "QPushButton:hover { "
    "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #5bc0de, stop:1 #2f96b4); "
    "} "
    "QPushButton:pressed { "
    "  background: #1a252f; "
    "} "
    "QPushButton:disabled { "
    "  background: #7f8c8d; "
    "  color: #bdc3c7; "
    "  border: 1px solid #1a252f; "
    "} "

    "/* Input Widgets */ "
    "QLineEdit, QComboBox, QDoubleSpinBox, QSpinBox { "
    "  border: 1px solid #1a1a1a; "
    "  border-radius: 3px; "
    "  padding: 4px; "
    "  background-color: #3c3c3c; "
    "  color: white; "
    "  selection-background-color: #555555; "
    "} "
    "QLineEdit:focus, QComboBox:focus, QDoubleSpinBox:focus { "
    "  border: 1px solid #555555; "
    "} "

    "/* Tabs */ "
    "QTabWidget::pane { border: 1px solid #1a1a1a; top: -1px; background: #333333; } "
    "QTabBar::tab { "
    "  background: #2b2b2b; "
    "  color: #999999; "
    "  border: 1px solid #1a1a1a; "
    "  padding: 8px 20px; "
    "  border-bottom-color: none; "
    "  border-top-left-radius: 4px; "
    "  border-top-right-radius: 4px; "
    "} "
    "QTabBar::tab:selected { "
    "  background: #333333; "
    "  color: white; "
    "  border-bottom-color: #333333; "
    "  font-weight: bold; "
    "} "

    "/* Console */ "
    "QTextEdit { "
    "  background-color: #1a1a1a; "
    "  color: #d4d4d4; "
    "  border: 1px solid #111111; "
    "  border-radius: 4px; "
    "  font-family: 'Courier New', Courier, monospace; "
    "  font-size: 11px; "
    "} "

    "/* Menu */ "
    "QMenu { background-color: #2b2b2b; color: white; border: 1px solid #1a1a1a; padding: 5px; } "
    "QMenu::item { padding: 5px 25px 5px 20px; border-radius: 3px; } "
    "QMenu::item:selected { background-color: #444444; color: white; } "
    "QMenu::separator { height: 1px; background: #444444; margin: 5px 10px; } "
  );
}

void MainWindow::createParameterPanel(QWidget *parent) {
  QGroupBox *group = new QGroupBox("Parameters", parent);
  group->setObjectName("GroupParams");
  QVBoxLayout *layout = new QVBoxLayout(group);

  m_btnSolverSettings = new QPushButton("Solver Settings...");
  m_btnSolverSettings->setObjectName("BtnSolver");
  connect(m_btnSolverSettings, &QPushButton::clicked, this, &MainWindow::onSolverSettingsButtonClicked);
  layout->addWidget(m_btnSolverSettings);

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
  m_spinTmin = addSpin("Tmin (MeV)", 80.0, 0.1, 10000.0, 1);
  m_spinTmax = addSpin("Tmax (MeV)", 250.0, 0.1, 10000.0, 1);

  m_comboNf = new QComboBox();
  m_comboNf->addItems({"2", "3", "4"});
  m_comboNf->setCurrentIndex(1); // Default to 3
  grid->addWidget(new QLabel("Flavors (nf)"), row, 0);
  grid->addWidget(m_comboNf, row++, 1);

  m_comboEos = new QComboBox();
  m_comboEos->addItems({"Free QGP (0)", "Lattice QCD (1)", "Interpolated Table (2)", "Entropy Contour (3)", "Entropy Contour Param (4)"});
  connect(m_comboEos, &QComboBox::currentIndexChanged, this, &MainWindow::onEosChanged);
  grid->addWidget(new QLabel("EoS"), row, 0);
  grid->addWidget(m_comboEos, row++, 1);

  m_eosPathWidget = new QWidget();
  QHBoxLayout *eosPathLayout = new QHBoxLayout(m_eosPathWidget);
  eosPathLayout->setContentsMargins(0, 0, 0, 0);
  m_lineEditEosPath = new QLineEdit();
  m_lineEditEosPath->setPlaceholderText("Path to EoS table...");
  m_btnBrowseEos = new QPushButton("Browse...");
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

  m_comboScan = new QComboBox();
  m_comboScan->addItems({"Low -> High (0)", "High -> Low (1)"});
  grid->addWidget(new QLabel("Scan Direction"), row, 0);
  grid->addWidget(m_comboScan, row++, 1);

  layout->addLayout(grid);

  QHBoxLayout *actionLayout = new QHBoxLayout();
  m_btnRun = new QPushButton("Run Simulation");
  m_btnRun->setObjectName("BtnRun");
  m_btnRun->setMinimumHeight(32);
  connect(m_btnRun, &QPushButton::clicked, this, &MainWindow::onRunClicked);

  m_btnStop = new QPushButton("Stop");
  m_btnStop->setObjectName("BtnStop");
  m_btnStop->setMinimumHeight(32);
  connect(m_btnStop, &QPushButton::clicked, this, &MainWindow::onStopClicked);

  actionLayout->addWidget(m_btnRun);
  actionLayout->addWidget(m_btnStop);
  layout->addLayout(actionLayout);

  m_btnExportFullData = new QPushButton("Export Full Data (TXT)");
  m_btnExportFullData->setObjectName("BtnExport");
  m_btnExportFullData->setMinimumHeight(24);
  m_btnExportFullData->setEnabled(false);
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

  QChart *c1, *c2, *c3, *c4, *c5;
  
  // Densities tab
  setupChart(m_densityChartView, c1, m_densAxisX, m_densAxisY, "Densities vs Temperature", "Densities [MeV^3]");
  m_seriesnB = new QLineSeries(); m_seriesnB->setName("nB"); m_seriesnB->setColor(Qt::blue);
  m_seriesS  = new QLineSeries(); m_seriesS->setName("s");   m_seriesS->setColor(Qt::green);
  m_seriesnQ = new QLineSeries(); m_seriesnQ->setName("|nQ|"); m_seriesnQ->setColor(QColor(255, 165, 0)); // Orange
  c1->addSeries(m_seriesnB); m_seriesnB->attachAxis(m_densAxisX); m_seriesnB->attachAxis(m_densAxisY);
  c1->addSeries(m_seriesS);  m_seriesS->attachAxis(m_densAxisX);  m_seriesS->attachAxis(m_densAxisY);
  c1->addSeries(m_seriesnQ); m_seriesnQ->attachAxis(m_densAxisX); m_seriesnQ->attachAxis(m_densAxisY);

  // Lepton Densities tab
  setupChart(m_leptonDensChartView, c5, m_lepDensAxisX, m_lepDensAxisY, "Lepton Densities", "Densities [MeV^3]");
  m_seriesNe = new QLineSeries(); m_seriesNe->setName("ne"); m_seriesNe->setColor(QColor(0, 0, 128)); // Navy
  m_seriesNmu = new QLineSeries(); m_seriesNmu->setName("nμ"); m_seriesNmu->setColor(QColor(139, 0, 0)); // Dark red
  m_seriesNtau = new QLineSeries(); m_seriesNtau->setName("nτ"); m_seriesNtau->setColor(QColor(85, 107, 47)); // Dark olive green
  m_seriesNnue   = new QLineSeries(); m_seriesNnue->setName("nνe");   m_seriesNnue->setColor(QColor(0, 128, 128));   // Teal
  m_seriesNnumu  = new QLineSeries(); m_seriesNnumu->setName("nνμ");  m_seriesNnumu->setColor(QColor(153, 50, 204)); // Dark orchid
  m_seriesNnutau = new QLineSeries(); m_seriesNnutau->setName("nντ"); m_seriesNnutau->setColor(QColor(255, 140, 0)); // Dark orange
  c5->addSeries(m_seriesNe);     m_seriesNe->attachAxis(m_lepDensAxisX);     m_seriesNe->attachAxis(m_lepDensAxisY);
  c5->addSeries(m_seriesNmu);    m_seriesNmu->attachAxis(m_lepDensAxisX);    m_seriesNmu->attachAxis(m_lepDensAxisY);
  c5->addSeries(m_seriesNtau);   m_seriesNtau->attachAxis(m_lepDensAxisX);   m_seriesNtau->attachAxis(m_lepDensAxisY);
  c5->addSeries(m_seriesNnue);   m_seriesNnue->attachAxis(m_lepDensAxisX);   m_seriesNnue->attachAxis(m_lepDensAxisY);
  c5->addSeries(m_seriesNnumu);  m_seriesNnumu->attachAxis(m_lepDensAxisX);  m_seriesNnumu->attachAxis(m_lepDensAxisY);
  c5->addSeries(m_seriesNnutau); m_seriesNnutau->attachAxis(m_lepDensAxisX); m_seriesNnutau->attachAxis(m_lepDensAxisY);

  // Chem pots tab
  setupChart(m_muChartView, c2, m_muAxisX, m_muAxisY, "Baryon & Electric Chem Pot", "Chem Pot [MeV] (abs)");
  m_seriesMuB = new QLineSeries(); m_seriesMuB->setName("|μB|"); m_seriesMuB->setColor(Qt::red);
  m_seriesMuQ = new QLineSeries(); m_seriesMuQ->setName("|μQ|"); m_seriesMuQ->setColor(QColor(128, 0, 128));
  QPen penMuQ(QColor(128, 0, 128)); penMuQ.setStyle(Qt::DashLine); penMuQ.setWidthF(2.0);
  m_seriesMuQ->setPen(penMuQ);
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

  // Register all data series with their base name and default abs flag.
  // Quantities previously displayed as "|·|" default to abs=ON; signed-or-positive
  // quantities default to abs=OFF.
  registerSeriesAbs(m_seriesnB,     "nB",       false);
  registerSeriesAbs(m_seriesS,      "s",        false);
  registerSeriesAbs(m_seriesnQ,     "nQ",   true);

  registerSeriesAbs(m_seriesNe,     "ne",       false);
  registerSeriesAbs(m_seriesNmu,    "nμ",       false);
  registerSeriesAbs(m_seriesNtau,   "nτ",       false);
  registerSeriesAbs(m_seriesNnue,   "nνe",      false);
  registerSeriesAbs(m_seriesNnumu,  "nνμ",      false);
  registerSeriesAbs(m_seriesNnutau, "nντ",      false);

  registerSeriesAbs(m_seriesMuB,    "μB",       true);
  registerSeriesAbs(m_seriesMuQ,    "μQ",       true);

  registerSeriesAbs(m_seriesMunue,  "μνe",      true);
  registerSeriesAbs(m_seriesMunumu, "μνμ",      true);
  registerSeriesAbs(m_seriesMnutau, "μντ",      true);

  registerSeriesAbs(m_seriesErrB,    "err_b",      true);
  registerSeriesAbs(m_seriesErrQ,    "err_charge", true);
  registerSeriesAbs(m_seriesErrLe,   "err_le",     true);
  registerSeriesAbs(m_seriesErrLmu,  "err_lmu",    true);
  registerSeriesAbs(m_seriesErrLtau, "err_ltau",   true);

  refreshSeriesNames();
  refreshLegendVisibility();

  // ── 3D trajectory tab (μB, μQ, T) ───────────────────────────────────
  // Q3DScatter is a QWindow; embed it via createWindowContainer so it can
  // live inside QTabWidget alongside the 2D chart views.
  m_scatter3D = new Q3DScatter();
  m_scatter3D->setActiveInputHandler(new LeftClick3DInputHandler(m_scatter3D));
  m_scatter3D->activeTheme()->setType(Q3DTheme::ThemeQt);
  m_scatter3D->activeTheme()->setBackgroundColor(QColor("#2b2b2b"));
  m_scatter3D->activeTheme()->setWindowColor(QColor("#2b2b2b"));
  m_scatter3D->activeTheme()->setLabelTextColor(Qt::white);
  m_scatter3D->setShadowQuality(QAbstract3DGraph::ShadowQualityNone);

  // Explicit value axes with auto-fit + a bit of segment density.
  auto *axX = new QValue3DAxis(); axX->setTitle("μB [MeV]");          axX->setTitleVisible(true);
  axX->setAutoAdjustRange(true);  axX->setLabelFormat(QStringLiteral("%.1f"));
  auto *axY = new QValue3DAxis(); axY->setTitle("Temperature T [MeV]"); axY->setTitleVisible(true);
  axY->setAutoAdjustRange(true);  axY->setLabelFormat(QStringLiteral("%.1f"));
  auto *axZ = new QValue3DAxis(); axZ->setTitle("μQ [MeV]");          axZ->setTitleVisible(true);
  axZ->setAutoAdjustRange(true);  axZ->setLabelFormat(QStringLiteral("%.1f"));
  m_scatter3D->setAxisX(axX);
  m_scatter3D->setAxisY(axY);
  m_scatter3D->setAxisZ(axZ);

  // Make the bounding box roomier on the floor (X = μB, Z = μQ) so μB
  // doesn't visually collapse when its data range is small.
  m_scatter3D->setAspectRatio(2.5);
  m_scatter3D->setHorizontalAspectRatio(1.0);

  m_scatter3D->scene()->activeCamera()->setCameraPreset(Q3DCamera::CameraPresetIsometricRight);
  m_scatter3D->setSelectionMode(QAbstract3DGraph::SelectionItem);

  m_series3D = new QScatter3DSeries();
  m_series3D->setName("Trajectory");
  // Sphere mesh + small size + per-segment subdivision below makes the
  // trajectory render as a continuous tube rather than a sparse scatter.
  m_series3D->setItemSize(0.06f);
  m_series3D->setMesh(QAbstract3DSeries::MeshSphere);
  m_series3D->setBaseColor(QColor(255, 165, 0));
  // Hover tooltip: show the (μB, T, μQ) triple for the selected point.
  m_series3D->setItemLabelFormat(QStringLiteral(
      "μB: @xLabel MeV\nT: @yLabel MeV\nμQ: @zLabel MeV"));
  m_scatter3D->addSeries(m_series3D);

  m_scatter3DContainer = QWidget::createWindowContainer(m_scatter3D, m_chartTabs);
  m_scatter3DContainer->setMinimumSize(QSize(320, 240));
  m_scatter3DContainer->setFocusPolicy(Qt::StrongFocus);

  // First-order surface overlay (rendered as a translucent sphere cloud).
  // Disabled until the user picks the Entropy Contour EoS.
  m_surface3D = new QScatter3DSeries();
  m_surface3D->setName("First-order surface");
  m_surface3D->setItemSize(0.05f);
  m_surface3D->setMesh(QAbstract3DSeries::MeshSphere);
  m_surface3D->setBaseColor(QColor(80, 160, 255, 90));
  m_surface3D->setVisible(false);
  m_scatter3D->addSeries(m_surface3D);

  // Toolbar: enable surface + opacity slider, sit above the 3D view
  QWidget *tabRoot = new QWidget(m_chartTabs);
  QVBoxLayout *tabLay = new QVBoxLayout(tabRoot);
  tabLay->setContentsMargins(0, 0, 0, 0);

  QHBoxLayout *toolBar = new QHBoxLayout();
  toolBar->setContentsMargins(8, 4, 8, 4);
  m_chkContinuous3D = new QCheckBox("Continuous line", tabRoot);
  m_chkContinuous3D->setChecked(m_continuous3D);
  m_chkContinuous3D->setToolTip("Linearly interpolate between trajectory points "
                                 "to show a continuous tube. Off = raw points.");
  m_chkSurface = new QCheckBox("Show first-order surface", tabRoot);
  m_chkSurface->setEnabled(false);
  m_chkSurface->setToolTip("Available when the Entropy Contour EoS is selected.");
  m_lblSurfaceOpacity = new QLabel("Opacity:", tabRoot);
  m_sliderSurfaceOpacity = new QSlider(Qt::Horizontal, tabRoot);
  m_sliderSurfaceOpacity->setRange(5, 255);
  m_sliderSurfaceOpacity->setValue(90);
  m_sliderSurfaceOpacity->setEnabled(false);
  m_sliderSurfaceOpacity->setMaximumWidth(220);
  toolBar->addWidget(m_chkContinuous3D);
  toolBar->addSpacing(20);
  toolBar->addWidget(m_chkSurface);
  toolBar->addStretch();
  toolBar->addWidget(m_lblSurfaceOpacity);
  toolBar->addWidget(m_sliderSurfaceOpacity);
  tabLay->addLayout(toolBar);
  tabLay->addWidget(m_scatter3DContainer, /*stretch=*/1);

  connect(m_chkContinuous3D, &QCheckBox::toggled, this, [this](bool on) {
    m_continuous3D = on;
    rebuild3DSeriesFromTrajectory();
  });
  connect(m_chkSurface, &QCheckBox::toggled, this, [this](bool on) {
    if (on && !m_surfaceLoaded) loadFirstOrderSurface();
    if (m_surface3D) m_surface3D->setVisible(on && m_surfaceLoaded);
    if (m_sliderSurfaceOpacity) m_sliderSurfaceOpacity->setEnabled(on && m_surfaceLoaded);
  });
  connect(m_sliderSurfaceOpacity, &QSlider::valueChanged, this, [this](int) {
    applySurfaceColor();
  });

  m_chartTabs->addTab(tabRoot, "3D Trajectory");
}

void MainWindow::onEosChanged(int index) {
  if (index == 2) {
    onLogMessage("Note: For Interpolated Table (EoS=2), user Tmin/Tmax are used if inside the table range; otherwise they are clamped to the table bounds.");
  }
  bool showEosPath = (index == 2);
  m_labelEosPath->setVisible(showEosPath);
  m_eosPathWidget->setVisible(showEosPath);

  // First-order surface overlay is only meaningful for the Entropy Contour
  // family of EoS (file-loaded chi tables = 3; analytic parametrization = 4).
  // Each variant has its own data file, so a cached load from one variant
  // is invalidated when switching to the other.
  const bool entrCont = (index == 3 || index == 4);
  if (m_chkSurface) m_chkSurface->setEnabled(entrCont);
  if (m_sliderSurfaceOpacity) m_sliderSurfaceOpacity->setEnabled(entrCont && m_chkSurface && m_chkSurface->isChecked());
  if (!entrCont) {
    if (m_chkSurface) m_chkSurface->setChecked(false);
    if (m_surface3D)  m_surface3D->setVisible(false);
  } else if (m_surfaceLoaded && m_surfaceLoadedEos != index) {
    // Switched between the two entrCont variants: invalidate the cached
    // surface and reload now if the checkbox is currently on.
    m_surfaceLoaded = false;
    m_surfaceLoadedEos = -1;
    if (m_chkSurface && m_chkSurface->isChecked()) {
      loadFirstOrderSurface();
      if (m_surface3D) m_surface3D->setVisible(m_surfaceLoaded);
    }
  }
}

void MainWindow::clearCharts(bool keepData) {
  m_seriesnB->clear();
  m_seriesS->clear();
  m_seriesnQ->clear();
  
  m_seriesNe->clear();
  m_seriesNmu->clear();
  m_seriesNtau->clear();
  m_seriesNnue->clear();
  m_seriesNnumu->clear();
  m_seriesNnutau->clear();
  
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

  if (m_series3D && m_series3D->dataProxy()) {
    m_series3D->dataProxy()->resetArray(new QScatterDataArray);
  }

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

  // Snapshot run parameters for legend display
  m_runB    = m_spinB->value();
  m_runLe   = m_spinLe->value();
  m_runLmu  = m_spinLmu->value();
  m_runLtau = m_spinLtau->value();
  m_runParamsValid = true;
  refreshSeriesNames();
  refreshLegendVisibility();
  m_worker->dT = m_spinDT->value();
  m_worker->Tmin = m_spinTmin->value();
  m_worker->Tmax = m_spinTmax->value();
  
  m_worker->tolerance = m_tolerance;
  m_worker->maxIter = m_maxIter;
  
  int nf = m_comboNf->currentText().toInt();
  int eos = m_comboEos->currentIndex();
  if (eos == 1 && nf == 2) {
      onLogMessage("<span style=\"color:#ffc107;\"><b>Warning:</b> System does not have a 2 flavor Lattice QCD EoS. Defaulting to 3 flavors.</span>");
      nf = 3;
  }
  m_worker->nf = nf;
  m_worker->eos = eos;
  m_worker->guessMethod = m_guessMethod;
  m_worker->scanDirection = m_comboScan->currentIndex();
  m_worker->eosTableFilePath = m_lineEditEosPath->text();
  
  m_worker->initialGuessType = m_initialGuessType;
  m_worker->customGuessLowHigh = m_customGuessLowHigh;
  m_worker->customGuessHighLow = m_customGuessHighLow;

  m_worker->metropolisMode      = m_metropolisMode;
  m_worker->metropolisSteps     = m_metropolisSteps;
  m_worker->metropolisStepSigma = m_metropolisStepSigma;
  m_worker->metropolisT         = m_metropolisT;
  m_worker->metropolisRetries   = m_metropolisRetries;
  
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

  auto V = [this](QXYSeries *s, double v) { return absTransform(s, v); };

  if (m_tempIsVertical) {
    m_seriesnB->append(V(m_seriesnB, pt.nB), pt.T);
    m_seriesS->append(V(m_seriesS, pt.s), pt.T);
    m_seriesnQ->append(V(m_seriesnQ, pt.nQ), pt.T);

    m_seriesNe->append(V(m_seriesNe, pt.ne), pt.T);
    m_seriesNmu->append(V(m_seriesNmu, pt.nmu), pt.T);
    m_seriesNtau->append(V(m_seriesNtau, pt.ntau), pt.T);
    m_seriesNnue->append(V(m_seriesNnue, pt.nnue), pt.T);
    m_seriesNnumu->append(V(m_seriesNnumu, pt.nnumu), pt.T);
    m_seriesNnutau->append(V(m_seriesNnutau, pt.nnutau), pt.T);

    m_seriesMuB->append(V(m_seriesMuB, pt.muB), pt.T);
    m_seriesMuQ->append(V(m_seriesMuQ, pt.muQ), pt.T);

    m_seriesMunue->append(V(m_seriesMunue, pt.munue), pt.T);
    m_seriesMunumu->append(V(m_seriesMunumu, pt.munumu), pt.T);
    m_seriesMnutau->append(V(m_seriesMnutau, pt.mnutau), pt.T);

    // Errors: Temperature horizontally by default
    m_seriesErrB->append(pt.T, V(m_seriesErrB, pt.err_b));
    m_seriesErrQ->append(pt.T, V(m_seriesErrQ, pt.err_charge));
    m_seriesErrLe->append(pt.T, V(m_seriesErrLe, pt.err_le));
    m_seriesErrLmu->append(pt.T, V(m_seriesErrLmu, pt.err_lmu));
    m_seriesErrLtau->append(pt.T, V(m_seriesErrLtau, pt.err_ltau));
  } else {
    m_seriesnB->append(pt.T, V(m_seriesnB, pt.nB));
    m_seriesS->append(pt.T, V(m_seriesS, pt.s));
    m_seriesnQ->append(pt.T, V(m_seriesnQ, pt.nQ));

    m_seriesNe->append(pt.T, V(m_seriesNe, pt.ne));
    m_seriesNmu->append(pt.T, V(m_seriesNmu, pt.nmu));
    m_seriesNtau->append(pt.T, V(m_seriesNtau, pt.ntau));
    m_seriesNnue->append(pt.T, V(m_seriesNnue, pt.nnue));
    m_seriesNnumu->append(pt.T, V(m_seriesNnumu, pt.nnumu));
    m_seriesNnutau->append(pt.T, V(m_seriesNnutau, pt.nnutau));

    m_seriesMuB->append(pt.T, V(m_seriesMuB, pt.muB));
    m_seriesMuQ->append(pt.T, V(m_seriesMuQ, pt.muQ));

    m_seriesMunue->append(pt.T, V(m_seriesMunue, pt.munue));
    m_seriesMunumu->append(pt.T, V(m_seriesMunumu, pt.munumu));
    m_seriesMnutau->append(pt.T, V(m_seriesMnutau, pt.mnutau));

    // Errors: Flipped
    m_seriesErrB->append(V(m_seriesErrB, pt.err_b), pt.T);
    m_seriesErrQ->append(V(m_seriesErrQ, pt.err_charge), pt.T);
    m_seriesErrLe->append(V(m_seriesErrLe, pt.err_le), pt.T);
    m_seriesErrLmu->append(V(m_seriesErrLmu, pt.err_lmu), pt.T);
    m_seriesErrLtau->append(V(m_seriesErrLtau, pt.err_ltau), pt.T);
  }

  // 3D trajectory: signed (μB, T, μQ) — Y is vertical.
  if (m_series3D && m_series3D->dataProxy()) {
    QScatterDataArray seg;
    if (m_continuous3D && m_trajectoryData.size() >= 2) {
      // Subdivide segment between previous and current step for a tube look.
      constexpr int kSubdiv = 20;
      const auto &prev = m_trajectoryData[m_trajectoryData.size() - 2];
      seg.reserve(kSubdiv);
      for (int k = 1; k <= kSubdiv; ++k) {
        const float t = float(k) / float(kSubdiv);
        seg.append(QScatterDataItem(QVector3D(
            static_cast<float>(prev.muB * (1.0 - t) + pt.muB * t),
            static_cast<float>(prev.T   * (1.0 - t) + pt.T   * t),
            static_cast<float>(prev.muQ * (1.0 - t) + pt.muQ * t))));
      }
    } else {
      // Plain raw point — most precise, no interpolation.
      seg.append(QScatterDataItem(QVector3D(static_cast<float>(pt.muB),
                                              static_cast<float>(pt.T),
                                              static_cast<float>(pt.muQ))));
    }
    m_series3D->dataProxy()->addItems(seg);
  }

  updateChartAxes();
}

void MainWindow::updateChartAxes() {
  if (m_trajectoryData.isEmpty()) return;

  double minT = m_trajectoryData.first().T;
  double maxT = m_trajectoryData.first().T;
  
  double minDens = 1e99, maxDens = -1e99;
  double minLepDens = 1e99, maxLepDens = -1e99;
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

    minLepDens = std::min({minLepDens, val(p.ne), val(p.nmu), val(p.ntau), val(p.nnue), val(p.nnumu), val(p.nnutau)});
    maxLepDens = std::max({maxLepDens, val(p.ne), val(p.nmu), val(p.ntau), val(p.nnue), val(p.nnumu), val(p.nnutau)});

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
    setRange(m_lepDensAxisY, minT, maxT, false);
    setRange(m_muAxisY,   minT, maxT, false);
    setRange(m_lepAxisY,  minT, maxT, false);

    setRange(m_densAxisX, minDens, maxDens, true);
    setRange(m_lepDensAxisX, minLepDens, maxLepDens, true);
    setRange(m_muAxisX,   minMu,   maxMu,   true);
    setRange(m_lepAxisX,  minLep,  maxLep,  true);

    setRange(m_errAxisX, minT, maxT, false);
    setRange(m_errAxisY, minErr, maxErr, true);
  } else {
    setRange(m_densAxisX, minT, maxT, false);
    setRange(m_lepDensAxisX, minT, maxT, false);
    setRange(m_muAxisX,   minT, maxT, false);
    setRange(m_lepAxisX,  minT, maxT, false);

    setRange(m_densAxisY, minDens, maxDens, true);
    setRange(m_lepDensAxisY, minLepDens, maxLepDens, true);
    setRange(m_muAxisY,   minMu,   maxMu,   true);
    setRange(m_lepAxisY,  minLep,  maxLep,  true);

    setRange(m_errAxisX, minErr, maxErr, true);
    setRange(m_errAxisY, minT,   maxT,   false);
  }
}

void MainWindow::onLogMessage(const QString &msg) {
  if (msg.contains("<font ") || msg.contains("<span ")) {
    m_console->append(msg);
  } else {
    // Explicitly wrap in white to prevent color bleeding from previous tags
    m_console->append("<span style=\"color:#ffffff;\">" + msg + "</span>");
  }
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
  m_leptonDensChartView->chart()->setTheme(newTheme);
  m_muChartView->chart()->setTheme(newTheme);
  m_leptonChartView->chart()->setTheme(newTheme);
  m_errorChartView->chart()->setTheme(newTheme);

  // setThemes resets series colors, so we restore them
  m_seriesnB->setColor(Qt::blue);
  m_seriesS->setColor(Qt::green);
  m_seriesnQ->setColor(QColor(255, 165, 0)); // Orange
  
  m_seriesNe->setColor(QColor(0, 0, 128)); // Navy
  m_seriesNmu->setColor(QColor(139, 0, 0)); // Dark red
  m_seriesNtau->setColor(QColor(85, 107, 47)); // Dark olive green
  m_seriesNnue->setColor(QColor(0, 128, 128));   // Teal
  m_seriesNnumu->setColor(QColor(153, 50, 204)); // Dark orchid
  m_seriesNnutau->setColor(QColor(255, 140, 0)); // Dark orange

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

  // Also update 3D plot theme
  if (m_scatter3D) {
    auto *theme = m_scatter3D->activeTheme();
    if (newTheme == QChart::ChartThemeLight) {
      theme->setBackgroundColor(Qt::white);
      theme->setWindowColor(Qt::white);
      theme->setLabelTextColor(Qt::black);
    } else {
      theme->setBackgroundColor(QColor("#2b2b2b"));
      theme->setWindowColor(QColor("#2b2b2b"));
      theme->setLabelTextColor(Qt::white);
    }
  }
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
  updateTitles(m_lepDensAxisX, m_lepDensAxisY, "Densities [MeV^3]");
  updateTitles(m_muAxisX, m_muAxisY, "Chem Pot [MeV] (abs)");
  updateTitles(m_lepAxisX, m_lepAxisY, "Chem Pot [MeV] (abs)");
  // Error plot flips opposite to others
  m_errAxisX->setTitleText(m_tempIsVertical ? "Temperature [MeV]" : "Relative Error");
  m_errAxisY->setTitleText(m_tempIsVertical ? "Relative Error" : "Temperature [MeV]");

  // Re-plot data by rebuilding series and keeping underlying memory
  clearCharts(true);

  auto V = [this](QXYSeries *s, double v) { return absTransform(s, v); };

  for (const auto &pt : m_trajectoryData) {
    if (m_tempIsVertical) {
      m_seriesnB->append(V(m_seriesnB, pt.nB), pt.T);
      m_seriesS->append(V(m_seriesS, pt.s), pt.T);
      m_seriesnQ->append(V(m_seriesnQ, pt.nQ), pt.T);
      m_seriesNe->append(V(m_seriesNe, pt.ne), pt.T);
      m_seriesNmu->append(V(m_seriesNmu, pt.nmu), pt.T);
      m_seriesNtau->append(V(m_seriesNtau, pt.ntau), pt.T);
      m_seriesNnue->append(V(m_seriesNnue, pt.nnue), pt.T);
      m_seriesNnumu->append(V(m_seriesNnumu, pt.nnumu), pt.T);
      m_seriesNnutau->append(V(m_seriesNnutau, pt.nnutau), pt.T);
      m_seriesMuB->append(V(m_seriesMuB, pt.muB), pt.T);
      m_seriesMuQ->append(V(m_seriesMuQ, pt.muQ), pt.T);
      m_seriesMunue->append(V(m_seriesMunue, pt.munue), pt.T);
      m_seriesMunumu->append(V(m_seriesMunumu, pt.munumu), pt.T);
      m_seriesMnutau->append(V(m_seriesMnutau, pt.mnutau), pt.T);
      // Errors: Temp on X
      m_seriesErrB->append(pt.T, V(m_seriesErrB, pt.err_b));
      m_seriesErrQ->append(pt.T, V(m_seriesErrQ, pt.err_charge));
      m_seriesErrLe->append(pt.T, V(m_seriesErrLe, pt.err_le));
      m_seriesErrLmu->append(pt.T, V(m_seriesErrLmu, pt.err_lmu));
      m_seriesErrLtau->append(pt.T, V(m_seriesErrLtau, pt.err_ltau));
    } else {
      m_seriesnB->append(pt.T, V(m_seriesnB, pt.nB));
      m_seriesS->append(pt.T, V(m_seriesS, pt.s));
      m_seriesnQ->append(pt.T, V(m_seriesnQ, pt.nQ));
      m_seriesNe->append(pt.T, V(m_seriesNe, pt.ne));
      m_seriesNmu->append(pt.T, V(m_seriesNmu, pt.nmu));
      m_seriesNtau->append(pt.T, V(m_seriesNtau, pt.ntau));
      m_seriesNnue->append(pt.T, V(m_seriesNnue, pt.nnue));
      m_seriesNnumu->append(pt.T, V(m_seriesNnumu, pt.nnumu));
      m_seriesNnutau->append(pt.T, V(m_seriesNnutau, pt.nnutau));
      m_seriesMuB->append(pt.T, V(m_seriesMuB, pt.muB));
      m_seriesMuQ->append(pt.T, V(m_seriesMuQ, pt.muQ));
      m_seriesMunue->append(pt.T, V(m_seriesMunue, pt.munue));
      m_seriesMunumu->append(pt.T, V(m_seriesMunumu, pt.munumu));
      m_seriesMnutau->append(pt.T, V(m_seriesMnutau, pt.mnutau));
      // Errors: Flipped
      m_seriesErrB->append(V(m_seriesErrB, pt.err_b), pt.T);
      m_seriesErrQ->append(V(m_seriesErrQ, pt.err_charge), pt.T);
      m_seriesErrLe->append(V(m_seriesErrLe, pt.err_le), pt.T);
      m_seriesErrLmu->append(V(m_seriesErrLmu, pt.err_lmu), pt.T);
      m_seriesErrLtau->append(V(m_seriesErrLtau, pt.err_ltau), pt.T);
    }
  }

  rebuild3DSeriesFromTrajectory();

  refreshSeriesNames();
  refreshLegendVisibility();
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
    m_seriesNnue->attachAxis(m_densAxisX);   m_seriesNnue->attachAxis(m_densAxisY);
    m_seriesNnumu->attachAxis(m_densAxisX);  m_seriesNnumu->attachAxis(m_densAxisY);
    m_seriesNnutau->attachAxis(m_densAxisX); m_seriesNnutau->attachAxis(m_densAxisY);

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
    } else {
        refreshSeriesNames();
    }

    // Sync abs-checkbox enabled state with the new scale
    auto syncAbsEnabled = [this](QCheckBox *c){ if (c) c->setEnabled(!m_isLogScale); };
    syncAbsEnabled(m_absnB);  syncAbsEnabled(m_absS);   syncAbsEnabled(m_absnQ);
    syncAbsEnabled(m_absNe);  syncAbsEnabled(m_absNmu); syncAbsEnabled(m_absNtau);
    syncAbsEnabled(m_absNnue);syncAbsEnabled(m_absNnumu); syncAbsEnabled(m_absNnutau);
    syncAbsEnabled(m_absMuB); syncAbsEnabled(m_absMuQ);
    syncAbsEnabled(m_absMunue); syncAbsEnabled(m_absMunumu); syncAbsEnabled(m_absMnutau);
    syncAbsEnabled(m_absErrB); syncAbsEnabled(m_absErrQ);
    syncAbsEnabled(m_absErrLe); syncAbsEnabled(m_absErrLmu); syncAbsEnabled(m_absErrLtau);

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
      out << "T\tnB\ts\t|nQ|\tnnue\tnnumu\tnnutau\n";
      for (const auto &pt : m_trajectoryData) out << pt.T << "\t" << pt.nB << "\t" << pt.s << "\t" << pt.nQ << "\t" << pt.nnue << "\t" << pt.nnumu << "\t" << pt.nnutau << "\n";
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
  out << "T\tmuB\tmuQ\tmunue\tmunumu\tmnutau\tnB\tnQ\ts_QCD\ts_tot\tne\tnmu\tntau\tnnue\tnnumu\tnnutau\n";
  
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

void MainWindow::onSolverSettingsButtonClicked() {
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

    // ── Convergence settings ──────────────────────────────────────────
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

    colLeft->addWidget(groupConv);

    // ── Guess method ─────────────────────────────────────────────────
    QGroupBox *groupGuess = new QGroupBox("Initial Guess Strategy", m_solverSettingsDialog);
    QVBoxLayout *vboxGuess = new QVBoxLayout(groupGuess);

    QComboBox *comboMethod = new QComboBox(m_solverSettingsDialog);
    comboMethod->addItems({"Simple (0)", "Linear Extrap (1)"});
    comboMethod->setCurrentIndex(m_guessMethod);
    vboxGuess->addWidget(new QLabel("Guess Propagation Method:"));
    vboxGuess->addWidget(comboMethod);

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
        "Number of independent Metropolis chains run before giving up on a "
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

    // ── Save button ───────────────────────────────────────────────────
    QPushButton *btnSave = new QPushButton("Save and Close", m_solverSettingsDialog);
    connect(btnSave, &QPushButton::clicked, [=]() {
      m_tolerance       = spinTol->value();
      m_maxIter         = spinMaxIter->value();
      m_guessMethod     = comboMethod->currentIndex();
      m_initialGuessType = comboType->currentIndex();
      m_customGuessLowHigh = {lh_muB->value(), lh_muQ->value(), lh_munue->value(), lh_munumu->value(), lh_mnutau->value()};
      m_customGuessHighLow = {hl_muB->value(), hl_muQ->value(), hl_munue->value(), hl_munumu->value(), hl_mnutau->value()};
      m_metropolisMode      = comboMetroMode->currentIndex();
      m_metropolisSteps     = spinMetroSteps->value();
      m_metropolisStepSigma = spinMetroSigma->value();
      m_metropolisT         = spinMetroT->value();
      m_metropolisRetries   = spinMetroRetries->value();
      m_solverSettingsDialog->accept();
    });
    vbox->addWidget(btnSave);
  }
  // Re-sync spinboxes to current state each time dialog is opened
  m_solverSettingsDialog->show();
  m_solverSettingsDialog->raise();
  m_solverSettingsDialog->activateWindow();
}

// ── Per-series abs/legend helpers ─────────────────────────────────────────
void MainWindow::registerSeriesAbs(QXYSeries *s, const QString &baseName, bool defaultAbs) {
  m_seriesMeta.append({s, baseName, defaultAbs});
}

double MainWindow::absTransform(QXYSeries *s, double v) const {
  // In log scale, abs is forced regardless of user choice (negative values
  // can't be plotted on a log axis).
  if (m_isLogScale) return std::max(std::abs(v), 1e-15);
  for (const auto &m : m_seriesMeta) {
    if (m.series == s) return m.useAbs ? std::abs(v) : v;
  }
  return v;
}

QString MainWindow::legendSuffix() const {
  if (!m_runParamsValid) return QString();
  QStringList parts;
  if (m_legendShowB)    parts << QString("B=%1").arg(m_runB,    0, 'g', 4);
  if (m_legendShowLe)   parts << QString("Le=%1").arg(m_runLe,  0, 'g', 4);
  if (m_legendShowLmu)  parts << QString("Lμ=%1").arg(m_runLmu, 0, 'g', 4);
  if (m_legendShowLtau) parts << QString("Lτ=%1").arg(m_runLtau,0, 'g', 4);
  if (parts.isEmpty()) return QString();
  return QString(" (%1)").arg(parts.join(", "));
}

void MainWindow::refreshSeriesNames() {
  const QString suffix = legendSuffix();
  for (const auto &m : m_seriesMeta) {
    if (!m.series) continue;
    // In log scale we always show abs (forced); in linear we follow useAbs.
    const bool showBars = m_isLogScale || m.useAbs;
    const QString display = showBars ? QString("|%1|").arg(m.baseName) : m.baseName;
    m.series->setName(display + suffix);
  }
  // Critical-point scatter series carry suffix too if visible
  if (m_seriesCpB) m_seriesCpB->setName(QString("CP |μB|") + suffix);
  if (m_seriesCpQ) m_seriesCpQ->setName(QString("CP |μQ|") + suffix);
}

void MainWindow::refreshLegendVisibility() {
  TooltipChartView *views[] = {m_densityChartView, m_leptonDensChartView,
                                m_muChartView, m_leptonChartView, m_errorChartView};
  for (auto *v : views) {
    if (v && v->chart()) v->chart()->legend()->setVisible(m_legendVisible);
  }
}

// ── First-order surface overlay (Entropy Contour EoS) ────────────────────
void MainWindow::loadFirstOrderSurface() {
  if (!m_surface3D) return;
  const int eos = m_comboEos ? m_comboEos->currentIndex() : 3;
  // Skip if already loaded for this EoS variant.
  if (m_surfaceLoaded && m_surfaceLoadedEos == eos) return;

  // Pick the surface file matching the selected EoS. Both files share the
  // same column layout (T, muB, muS, muQ, dnB).
  const QString fileName = (eos == 4) ? "assets/first_order_surface_param.dat"
                                       : "assets/first_order_surface.dat";

  // Files live next to the project root. The application is launched from
  // gui/build/, so go up two levels.
  QDir dir(QCoreApplication::applicationDirPath());
  dir.cdUp(); dir.cdUp();
  const QString path = dir.absoluteFilePath(fileName);

  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QMessageBox::warning(this, "Surface Data",
        QString("Could not open %1").arg(path));
    return;
  }
  QTextStream in(&f);
  QScatterDataArray *arr = new QScatterDataArray;
  arr->reserve(20000);
  while (!in.atEnd()) {
    QString line = in.readLine().trimmed();
    if (line.isEmpty() || line.startsWith('#')) continue;
    const QStringList toks = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    // Columns: T  muB  muS  muQ  dnB
    if (toks.size() < 4) continue;
    bool okT = false, okB = false, okQ = false;
    const double T   = toks[0].toDouble(&okT);
    const double muB = toks[1].toDouble(&okB);
    const double muQ = toks[3].toDouble(&okQ);
    if (!okT || !okB || !okQ) continue;
    arr->append(QScatterDataItem(QVector3D(static_cast<float>(muB),
                                            static_cast<float>(T),
                                            static_cast<float>(muQ))));
  }
  f.close();
  m_surface3D->dataProxy()->resetArray(arr);
  m_surfaceLoaded = true;
  m_surfaceLoadedEos = eos;
  applySurfaceColor();
  onLogMessage(QString("Loaded first-order surface (%1): %2 points.")
                   .arg(fileName).arg(arr->size()));
}

void MainWindow::applySurfaceColor() {
  if (!m_surface3D || !m_sliderSurfaceOpacity) return;
  const int alpha = m_sliderSurfaceOpacity->value();
  QColor c(80, 160, 255, alpha);
  m_surface3D->setBaseColor(c);
}

void MainWindow::rebuild3DSeriesFromTrajectory() {
  if (!m_series3D || !m_series3D->dataProxy()) return;
  QScatterDataArray *arr = new QScatterDataArray;
  if (!m_trajectoryData.isEmpty()) {
    if (m_continuous3D) {
      constexpr int kSubdiv = 20;
      arr->reserve(static_cast<int>(m_trajectoryData.size()) * kSubdiv);
      const auto &p0 = m_trajectoryData.first();
      arr->append(QScatterDataItem(QVector3D(static_cast<float>(p0.muB),
                                              static_cast<float>(p0.T),
                                              static_cast<float>(p0.muQ))));
      for (int i = 1; i < m_trajectoryData.size(); ++i) {
        const auto &a = m_trajectoryData[i - 1];
        const auto &b = m_trajectoryData[i];
        for (int k = 1; k <= kSubdiv; ++k) {
          const float t = float(k) / float(kSubdiv);
          arr->append(QScatterDataItem(QVector3D(
              static_cast<float>(a.muB * (1.0 - t) + b.muB * t),
              static_cast<float>(a.T   * (1.0 - t) + b.T   * t),
              static_cast<float>(a.muQ * (1.0 - t) + b.muQ * t))));
        }
      }
    } else {
      // Raw points only — most precise.
      arr->reserve(static_cast<int>(m_trajectoryData.size()));
      for (const auto &p : m_trajectoryData) {
        arr->append(QScatterDataItem(QVector3D(static_cast<float>(p.muB),
                                                static_cast<float>(p.T),
                                                static_cast<float>(p.muQ))));
      }
    }
  }
  m_series3D->dataProxy()->resetArray(arr);
}
