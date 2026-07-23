// SPDX-FileCopyrightText: 2026 Rao Jing
// SPDX-License-Identifier: GPL-3.0-only

#include "MainWindow.h"

#include <QApplication>
#include <QAbstractItemView>
#include <QChart>
#include <QChartView>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QLabel>
#include <QLineSeries>
#include <QMessageBox>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSqlError>
#include <QSqlQuery>
#include <QStatusBar>
#include <QStringConverter>
#include <QStringList>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QVariant>
#include <QValueAxis>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace {
constexpr int kHistoryLimit = 120;
constexpr int kChartWindow = 120;
constexpr int kSqliteApplicationId = 0x44474149; // DGAI

QString authorshipJsonText()
{
    QFile file(":/meta/authorship.json");
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString::fromUtf8(file.readAll());
    }

    return R"({"product":"DriveGuard-AI","author":"Rao Jing","author_cn":"饶晶","identity":"DGAI-RJ-2026","authorship_notice_cn":"由饶晶制作。"})";
}

QJsonObject authorshipObject()
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(authorshipJsonText().toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        return {};
    }
    return doc.object();
}

QString metaValue(const QString &key, const QString &fallback)
{
    const QJsonObject meta = authorshipObject();
    const QString value = meta.value(key).toString();
    return value.isEmpty() ? fallback : value;
}

QString authorName()
{
    return metaValue("author_cn", "饶晶");
}

QString authorRomanName()
{
    return metaValue("author", "Rao Jing");
}

QString authorshipNotice()
{
    return metaValue("authorship_notice_cn", "由饶晶制作。");
}

QString productIdentity()
{
    return metaValue("identity", "DGAI-RJ-2026");
}

QString authorshipHash()
{
    return QString::fromLatin1(
        QCryptographicHash::hash(authorshipJsonText().toUtf8(), QCryptographicHash::Sha256).toHex()
    ).toUpper();
}

QString shortAuthorshipHash()
{
    return authorshipHash().left(12);
}

QString brandSignature()
{
    return QString("制作：%1 | %2 | SHA-256 %3")
        .arg(authorName(), productIdentity(), shortAuthorshipHash());
}

bool isPythonLauncher(const QString &python)
{
    const QString name = QFileInfo(python).fileName();
    return name.compare("py.exe", Qt::CaseInsensitive) == 0
        || name.compare("py", Qt::CaseInsensitive) == 0;
}

QStringList pythonRuntimeProbeArgs(const QString &python)
{
    QStringList args;
    if (isPythonLauncher(python)) {
        args << "-3";
    }
    args << "-c" << "import cv2, numpy; print('driveguard-python-ok')";
    return args;
}

bool pythonCanRunDetector(const QString &python, QString *detail = nullptr)
{
    if (python.isEmpty()) {
        if (detail) {
            *detail = "Python path is empty";
        }
        return false;
    }

    QProcess probe;
    probe.setProcessChannelMode(QProcess::MergedChannels);
    probe.start(python, pythonRuntimeProbeArgs(python));
    if (!probe.waitForStarted(2500)) {
        if (detail) {
            *detail = probe.errorString();
        }
        return false;
    }
    if (!probe.waitForFinished(7000)) {
        probe.kill();
        probe.waitForFinished(1000);
        if (detail) {
            *detail = "Python dependency probe timed out";
        }
        return false;
    }
    const QString output = QString::fromUtf8(probe.readAll()).trimmed();
    if (probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0) {
        if (detail) {
            *detail = output.isEmpty() ? probe.errorString() : output;
        }
        return false;
    }
    return true;
}

QLabel *makeTitle(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName("sectionTitle");
    label->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    return label;
}

QLabel *makeMetricLabel()
{
    auto *label = new QLabel("--");
    label->setObjectName("metricValue");
    label->setMinimumWidth(118);
    return label;
}

QString fmt(double value, int precision = 2)
{
    return QString::number(value, 'f', precision);
}

QString csvEscape(const QString &value)
{
    QString escaped = value;
    escaped.replace("\"", "\"\"");
    if (escaped.contains(',') || escaped.contains('"') || escaped.contains('\n')) {
        return "\"" + escaped + "\"";
    }
    return escaped;
}

QString htmlEscape(QString value)
{
    value.replace("&", "&amp;");
    value.replace("<", "&lt;");
    value.replace(">", "&gt;");
    value.replace("\"", "&quot;");
    value.replace("\n", "<br>");
    return value;
}

QString htmlImageDataUri(const QString &path)
{
    if (path.isEmpty()) {
        return {};
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QByteArray data = file.readAll().toBase64();
    const QString suffix = QFileInfo(path).suffix().toLower();
    const QString mime = suffix == "png" ? "image/png" : "image/jpeg";
    return QString("data:%1;base64,%2").arg(mime, QString::fromLatin1(data));
}

void ensureColumn(QSqlDatabase &db, const QString &table, const QString &column, const QString &definition)
{
    QSqlQuery info(db);
    if (!info.exec(QString("PRAGMA table_info(%1)").arg(table))) {
        return;
    }

    while (info.next()) {
        if (info.value(1).toString() == column) {
            return;
        }
    }

    QSqlQuery alter(db);
    alter.exec(QString("ALTER TABLE %1 ADD COLUMN %2 %3").arg(table, column, definition));
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent),
      m_detector(new QProcess(this))
{
    buildUi();
    applyTheme();
    initDatabase();
    setupVoiceAlerts();
    loadHistory();
    loadSafetyEvents();
    refreshReportSummary();

    connect(m_detector, &QProcess::readyReadStandardOutput, this, &MainWindow::readDetectorOutput);
    connect(m_detector, &QProcess::readyReadStandardError, this, &MainWindow::readDetectorError);
    connect(m_detector, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &MainWindow::handleDetectorFinished);
    connect(m_detector, &QProcess::errorOccurred, this, &MainWindow::handleDetectorError);
    connect(m_detector, &QProcess::started, this, &MainWindow::handleDetectorStarted);
    connect(m_detector, &QProcess::stateChanged, this, &MainWindow::handleDetectorStateChanged);

    m_dbWriteClock.start();
    m_alarmClock.start();
    m_eventClock.start();
    m_voiceClock.start();
    appendLog("系统就绪。建议先启动模拟模式，再测试摄像头或视频文件。");
}

MainWindow::~MainWindow()
{
    stopDetector();
    if (m_db.isOpen()) {
        m_db.close();
    }
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    refreshVideoPixmap();
}

void MainWindow::buildUi()
{
    setWindowTitle("DriveGuard-AI 疲劳驾驶智能预警系统");
    resize(1500, 900);
    setMinimumSize(1180, 760);

    m_tabs = new QTabWidget(this);
    m_tabs->setObjectName("mainTabs");
    m_tabs->addTab(createMonitorPage(), "实时监测");
    m_tabs->addTab(createEventsPage(), "事件中心");
    m_tabs->addTab(createReportPage(), "报告中心");
    m_tabs->addTab(createHealthPage(), "设备健康");
    m_tabs->addTab(createCalibrationPage(), "个体校准");
    m_tabs->addTab(createHistoryPage(), "历史记录");
    setCentralWidget(m_tabs);

    statusBar()->setObjectName("brandStatusBar");
    statusBar()->showMessage(brandSignature());
    statusBar()->setToolTip("内嵌作者身份 SHA-256：" + authorshipHash());
}

QWidget *MainWindow::createMonitorPage()
{
    auto *scroll = new QScrollArea;
    scroll->setObjectName("monitorScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto *page = new QWidget;
    page->setObjectName("monitorPage");
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(18, 16, 18, 18);
    root->setSpacing(12);

    auto *header = new QHBoxLayout;
    auto *titleBlock = new QVBoxLayout;
    auto *title = new QLabel("DriveGuard-AI");
    title->setObjectName("appTitle");
    auto *subtitle = new QLabel("非接触式疲劳驾驶检测 | 视觉特征 + PERCLOS + 多级预警");
    subtitle->setObjectName("subtitle");
    auto *identity = new QLabel(brandSignature());
    identity->setObjectName("identityChip");
    titleBlock->addWidget(title);
    titleBlock->addWidget(subtitle);
    titleBlock->addWidget(identity);
    header->addLayout(titleBlock);
    header->addStretch();
    header->addWidget(createControlBar());
    root->addLayout(header);

    auto *main = new QHBoxLayout;
    main->setSpacing(16);

    auto *videoPanel = new QFrame;
    videoPanel->setObjectName("videoPanel");
    videoPanel->setMaximumHeight(430);
    videoPanel->setMaximumWidth(1120);
    auto *videoLayout = new QVBoxLayout(videoPanel);
    videoLayout->setContentsMargins(14, 14, 14, 14);
    videoLayout->setSpacing(10);
    auto *videoTitle = new QHBoxLayout;
    videoTitle->addWidget(makeTitle("实时画面"));
    videoTitle->addStretch();
    m_modeLabel = new QLabel("未启动");
    m_modeLabel->setObjectName("modeBadge");
    m_modeLabel->setMaximumHeight(34);
    m_modeLabel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    videoTitle->addWidget(m_modeLabel);
    videoLayout->addLayout(videoTitle);

    m_videoLabel = new QLabel("等待视频流");
    m_videoLabel->setObjectName("videoLabel");
    m_videoLabel->setAlignment(Qt::AlignCenter);
    m_videoLabel->setMinimumSize(460, 259);
    m_videoLabel->setMaximumHeight(285);
    m_videoLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    videoLayout->addWidget(m_videoLabel, 1);

    m_alarmLabel = new QLabel("安全监测待启动");
    m_alarmLabel->setObjectName("alarmLabel");
    m_alarmLabel->setAlignment(Qt::AlignCenter);
    m_alarmLabel->setMinimumHeight(46);
    m_alarmLabel->setMaximumHeight(54);
    m_alarmLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    videoLayout->addWidget(m_alarmLabel);
    main->addWidget(videoPanel, 3);

    auto *sideScroll = new QScrollArea;
    sideScroll->setObjectName("sideScroll");
    sideScroll->setWidgetResizable(true);
    sideScroll->setFrameShape(QFrame::NoFrame);
    sideScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    sideScroll->setMaximumHeight(430);
    sideScroll->setMinimumWidth(360);
    sideScroll->setMaximumWidth(420);

    auto *sideContent = new QWidget;
    sideContent->setObjectName("sideContent");
    auto *side = new QVBoxLayout(sideContent);
    side->setContentsMargins(0, 0, 0, 0);
    side->setSpacing(12);
    side->addWidget(createStatusCard());
    side->addWidget(createAiInsightCard());
    side->addWidget(createMetricsCard());
    side->addStretch();
    sideScroll->setWidget(sideContent);
    main->addWidget(sideScroll, 2);

    root->addLayout(main, 0);

    auto *chartPanel = new QFrame;
    chartPanel->setObjectName("chartPanel");
    auto *chartLayout = new QVBoxLayout(chartPanel);
    chartLayout->setContentsMargins(14, 12, 14, 14);
    chartLayout->addWidget(makeTitle("疲劳趋势"));

    m_scoreSeries = new QLineSeries(this);
    m_scoreSeries->setName("疲劳评分");
    m_scoreSeries->setColor(QColor("#0284c7"));
    m_perclosSeries = new QLineSeries(this);
    m_perclosSeries->setName("PERCLOS x100");
    m_perclosSeries->setColor(QColor("#84cc16"));

    m_chart = new QChart;
    m_chart->addSeries(m_scoreSeries);
    m_chart->addSeries(m_perclosSeries);
    m_chart->legend()->setVisible(true);
    m_chart->legend()->setAlignment(Qt::AlignBottom);
    m_chart->setMargins(QMargins(0, 0, 0, 0));
    m_chart->setBackgroundVisible(false);

    m_axisX = new QValueAxis(this);
    m_axisY = new QValueAxis(this);
    m_axisX->setRange(0, kChartWindow);
    m_axisX->setLabelFormat("%d");
    m_axisY->setRange(0, 100);
    m_axisY->setLabelFormat("%d");
    m_axisX->setTitleText("样本");
    m_axisY->setTitleText("指标");
    m_axisX->setGridLineColor(QColor("#d8e2ea"));
    m_axisY->setGridLineColor(QColor("#d8e2ea"));
    m_axisX->setLabelsColor(QColor("#52616f"));
    m_axisY->setLabelsColor(QColor("#52616f"));
    m_axisX->setTitleBrush(QColor("#1f2d3a"));
    m_axisY->setTitleBrush(QColor("#1f2d3a"));

    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);
    m_scoreSeries->attachAxis(m_axisX);
    m_scoreSeries->attachAxis(m_axisY);
    m_perclosSeries->attachAxis(m_axisX);
    m_perclosSeries->attachAxis(m_axisY);

    m_chartView = new QChartView(m_chart);
    m_chartView->setObjectName("chartView");
    m_chartView->setRenderHint(QPainter::Antialiasing);
    m_chartView->setMinimumHeight(230);
    m_chartView->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    chartLayout->addWidget(m_chartView);
    chartPanel->setMinimumHeight(275);
    root->addWidget(chartPanel, 0);

    auto *footer = new QLabel(brandSignature() + " | 本地检测证据自动写入作者水印与完整性元数据");
    footer->setObjectName("identityFooter");
    footer->setAlignment(Qt::AlignCenter);
    root->addWidget(footer);

    scroll->setWidget(page);
    return scroll;
}

QWidget *MainWindow::createEventsPage()
{
    auto *page = new QWidget;
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(12);

    auto *top = new QHBoxLayout;
    auto *titleBlock = new QVBoxLayout;
    auto *title = new QLabel("安全事件中心");
    title->setObjectName("appTitle");
    auto *subtitle = new QLabel("自动沉淀疲劳、遮挡、无效画面等风险事件，形成可复盘证据链");
    subtitle->setObjectName("subtitle");
    titleBlock->addWidget(title);
    titleBlock->addWidget(subtitle);
    top->addLayout(titleBlock);
    top->addStretch();
    auto *refreshButton = new QPushButton("刷新事件");
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::loadSafetyEvents);
    top->addWidget(refreshButton);
    auto *ackButton = new QPushButton("确认事件");
    connect(ackButton, &QPushButton::clicked, this, &MainWindow::acknowledgeSelectedEvent);
    top->addWidget(ackButton);
    auto *falseButton = new QPushButton("标记误报");
    falseButton->setObjectName("continueButton");
    connect(falseButton, &QPushButton::clicked, this, &MainWindow::markSelectedEventFalsePositive);
    top->addWidget(falseButton);
    root->addLayout(top);

    auto *main = new QHBoxLayout;
    main->setSpacing(14);

    m_eventTable = new QTableWidget(0, 9);
    m_eventTable->setObjectName("eventTable");
    m_eventTable->setHorizontalHeaderLabels({
        "ID", "时间", "类型", "等级", "评分", "状态", "来源", "主因子", "证据"
    });
    m_eventTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_eventTable->verticalHeader()->setVisible(false);
    m_eventTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_eventTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_eventTable, &QTableWidget::itemSelectionChanged,
            this, &MainWindow::handleEventSelectionChanged);
    main->addWidget(m_eventTable, 3);

    auto *detail = new QFrame;
    detail->setObjectName("statusCard");
    detail->setMinimumWidth(340);
    auto *detailLayout = new QVBoxLayout(detail);
    detailLayout->setContentsMargins(14, 14, 14, 14);
    detailLayout->setSpacing(10);
    detailLayout->addWidget(makeTitle("事件证据"));

    m_eventPreviewLabel = new QLabel("选择事件后显示证据截图");
    m_eventPreviewLabel->setObjectName("videoLabel");
    m_eventPreviewLabel->setAlignment(Qt::AlignCenter);
    m_eventPreviewLabel->setMinimumSize(320, 180);
    m_eventPreviewLabel->setMaximumHeight(240);
    detailLayout->addWidget(m_eventPreviewLabel);

    m_eventDetailLabel = new QLabel("事件尚未选择");
    m_eventDetailLabel->setObjectName("reasonText");
    m_eventDetailLabel->setWordWrap(true);
    detailLayout->addWidget(m_eventDetailLabel);
    detailLayout->addStretch();
    main->addWidget(detail, 1);

    root->addLayout(main, 1);
    return page;
}

QWidget *MainWindow::createReportPage()
{
    auto *page = new QWidget;
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(14);

    auto *top = new QHBoxLayout;
    auto *titleBlock = new QVBoxLayout;
    auto *title = new QLabel("报告中心");
    title->setObjectName("appTitle");
    auto *subtitle = new QLabel("汇总检测时长、风险分布、关键事件与证据截图，导出可交付 HTML 报告");
    subtitle->setObjectName("subtitle");
    titleBlock->addWidget(title);
    titleBlock->addWidget(subtitle);
    top->addLayout(titleBlock);
    top->addStretch();
    auto *refreshButton = new QPushButton("刷新统计");
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::refreshReportSummary);
    top->addWidget(refreshButton);
    auto *exportButton = new QPushButton("导出HTML报告");
    connect(exportButton, &QPushButton::clicked, this, &MainWindow::exportHtmlReport);
    top->addWidget(exportButton);
    root->addLayout(top);

    auto *grid = new QGridLayout;
    grid->setSpacing(12);
    auto makeCard = [](const QString &titleText, QLabel **valueLabel) {
        auto *card = new QFrame;
        card->setObjectName("statusCard");
        auto *layout = new QVBoxLayout(card);
        layout->setContentsMargins(16, 14, 16, 14);
        layout->addWidget(makeTitle(titleText));
        *valueLabel = new QLabel("--");
        (*valueLabel)->setObjectName("metricValue");
        (*valueLabel)->setMinimumHeight(34);
        layout->addWidget(*valueLabel);
        return card;
    };
    grid->addWidget(makeCard("事件总数", &m_totalEventsLabel), 0, 0);
    grid->addWidget(makeCard("高风险事件", &m_highRiskEventsLabel), 0, 1);
    grid->addWidget(makeCard("待确认事件", &m_pendingEventsLabel), 0, 2);
    root->addLayout(grid);

    m_reportSummaryLabel = new QLabel("等待统计数据");
    m_reportSummaryLabel->setObjectName("reasonText");
    m_reportSummaryLabel->setWordWrap(true);
    m_reportSummaryLabel->setMinimumHeight(220);
    root->addWidget(m_reportSummaryLabel, 1);
    return page;
}

QWidget *MainWindow::createHealthPage()
{
    auto *page = new QWidget;
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(14);

    auto *title = new QLabel("设备健康");
    title->setObjectName("appTitle");
    root->addWidget(title);
    auto *subtitle = new QLabel("面向交付现场的运行诊断：摄像头、模型、Python 子进程、数据库与画面质量");
    subtitle->setObjectName("subtitle");
    root->addWidget(subtitle);

    auto *card = new QFrame;
    card->setObjectName("aiCard");
    auto *layout = new QGridLayout(card);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setHorizontalSpacing(18);
    layout->setVerticalSpacing(12);
    auto makeHealthValue = []() {
        auto *label = new QLabel("--");
        label->setObjectName("aiValue");
        label->setWordWrap(true);
        return label;
    };
    m_healthProcessLabel = makeHealthValue();
    m_healthDbLabel = makeHealthValue();
    m_healthModeLabel = makeHealthValue();
    m_healthFrameLabel = makeHealthValue();
    m_healthQualityLabel = makeHealthValue();
    m_healthModelLabel = makeHealthValue();
    layout->addWidget(new QLabel("检测进程"), 0, 0);
    layout->addWidget(m_healthProcessLabel, 0, 1);
    layout->addWidget(new QLabel("SQLite"), 1, 0);
    layout->addWidget(m_healthDbLabel, 1, 1);
    layout->addWidget(new QLabel("运行模式"), 2, 0);
    layout->addWidget(m_healthModeLabel, 2, 1);
    layout->addWidget(new QLabel("最新画面"), 3, 0);
    layout->addWidget(m_healthFrameLabel, 3, 1);
    layout->addWidget(new QLabel("画面质量"), 4, 0);
    layout->addWidget(m_healthQualityLabel, 4, 1);
    layout->addWidget(new QLabel("模型链路"), 5, 0);
    layout->addWidget(m_healthModelLabel, 5, 1);
    root->addWidget(card);
    root->addStretch();
    return page;
}

QWidget *MainWindow::createCalibrationPage()
{
    auto *page = new QWidget;
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(14);

    auto *top = new QHBoxLayout;
    auto *titleBlock = new QVBoxLayout;
    auto *title = new QLabel("个体校准");
    title->setObjectName("appTitle");
    auto *subtitle = new QLabel("根据当前驾驶员稳定状态自适应 EAR/MAR 阈值，降低固定阈值误报");
    subtitle->setObjectName("subtitle");
    titleBlock->addWidget(title);
    titleBlock->addWidget(subtitle);
    top->addLayout(titleBlock);
    top->addStretch();
    auto *cameraButton = new QPushButton("启动摄像头校准");
    connect(cameraButton, &QPushButton::clicked, this, &MainWindow::startCamera);
    top->addWidget(cameraButton);
    root->addLayout(top);

    auto *card = new QFrame;
    card->setObjectName("statusCard");
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(18, 18, 18, 18);
    layout->setSpacing(12);
    layout->addWidget(makeTitle("校准进度"));
    m_calibrationPageBar = new QProgressBar;
    m_calibrationPageBar->setRange(0, 100);
    m_calibrationPageBar->setValue(0);
    layout->addWidget(m_calibrationPageBar);
    m_calibrationStateLabel = new QLabel("等待摄像头数据");
    m_calibrationStateLabel->setObjectName("metricValue");
    layout->addWidget(m_calibrationStateLabel);
    m_calibrationThresholdLabel = new QLabel("动态阈值：EAR 0.20 / MAR 0.62");
    m_calibrationThresholdLabel->setObjectName("aiValue");
    layout->addWidget(m_calibrationThresholdLabel);
    m_calibrationGuideLabel = new QLabel("校准流程：正脸保持 5 秒，正常眨眼，短暂张嘴，左右轻微转头。系统会在画面稳定时自动学习个体基线。");
    m_calibrationGuideLabel->setObjectName("reasonText");
    m_calibrationGuideLabel->setWordWrap(true);
    layout->addWidget(m_calibrationGuideLabel);
    root->addWidget(card);
    root->addStretch();
    return page;
}

QWidget *MainWindow::createHistoryPage()
{
    auto *page = new QWidget;
    auto *root = new QVBoxLayout(page);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(12);

    auto *top = new QHBoxLayout;
    top->addWidget(makeTitle("检测历史"));
    top->addStretch();
    auto *exportButton = new QPushButton("导出CSV");
    connect(exportButton, &QPushButton::clicked, this, &MainWindow::exportHistoryCsv);
    top->addWidget(exportButton);
    auto *refreshButton = new QPushButton("刷新记录");
    connect(refreshButton, &QPushButton::clicked, this, &MainWindow::loadHistory);
    top->addWidget(refreshButton);
    root->addLayout(top);

    m_historyTable = new QTableWidget(0, 12);
    m_historyTable->setHorizontalHeaderLabels({
        "时间", "来源", "等级", "评分", "注意力", "质量", "EAR", "MAR", "PERCLOS", "闭眼(s)", "原因", "证据"
    });
    m_historyTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_historyTable->verticalHeader()->setVisible(false);
    m_historyTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_historyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    root->addWidget(m_historyTable, 1);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(300);
    m_log->setMinimumHeight(130);
    m_log->setObjectName("logBox");
    root->addWidget(makeTitle("系统日志"));
    root->addWidget(m_log);

    return page;
}

QWidget *MainWindow::createStatusCard()
{
    auto *card = new QFrame;
    card->setObjectName("statusCard");
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(9);

    layout->addWidget(makeTitle("AI 决策内核"));

    m_statusBadge = new QLabel("未启动");
    m_statusBadge->setObjectName("statusBadge");
    m_statusBadge->setAlignment(Qt::AlignCenter);
    m_statusBadge->setMinimumHeight(48);
    layout->addWidget(m_statusBadge);

    auto *scoreRow = new QHBoxLayout;
    m_scoreLabel = new QLabel("0 / 100");
    m_scoreLabel->setObjectName("scoreText");
    scoreRow->addWidget(new QLabel("疲劳评分"));
    scoreRow->addStretch();
    scoreRow->addWidget(m_scoreLabel);
    layout->addLayout(scoreRow);

    m_scoreBar = new QProgressBar;
    m_scoreBar->setRange(0, 100);
    m_scoreBar->setTextVisible(false);
    layout->addWidget(m_scoreBar);

    m_reasonLabel = new QLabel("等待检测数据");
    m_reasonLabel->setObjectName("reasonText");
    m_reasonLabel->setWordWrap(true);
    m_reasonLabel->setMinimumHeight(58);
    layout->addWidget(m_reasonLabel);

    return card;
}

QWidget *MainWindow::createAiInsightCard()
{
    auto *card = new QFrame;
    card->setObjectName("aiCard");
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(7);

    layout->addWidget(makeTitle("AI 增强诊断"));

    auto makeValue = []() {
        auto *label = new QLabel("--");
        label->setObjectName("aiValue");
        label->setWordWrap(true);
        return label;
    };

    m_attentionLabel = makeValue();
    m_qualityLabel = makeValue();
    m_calibrationLabel = makeValue();
    m_thresholdLabel = makeValue();
    m_riskLabel = makeValue();
    m_snapshotLabel = makeValue();

    m_qualityBar = new QProgressBar;
    m_qualityBar->setObjectName("miniProgress");
    m_qualityBar->setRange(0, 100);
    m_qualityBar->setTextVisible(false);
    m_calibrationBar = new QProgressBar;
    m_calibrationBar->setObjectName("miniProgress");
    m_calibrationBar->setRange(0, 100);
    m_calibrationBar->setTextVisible(false);

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(12);
    grid->setVerticalSpacing(5);
    grid->addWidget(new QLabel("注意力"), 0, 0);
    grid->addWidget(m_attentionLabel, 0, 1);
    grid->addWidget(new QLabel("画面质量"), 1, 0);
    grid->addWidget(m_qualityLabel, 1, 1);
    grid->addWidget(m_qualityBar, 2, 0, 1, 2);
    grid->addWidget(new QLabel("个体校准"), 3, 0);
    grid->addWidget(m_calibrationLabel, 3, 1);
    grid->addWidget(m_calibrationBar, 4, 0, 1, 2);
    grid->addWidget(new QLabel("动态阈值"), 5, 0);
    grid->addWidget(m_thresholdLabel, 5, 1);
    grid->addWidget(new QLabel("主因子"), 6, 0);
    grid->addWidget(m_riskLabel, 6, 1);
    grid->addWidget(new QLabel("证据"), 7, 0);
    grid->addWidget(m_snapshotLabel, 7, 1);
    layout->addLayout(grid);

    return card;
}

QWidget *MainWindow::createMetricsCard()
{
    auto *card = new QFrame;
    card->setObjectName("metricsCard");
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(8);
    layout->addWidget(makeTitle("实时特征"));

    auto *grid = new QGridLayout;
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(7);

    m_earLabel = makeMetricLabel();
    m_marLabel = makeMetricLabel();
    m_perclosLabel = makeMetricLabel();
    m_blinkLabel = makeMetricLabel();
    m_yawnLabel = makeMetricLabel();
    m_eyeClosedLabel = makeMetricLabel();
    m_poseLabel = makeMetricLabel();

    grid->addWidget(new QLabel("EAR 眼部"), 0, 0);
    grid->addWidget(m_earLabel, 0, 1);
    grid->addWidget(new QLabel("MAR 嘴部"), 1, 0);
    grid->addWidget(m_marLabel, 1, 1);
    grid->addWidget(new QLabel("PERCLOS"), 2, 0);
    grid->addWidget(m_perclosLabel, 2, 1);
    grid->addWidget(new QLabel("眨眼频率"), 3, 0);
    grid->addWidget(m_blinkLabel, 3, 1);
    grid->addWidget(new QLabel("哈欠次数"), 4, 0);
    grid->addWidget(m_yawnLabel, 4, 1);
    grid->addWidget(new QLabel("连续闭眼"), 5, 0);
    grid->addWidget(m_eyeClosedLabel, 5, 1);
    grid->addWidget(new QLabel("头部姿态"), 6, 0);
    grid->addWidget(m_poseLabel, 6, 1);

    layout->addLayout(grid);
    return card;
}

QWidget *MainWindow::createControlBar()
{
    auto *bar = new QWidget;
    auto *layout = new QHBoxLayout(bar);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);

    m_continueButton = new QPushButton("继续");
    m_continueButton->setObjectName("continueButton");
    m_continueButton->setEnabled(false);
    m_simButton = new QPushButton("模拟模式");
    m_cameraButton = new QPushButton("摄像头");
    m_videoButton = new QPushButton("视频文件");
    auto *voiceTestButton = new QPushButton("测试语音");
    m_stopButton = new QPushButton("停止");
    m_stopButton->setObjectName("dangerButton");

    connect(m_simButton, &QPushButton::clicked, this, &MainWindow::startSimulation);
    connect(m_cameraButton, &QPushButton::clicked, this, &MainWindow::startCamera);
    connect(m_videoButton, &QPushButton::clicked, this, &MainWindow::openVideoFile);
    connect(voiceTestButton, &QPushButton::clicked, this, [this]() {
        FatigueSample sample;
        sample.level = "light";
        sample.score = 42;
        sample.reason = "语音链路自检";
        m_lastVoiceKey.clear();
        appendLog("语音提醒测试：轻度疲劳请注意。");
        playVoiceAlert(sample);
    });
    connect(m_continueButton, &QPushButton::clicked, this, &MainWindow::continueDetector);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::stopDetector);

    layout->addWidget(m_simButton);
    layout->addWidget(m_cameraButton);
    layout->addWidget(m_videoButton);
    layout->addWidget(voiceTestButton);
    layout->addWidget(m_continueButton);
    layout->addWidget(m_stopButton);
    return bar;
}

void MainWindow::applyTheme()
{
    setStyleSheet(R"(
        QMainWindow, QWidget {
            background: #edf2f6;
            color: #1b2733;
            font-family: "Microsoft YaHei UI", "Segoe UI", sans-serif;
            font-size: 13px;
        }
        #monitorScroll, #monitorPage, #sideScroll, #sideContent {
            background: #edf2f6;
        }
        #appTitle {
            font-size: 28px;
            font-weight: 900;
            color: #101820;
        }
        #subtitle {
            color: #65717d;
            font-size: 13px;
        }
        #identityChip {
            margin-top: 4px;
            padding: 5px 9px;
            border-radius: 5px;
            background: #111827;
            color: #dbeafe;
            font-size: 12px;
            font-weight: 900;
        }
        #identityFooter {
            padding: 9px 12px;
            border-radius: 6px;
            background: #101827;
            color: #dbeafe;
            font-size: 12px;
            font-weight: 900;
        }
        QStatusBar#brandStatusBar {
            background: #0f172a;
            color: #dbeafe;
            border-top: 1px solid #203044;
            font-weight: 900;
        }
        #videoPanel, #statusCard, #aiCard, #metricsCard, #chartPanel {
            background: #fbfdff;
            border: 1px solid #d6e0e8;
            border-radius: 8px;
        }
        #videoPanel {
            border-top: 3px solid #1f2f3d;
        }
        #statusCard {
            border-top: 3px solid #0f766e;
        }
        #aiCard {
            border-top: 3px solid #2563eb;
        }
        #metricsCard {
            border-top: 3px solid #64748b;
        }
        #chartPanel {
            border-top: 3px solid #0891b2;
        }
        #videoLabel {
            background: #0c141c;
            color: #d8e7f0;
            border: 1px solid #1d2a35;
            border-radius: 6px;
            font-size: 18px;
            font-weight: 800;
        }
        #chartView {
            background: transparent;
            border: 0;
        }
        #sectionTitle {
            background: transparent;
            font-size: 15px;
            font-weight: 900;
            color: #111827;
            padding: 0 0 2px 0;
        }
        #modeBadge {
            background: #e5edf3;
            border: 1px solid #cfdae3;
            border-radius: 6px;
            padding: 6px 10px;
            color: #243545;
            font-weight: 800;
        }
        #statusBadge {
            border-radius: 7px;
            color: #ffffff;
            font-size: 23px;
            font-weight: 900;
            background: #748292;
        }
        #scoreText {
            font-size: 18px;
            font-weight: 900;
            color: #0f1f2e;
        }
        #reasonText {
            background: #f3f7fa;
            border: 1px solid #dfe8ee;
            border-radius: 6px;
            padding: 9px;
            color: #334155;
            line-height: 140%;
        }
        #metricValue {
            background: #f1f5f8;
            border-radius: 4px;
            padding: 2px 8px;
            font-size: 16px;
            font-weight: 900;
            color: #0f2f47;
        }
        #aiValue {
            background: #f1f5f8;
            border-radius: 4px;
            padding: 2px 8px;
            font-size: 13px;
            font-weight: 800;
            color: #17324a;
        }
        #miniProgress {
            height: 8px;
        }
        #alarmLabel {
            border-radius: 6px;
            padding: 9px;
            background: #e7f4ee;
            color: #0f7a4c;
            font-size: 16px;
            font-weight: 900;
        }
        QProgressBar {
            background: #e8eef3;
            border: 0;
            border-radius: 6px;
            height: 12px;
        }
        QProgressBar::chunk {
            background: #20c779;
            border-radius: 6px;
        }
        QPushButton {
            background: #2563eb;
            color: white;
            border: 0;
            border-radius: 6px;
            padding: 9px 14px;
            font-weight: 900;
        }
        QPushButton:hover {
            background: #1d4ed8;
        }
        QPushButton:disabled {
            background: #aab6c2;
            color: #eef3f7;
        }
        QPushButton#continueButton {
            background: #0f9f6e;
        }
        QPushButton#continueButton:hover {
            background: #0b7f59;
        }
        QPushButton#continueButton:disabled {
            background: #b7c2cb;
        }
        QPushButton#dangerButton {
            background: #e14646;
        }
        QPushButton#dangerButton:hover {
            background: #bd2f38;
        }
        QTabWidget::pane {
            border: 0;
        }
        QTabBar::tab {
            background: #dfe8ef;
            padding: 10px 20px;
            border-top-left-radius: 6px;
            border-top-right-radius: 6px;
            margin-right: 4px;
            font-weight: 900;
            color: #314255;
        }
        QTabBar::tab:selected {
            background: #fbfdff;
            color: #0b63d1;
        }
        QTableWidget {
            background: #fbfdff;
            border: 1px solid #d6e0e8;
            border-radius: 8px;
            gridline-color: #e7eef3;
            selection-background-color: #dbeafe;
        }
        QHeaderView::section {
            background: #e8eff5;
            padding: 8px;
            border: 0;
            font-weight: 900;
            color: #1f2d3a;
        }
        #logBox {
            background: #0c141c;
            color: #c7e4ff;
            border-radius: 8px;
            padding: 8px;
            font-family: Consolas, "Microsoft YaHei UI";
        }
        QScrollBar:vertical {
            background: #edf2f6;
            width: 10px;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #bac7d2;
            border-radius: 5px;
            min-height: 40px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
    )");
}

void MainWindow::initDatabase()
{
    QDir().mkpath(runtimeDir());
    const QString dbPath = QDir(runtimeDir()).filePath("driveguard.db");

    const QString connectionName = "driveguard";
    if (QSqlDatabase::contains(connectionName)) {
        m_db = QSqlDatabase::database(connectionName);
    } else {
        m_db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
    }
    m_db.setDatabaseName(dbPath);

    if (!m_db.open()) {
        appendLog("数据库打开失败：" + m_db.lastError().text());
        return;
    }

    createTables();
    appendLog("SQLite 数据库已连接：" + dbPath);
}

void MainWindow::createTables()
{
    QSqlQuery query(m_db);
    query.exec(QString("PRAGMA application_id = %1").arg(kSqliteApplicationId));
    query.exec("PRAGMA user_version = 20260722");

    query.exec(R"(
        CREATE TABLE IF NOT EXISTS detection_records (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            created_at TEXT NOT NULL,
            source_type TEXT NOT NULL,
            fatigue_level TEXT NOT NULL,
            fatigue_score INTEGER NOT NULL,
            ear REAL,
            mar REAL,
            perclos REAL,
            blink_rate REAL,
            yawn_count INTEGER,
            eye_closed_seconds REAL,
            pitch REAL,
            yaw REAL,
            roll REAL,
            reason TEXT,
            snapshot_path TEXT,
            attention_state TEXT,
            quality_score INTEGER DEFAULT 0,
            calibration_progress INTEGER DEFAULT 0,
            adaptive_ear_threshold REAL DEFAULT 0.20,
            adaptive_mar_threshold REAL DEFAULT 0.62,
            risk_factors TEXT,
            detector_backend TEXT,
            feature_origin TEXT,
            perception_state TEXT,
            measurement_valid INTEGER DEFAULT 0,
            processing_fps REAL DEFAULT 0,
            latency_ms REAL DEFAULT 0
        )
    )");

    query.exec(R"(
        CREATE TABLE IF NOT EXISTS system_metadata (
            key TEXT PRIMARY KEY,
            value TEXT NOT NULL,
            updated_at TEXT NOT NULL
        )
    )");

    query.exec(R"(
        CREATE TABLE IF NOT EXISTS alarm_events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            created_at TEXT NOT NULL,
            level TEXT NOT NULL,
            message TEXT NOT NULL,
            acknowledged INTEGER DEFAULT 0
        )
    )");

    query.exec(R"(
        CREATE TABLE IF NOT EXISTS safety_events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            created_at TEXT NOT NULL,
            event_type TEXT NOT NULL,
            risk_level TEXT NOT NULL,
            fatigue_score INTEGER NOT NULL,
            source_type TEXT NOT NULL,
            confidence INTEGER DEFAULT 0,
            reason TEXT,
            risk_factors TEXT,
            snapshot_path TEXT,
            frame_path TEXT,
            ear REAL,
            mar REAL,
            perclos REAL,
            eye_closed_seconds REAL,
            pitch REAL,
            yaw REAL,
            quality_score INTEGER DEFAULT 0,
            calibration_progress INTEGER DEFAULT 0,
            detector_backend TEXT,
            feature_origin TEXT,
            perception_state TEXT,
            measurement_valid INTEGER DEFAULT 0,
            processing_fps REAL DEFAULT 0,
            latency_ms REAL DEFAULT 0,
            status TEXT DEFAULT 'pending',
            note TEXT
        )
    )");

    ensureColumn(m_db, "detection_records", "attention_state", "TEXT");
    ensureColumn(m_db, "detection_records", "quality_score", "INTEGER DEFAULT 0");
    ensureColumn(m_db, "detection_records", "calibration_progress", "INTEGER DEFAULT 0");
    ensureColumn(m_db, "detection_records", "adaptive_ear_threshold", "REAL DEFAULT 0.20");
    ensureColumn(m_db, "detection_records", "adaptive_mar_threshold", "REAL DEFAULT 0.62");
    ensureColumn(m_db, "detection_records", "risk_factors", "TEXT");
    ensureColumn(m_db, "detection_records", "detector_backend", "TEXT");
    ensureColumn(m_db, "detection_records", "feature_origin", "TEXT");
    ensureColumn(m_db, "detection_records", "perception_state", "TEXT");
    ensureColumn(m_db, "detection_records", "measurement_valid", "INTEGER DEFAULT 0");
    ensureColumn(m_db, "detection_records", "processing_fps", "REAL DEFAULT 0");
    ensureColumn(m_db, "detection_records", "latency_ms", "REAL DEFAULT 0");
    ensureColumn(m_db, "safety_events", "confidence", "INTEGER DEFAULT 0");
    ensureColumn(m_db, "safety_events", "risk_factors", "TEXT");
    ensureColumn(m_db, "safety_events", "frame_path", "TEXT");
    ensureColumn(m_db, "safety_events", "quality_score", "INTEGER DEFAULT 0");
    ensureColumn(m_db, "safety_events", "calibration_progress", "INTEGER DEFAULT 0");
    ensureColumn(m_db, "safety_events", "detector_backend", "TEXT");
    ensureColumn(m_db, "safety_events", "feature_origin", "TEXT");
    ensureColumn(m_db, "safety_events", "perception_state", "TEXT");
    ensureColumn(m_db, "safety_events", "measurement_valid", "INTEGER DEFAULT 0");
    ensureColumn(m_db, "safety_events", "processing_fps", "REAL DEFAULT 0");
    ensureColumn(m_db, "safety_events", "latency_ms", "REAL DEFAULT 0");
    ensureColumn(m_db, "safety_events", "status", "TEXT DEFAULT 'pending'");
    ensureColumn(m_db, "safety_events", "note", "TEXT");
    query.exec("CREATE INDEX IF NOT EXISTS idx_detection_records_created_at ON detection_records(created_at)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_safety_events_created_at ON safety_events(created_at)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_safety_events_status ON safety_events(status)");
    query.exec("CREATE INDEX IF NOT EXISTS idx_safety_events_type ON safety_events(event_type)");

    auto putMeta = [this](const QString &key, const QString &value) {
        QSqlQuery meta(m_db);
        meta.prepare(R"(
            INSERT OR REPLACE INTO system_metadata (key, value, updated_at)
            VALUES (?, ?, ?)
        )");
        meta.addBindValue(key);
        meta.addBindValue(value);
        meta.addBindValue(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"));
        meta.exec();
    };

    putMeta("product", "DriveGuard-AI");
    putMeta("author", authorName());
    putMeta("author_roman", authorRomanName());
    putMeta("authorship_notice", authorshipNotice());
    putMeta("identity", productIdentity());
    putMeta("authorship_sha256", authorshipHash());
    putMeta("sqlite_application_id", QString::number(kSqliteApplicationId));
}

void MainWindow::insertRecord(const FatigueSample &sample)
{
    if (!m_db.isOpen()) {
        return;
    }

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT INTO detection_records (
            created_at, source_type, fatigue_level, fatigue_score,
            ear, mar, perclos, blink_rate, yawn_count, eye_closed_seconds,
            pitch, yaw, roll, reason, snapshot_path,
            attention_state, quality_score, calibration_progress,
            adaptive_ear_threshold, adaptive_mar_threshold, risk_factors,
            detector_backend, feature_origin, perception_state,
            measurement_valid, processing_fps, latency_ms
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");
    query.addBindValue(sample.timestamp);
    query.addBindValue(sample.mode);
    query.addBindValue(levelText(sample.level));
    query.addBindValue(sample.score);
    query.addBindValue(sample.ear);
    query.addBindValue(sample.mar);
    query.addBindValue(sample.perclos);
    query.addBindValue(sample.blinkRate);
    query.addBindValue(sample.yawnCount);
    query.addBindValue(sample.eyeClosedSeconds);
    query.addBindValue(sample.pitch);
    query.addBindValue(sample.yaw);
    query.addBindValue(sample.roll);
    query.addBindValue(sample.reason);
    query.addBindValue(sample.snapshotPath.isEmpty() ? sample.framePath : sample.snapshotPath);
    query.addBindValue(sample.attentionState);
    query.addBindValue(sample.qualityScore);
    query.addBindValue(sample.calibrationProgress);
    query.addBindValue(sample.adaptiveEarThreshold);
    query.addBindValue(sample.adaptiveMarThreshold);
    query.addBindValue(sample.riskFactors);
    query.addBindValue(sample.detectorBackend);
    query.addBindValue(sample.featureOrigin);
    query.addBindValue(sample.perceptionState);
    query.addBindValue(sample.measurementValid ? 1 : 0);
    query.addBindValue(sample.processingFps);
    query.addBindValue(sample.latencyMs);

    if (!query.exec()) {
        appendLog("记录写入失败：" + query.lastError().text());
    }
}

void MainWindow::insertSafetyEvent(const FatigueSample &sample, const QString &eventType)
{
    if (!m_db.isOpen() || eventType.isEmpty()) {
        return;
    }

    if (m_eventClock.isValid()) {
        const qint64 elapsed = m_eventClock.elapsed();
        qint64 sameTypeCooldown = 6000;
        if (sample.level == "severe") {
            sameTypeCooldown = 10000;
        } else if (sample.level == "invalid") {
            sameTypeCooldown = 15000;
        }
        if (elapsed < 2500) {
            return;
        }
        if (m_lastEventType == eventType && elapsed < sameTypeCooldown) {
            return;
        }
    }

    const QString evidencePath = sample.snapshotPath.isEmpty() ? sample.framePath : sample.snapshotPath;
    const int confidence = qBound(45, sample.score + sample.qualityScore / 4 + sample.calibrationProgress / 5, 98);

    QSqlQuery query(m_db);
    query.prepare(R"(
        INSERT INTO safety_events (
            created_at, event_type, risk_level, fatigue_score, source_type,
            confidence, reason, risk_factors, snapshot_path, frame_path,
            ear, mar, perclos, eye_closed_seconds, pitch, yaw,
            quality_score, calibration_progress,
            detector_backend, feature_origin, perception_state,
            measurement_valid, processing_fps, latency_ms,
            status, note
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )");
    query.addBindValue(sample.timestamp);
    query.addBindValue(eventType);
    query.addBindValue(levelText(sample.level));
    query.addBindValue(sample.score);
    query.addBindValue(sample.mode);
    query.addBindValue(confidence);
    query.addBindValue(sample.reason);
    query.addBindValue(sample.riskFactors);
    query.addBindValue(evidencePath);
    query.addBindValue(sample.framePath);
    query.addBindValue(sample.ear);
    query.addBindValue(sample.mar);
    query.addBindValue(sample.perclos);
    query.addBindValue(sample.eyeClosedSeconds);
    query.addBindValue(sample.pitch);
    query.addBindValue(sample.yaw);
    query.addBindValue(sample.qualityScore);
    query.addBindValue(sample.calibrationProgress);
    query.addBindValue(sample.detectorBackend);
    query.addBindValue(sample.featureOrigin);
    query.addBindValue(sample.perceptionState);
    query.addBindValue(sample.measurementValid ? 1 : 0);
    query.addBindValue(sample.processingFps);
    query.addBindValue(sample.latencyMs);
    query.addBindValue("待确认");
    query.addBindValue("");

    if (!query.exec()) {
        appendLog("安全事件写入失败：" + query.lastError().text());
        return;
    }

    m_lastEventType = eventType;
    m_eventClock.restart();
    appendLog(QString("安全事件已生成：%1 | %2 | 评分 %3").arg(eventType, levelText(sample.level)).arg(sample.score));
    loadSafetyEvents();
    refreshReportSummary();
}

void MainWindow::loadHistory()
{
    if (!m_historyTable || !m_db.isOpen()) {
        return;
    }

    m_historyTable->setRowCount(0);
    QSqlQuery query(m_db);
    query.prepare(R"(
        SELECT created_at, source_type, fatigue_level, fatigue_score,
               attention_state, quality_score, ear, mar, perclos,
               eye_closed_seconds, reason, snapshot_path
        FROM detection_records
        ORDER BY id DESC
        LIMIT ?
    )");
    query.addBindValue(kHistoryLimit);

    if (!query.exec()) {
        appendLog("历史记录读取失败：" + query.lastError().text());
        return;
    }

    int row = 0;
    while (query.next()) {
        m_historyTable->insertRow(row);
        for (int column = 0; column < 12; ++column) {
            QString value = query.value(column).toString();
            if (column == 11 && !value.isEmpty()) {
                value = QFileInfo(value).fileName();
            }
            auto *item = new QTableWidgetItem(value);
            if (column == 2) {
                item->setForeground(levelColor(query.value(column).toString()));
            }
            m_historyTable->setItem(row, column, item);
        }
        ++row;
    }
}

void MainWindow::loadSafetyEvents()
{
    if (!m_eventTable || !m_db.isOpen()) {
        return;
    }

    m_eventTable->setRowCount(0);
    QSqlQuery query(m_db);
    query.prepare(R"(
        SELECT id, created_at, event_type, risk_level, fatigue_score,
               status, source_type, risk_factors, snapshot_path
        FROM safety_events
        ORDER BY id DESC
        LIMIT 200
    )");

    if (!query.exec()) {
        appendLog("安全事件读取失败：" + query.lastError().text());
        return;
    }

    int row = 0;
    while (query.next()) {
        m_eventTable->insertRow(row);
        for (int column = 0; column < 9; ++column) {
            QString value = query.value(column).toString();
            if (column == 8 && !value.isEmpty()) {
                value = QFileInfo(value).fileName();
            }
            auto *item = new QTableWidgetItem(value);
            if (column == 3) {
                item->setForeground(levelColor(query.value(column).toString()));
            }
            if (column == 0) {
                item->setData(Qt::UserRole, query.value(0).toInt());
            }
            if (column == 8) {
                item->setToolTip(query.value(8).toString());
            }
            m_eventTable->setItem(row, column, item);
        }
        ++row;
    }

    if (m_eventTable->rowCount() > 0) {
        m_eventTable->selectRow(0);
    }
}

void MainWindow::handleEventSelectionChanged()
{
    if (!m_eventTable) {
        return;
    }
    refreshEventDetail(m_eventTable->currentRow());
}

void MainWindow::refreshEventDetail(int row)
{
    if (!m_eventTable || row < 0 || !m_db.isOpen()) {
        return;
    }

    auto *idItem = m_eventTable->item(row, 0);
    if (!idItem) {
        return;
    }

    QSqlQuery query(m_db);
    query.prepare(R"(
        SELECT id, created_at, event_type, risk_level, fatigue_score, confidence,
               reason, risk_factors, snapshot_path, ear, mar, perclos,
               eye_closed_seconds, pitch, yaw, quality_score, calibration_progress,
               status, note
        FROM safety_events
        WHERE id = ?
    )");
    query.addBindValue(idItem->text().toInt());
    if (!query.exec() || !query.next()) {
        return;
    }

    const QString snapshotPath = query.value(8).toString();
    QPixmap preview(snapshotPath);
    if (!preview.isNull() && m_eventPreviewLabel) {
        m_eventPreviewLabel->setPixmap(preview.scaled(
            m_eventPreviewLabel->size(),
            Qt::KeepAspectRatio,
            Qt::SmoothTransformation
        ));
    } else if (m_eventPreviewLabel) {
        m_eventPreviewLabel->setText("暂无证据截图");
    }

    if (m_eventDetailLabel) {
        m_eventDetailLabel->setText(QString(
            "事件 #%1\n时间：%2\n类型：%3\n等级：%4\n评分/可信指数：%5 / %6\n"
            "EAR/MAR/PERCLOS：%7 / %8 / %9\n连续闭眼：%10 s\n姿态：P %11 / Y %12\n"
            "画面质量/校准：%13 / %14%\n状态：%15\n主因子：%16\n原因：%17"
        ).arg(query.value(0).toString(),
              query.value(1).toString(),
              query.value(2).toString(),
              query.value(3).toString())
         .arg(query.value(4).toInt())
         .arg(query.value(5).toInt())
         .arg(fmt(query.value(9).toDouble()), fmt(query.value(10).toDouble()), fmt(query.value(11).toDouble()))
         .arg(fmt(query.value(12).toDouble(), 1), fmt(query.value(13).toDouble(), 1), fmt(query.value(14).toDouble(), 1))
         .arg(query.value(15).toInt())
         .arg(query.value(16).toInt())
         .arg(query.value(17).toString(),
              query.value(7).toString(),
              query.value(6).toString()));
    }
}

void MainWindow::acknowledgeSelectedEvent()
{
    if (!m_eventTable || m_eventTable->currentRow() < 0 || !m_db.isOpen()) {
        return;
    }
    QSqlQuery query(m_db);
    query.prepare("UPDATE safety_events SET status = '已确认', note = '人工确认风险事件' WHERE id = ?");
    query.addBindValue(m_eventTable->item(m_eventTable->currentRow(), 0)->text().toInt());
    if (!query.exec()) {
        appendLog("事件确认失败：" + query.lastError().text());
        return;
    }
    loadSafetyEvents();
    refreshReportSummary();
}

void MainWindow::markSelectedEventFalsePositive()
{
    if (!m_eventTable || m_eventTable->currentRow() < 0 || !m_db.isOpen()) {
        return;
    }
    QSqlQuery query(m_db);
    query.prepare("UPDATE safety_events SET status = '误报', note = '人工标记误报' WHERE id = ?");
    query.addBindValue(m_eventTable->item(m_eventTable->currentRow(), 0)->text().toInt());
    if (!query.exec()) {
        appendLog("事件标记失败：" + query.lastError().text());
        return;
    }
    loadSafetyEvents();
    refreshReportSummary();
}

void MainWindow::refreshReportSummary()
{
    if (!m_db.isOpen()) {
        return;
    }

    auto scalar = [this](const QString &sql) {
        QSqlQuery query(m_db);
        return query.exec(sql) && query.next() ? query.value(0) : QVariant();
    };

    const int totalEvents = scalar("SELECT COUNT(*) FROM safety_events").toInt();
    const int highRiskEvents = scalar("SELECT COUNT(*) FROM safety_events WHERE risk_level IN ('中度疲劳','重度疲劳') OR fatigue_score >= 65").toInt();
    const int pendingEvents = scalar("SELECT COUNT(*) FROM safety_events WHERE status = '待确认'").toInt();
    const int totalRecords = scalar("SELECT COUNT(*) FROM detection_records").toInt();
    const int maxScore = scalar("SELECT COALESCE(MAX(fatigue_score), 0) FROM detection_records").toInt();
    const double avgScore = scalar("SELECT COALESCE(AVG(fatigue_score), 0) FROM detection_records").toDouble();
    const QString firstTime = scalar("SELECT created_at FROM detection_records ORDER BY id ASC LIMIT 1").toString();
    const QString lastTime = scalar("SELECT created_at FROM detection_records ORDER BY id DESC LIMIT 1").toString();

    if (m_totalEventsLabel) {
        m_totalEventsLabel->setText(QString::number(totalEvents));
        m_highRiskEventsLabel->setText(QString::number(highRiskEvents));
        m_pendingEventsLabel->setText(QString::number(pendingEvents));
    }

    if (m_reportSummaryLabel) {
        m_reportSummaryLabel->setText(QString(
            "检测样本：%1\n事件总数：%2\n高风险事件：%3\n待确认事件：%4\n最高疲劳评分：%5\n平均疲劳评分：%6\n"
            "数据范围：%7 至 %8\n\n交付建议：报告导出后包含风险概览、关键事件、证据路径和系统建议，适合答辩演示与验收归档。"
        ).arg(totalRecords)
         .arg(totalEvents)
         .arg(highRiskEvents)
         .arg(pendingEvents)
         .arg(maxScore)
         .arg(fmt(avgScore, 1), firstTime.isEmpty() ? "--" : firstTime, lastTime.isEmpty() ? "--" : lastTime));
    }
}

void MainWindow::startSimulation()
{
    startDetector("simulation");
}

void MainWindow::startCamera()
{
    startDetector("camera");
}

void MainWindow::openVideoFile()
{
    const QString file = QFileDialog::getOpenFileName(
        this,
        "选择驾驶员视频",
        QDir::homePath(),
        "Video Files (*.mp4 *.avi *.mov *.mkv *.wmv);;All Files (*.*)"
    );
    if (!file.isEmpty()) {
        startDetector("video", file);
    }
}

void MainWindow::continueDetector()
{
    if (m_lastMode.isEmpty()) {
        startCamera();
        return;
    }

    if (m_lastMode == "video" && !QFileInfo::exists(m_lastSource)) {
        QMessageBox::warning(this, "无法继续", "上一次的视频文件已经不可用，请重新选择视频。");
        return;
    }

    startDetector(m_lastMode, m_lastSource);
}

void MainWindow::exportHistoryCsv()
{
    if (!m_db.isOpen()) {
        QMessageBox::warning(this, "导出失败", "SQLite 数据库尚未打开。");
        return;
    }

    const QString defaultPath = QDir(runtimeDir()).filePath("driveguard_records.csv");
    const QString filePath = QFileDialog::getSaveFileName(
        this,
        "导出检测记录",
        defaultPath,
        "CSV Files (*.csv);;All Files (*.*)"
    );
    if (filePath.isEmpty()) {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "导出失败", "无法写入文件：" + filePath);
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    const QStringList headers = {
        "时间", "来源", "等级", "评分", "注意力", "质量", "校准进度",
        "EAR", "MAR", "PERCLOS", "动态EAR阈值", "动态MAR阈值",
        "眨眼频率", "哈欠次数", "连续闭眼", "Pitch", "Yaw", "Roll",
        "主风险因子", "原因", "证据快照"
    };
    out << headers.join(',') << '\n';

    QSqlQuery query(m_db);
    if (!query.exec(R"(
        SELECT created_at, source_type, fatigue_level, fatigue_score,
               attention_state, quality_score, calibration_progress,
               ear, mar, perclos, adaptive_ear_threshold, adaptive_mar_threshold,
               blink_rate, yawn_count, eye_closed_seconds, pitch, yaw, roll,
               risk_factors, reason, snapshot_path
        FROM detection_records
        ORDER BY id DESC
    )")) {
        QMessageBox::warning(this, "导出失败", query.lastError().text());
        return;
    }

    while (query.next()) {
        QStringList row;
        for (int i = 0; i < headers.size(); ++i) {
            row << csvEscape(query.value(i).toString());
        }
        out << row.join(',') << '\n';
    }

    appendLog("检测记录已导出：" + filePath);
    QMessageBox::information(this, "导出完成", "检测记录已导出到：" + filePath);
}

void MainWindow::exportHtmlReport()
{
    if (!m_db.isOpen()) {
        QMessageBox::warning(this, "导出失败", "SQLite 数据库尚未打开。");
        return;
    }

    const QString defaultPath = QDir(runtimeDir()).filePath("DriveGuard_AI_检测报告.html");
    const QString filePath = QFileDialog::getSaveFileName(
        this,
        "导出企业检测报告",
        defaultPath,
        "HTML Files (*.html);;All Files (*.*)"
    );
    if (filePath.isEmpty()) {
        return;
    }

    auto scalar = [this](const QString &sql) {
        QSqlQuery query(m_db);
        return query.exec(sql) && query.next() ? query.value(0) : QVariant();
    };

    const int totalRecords = scalar("SELECT COUNT(*) FROM detection_records").toInt();
    const int totalEvents = scalar("SELECT COUNT(*) FROM safety_events").toInt();
    const int highRiskEvents = scalar("SELECT COUNT(*) FROM safety_events WHERE risk_level IN ('中度疲劳','重度疲劳') OR fatigue_score >= 65").toInt();
    const int pendingEvents = scalar("SELECT COUNT(*) FROM safety_events WHERE status = '待确认'").toInt();
    const int maxScore = scalar("SELECT COALESCE(MAX(fatigue_score), 0) FROM detection_records").toInt();
    const double avgScore = scalar("SELECT COALESCE(AVG(fatigue_score), 0) FROM detection_records").toDouble();
    const QString firstTime = scalar("SELECT created_at FROM detection_records ORDER BY id ASC LIMIT 1").toString();
    const QString lastTime = scalar("SELECT created_at FROM detection_records ORDER BY id DESC LIMIT 1").toString();
    const QString signature = brandSignature();
    const QString fullHash = authorshipHash();

    QString eventRows;
    QSqlQuery events(m_db);
    events.exec(R"(
        SELECT created_at, event_type, risk_level, fatigue_score, confidence,
               status, reason, risk_factors, snapshot_path
        FROM safety_events
        ORDER BY id DESC
        LIMIT 30
    )");
    while (events.next()) {
        const QString snapshot = events.value(8).toString();
        const QString image = snapshot.isEmpty()
            ? QString("<span class='muted'>无截图</span>")
            : QString("<img src='%1' alt='event'>").arg(htmlImageDataUri(snapshot));
        eventRows += QString(
            "<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5%</td><td>%6</td><td>%7</td><td>%8</td><td>%9</td></tr>\n"
        ).arg(htmlEscape(events.value(0).toString()),
              htmlEscape(events.value(1).toString()),
              htmlEscape(events.value(2).toString()))
         .arg(events.value(3).toInt())
         .arg(events.value(4).toInt())
         .arg(htmlEscape(events.value(5).toString()),
              htmlEscape(events.value(7).toString()),
              htmlEscape(events.value(6).toString()),
              image);
    }
    if (eventRows.isEmpty()) {
        eventRows = "<tr><td colspan='9' class='muted'>暂无安全事件，建议先运行模拟模式或摄像头模式生成检测数据。</td></tr>";
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "导出失败", "无法写入报告文件：" + filePath);
        return;
    }

    QTextStream out(&file);
    out.setEncoding(QStringConverter::Utf8);
    out << R"(<!doctype html><html lang="zh-CN"><head><meta charset="utf-8">)"
        << "<title>DriveGuard-AI 企业检测报告</title>"
        << R"(<style>
            body{margin:0;background:#eef3f7;color:#111827;font-family:"Microsoft YaHei",Arial,sans-serif;}
            .wrap{max-width:1180px;margin:0 auto;padding:32px;}
            h1{margin:0;font-size:30px}.sub{color:#64748b;margin-top:8px}
            .grid{display:grid;grid-template-columns:repeat(4,1fr);gap:12px;margin:24px 0}
            .card{background:#fff;border:1px solid #d7e1ea;border-radius:8px;padding:16px}
            .label{color:#64748b;font-size:13px}.value{font-size:26px;font-weight:800;margin-top:8px;color:#0f172a}
            table{width:100%;border-collapse:collapse;background:#fff;border:1px solid #d7e1ea;border-radius:8px;overflow:hidden}
            th,td{padding:10px;border-bottom:1px solid #e5edf3;text-align:left;vertical-align:top;font-size:13px}
            th{background:#e8eff5;font-weight:800} img{width:150px;border-radius:6px;border:1px solid #d7e1ea}
            .muted{color:#64748b}.advice{line-height:1.8}
            .identity{margin-top:10px;padding:10px 12px;background:#0f172a;color:#dbeafe;border-radius:8px;font-weight:800}
            .footer{margin-top:24px;padding:14px 16px;background:#fff;border:1px solid #d7e1ea;border-radius:8px;color:#475569;line-height:1.7}
            .reportWatermark{position:fixed;right:22px;bottom:18px;color:rgba(15,23,42,.18);font-size:18px;font-weight:900;pointer-events:none}
        </style></head><body><div class="wrap">)"
        << "<div class='reportWatermark'>制作：" << htmlEscape(authorName()) << "</div>"
        << "<h1>DriveGuard-AI 驾驶员状态安全检测报告</h1>"
        << "<div class='sub'>生成时间：" << htmlEscape(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss"))
        << " ｜ 数据范围：" << htmlEscape(firstTime.isEmpty() ? "--" : firstTime)
        << " 至 " << htmlEscape(lastTime.isEmpty() ? "--" : lastTime) << "</div>"
        << "<div class='identity'>" << htmlEscape(signature) << " ｜ " << htmlEscape(authorshipNotice()) << "</div>"
        << "<div class='grid'>"
        << "<div class='card'><div class='label'>检测样本</div><div class='value'>" << totalRecords << "</div></div>"
        << "<div class='card'><div class='label'>事件总数</div><div class='value'>" << totalEvents << "</div></div>"
        << "<div class='card'><div class='label'>高风险事件</div><div class='value'>" << highRiskEvents << "</div></div>"
        << "<div class='card'><div class='label'>最高评分</div><div class='value'>" << maxScore << "</div></div>"
        << "</div>"
        << "<div class='card advice'><b>系统结论：</b>平均疲劳评分 " << fmt(avgScore, 1)
        << "，待确认事件 " << pendingEvents
        << "。建议结合事件截图、风险因子和连续闭眼/PERCLOS 指标复核高风险片段。</div>"
        << "<h2>关键安全事件</h2>"
        << "<table><thead><tr><th>时间</th><th>类型</th><th>等级</th><th>评分</th><th>可信指数</th><th>状态</th><th>主因子</th><th>原因</th><th>证据</th></tr></thead><tbody>"
        << eventRows
        << "</tbody></table>"
        << "<div class='footer'>本报告由 DriveGuard-AI 本地生成，截图路径来自 runtime/snapshots 或最新检测帧。<br>"
        << "原创归属：" << htmlEscape(authorName()) << " / " << htmlEscape(authorRomanName())
        << " ｜ 产品身份：" << htmlEscape(productIdentity())
        << " ｜ 内嵌作者身份 SHA-256：" << htmlEscape(fullHash)
        << "</div>"
        << "</div></body></html>";

    appendLog("企业检测报告已导出：" + filePath);
    QMessageBox::information(this, "导出完成", "企业检测报告已导出到：" + filePath);
}

void MainWindow::stopDetector()
{
    if (!m_detector || m_detector->state() == QProcess::NotRunning) {
        setRunningUi(false);
        return;
    }

    m_detector->terminate();
    if (!m_detector->waitForFinished(2000)) {
        m_detector->kill();
        m_detector->waitForFinished(1000);
    }
    setRunningUi(false);
    if (m_modeLabel) {
        m_modeLabel->setText("已停止");
    }
    appendLog("检测进程已停止。");
}

void MainWindow::startDetector(const QString &mode, const QString &source)
{
    stopDetector();
    QDir().mkpath(runtimeDir());

    const QString script = detectorScriptPath();
    if (!QFileInfo::exists(script)) {
        QMessageBox::critical(this, "缺少检测脚本", "未找到：" + script);
        return;
    }
    const QString python = resolvePythonExecutable();
    if (python.isEmpty()) {
        QMessageBox::critical(this, "缺少 Python",
            "未找到可运行检测脚本的 Python 环境。\n\n"
            "需要 Python 能够导入 cv2 和 numpy。请运行 scripts\\setup_windows.ps1，"
            "或设置 DRIVEGUARD_PYTHON 指向已安装依赖的 python.exe。");
        appendLog("Python 环境不可用：需要 cv2/numpy。可运行 scripts\\setup_windows.ps1 或设置 DRIVEGUARD_PYTHON。");
        setRunningUi(false);
        return;
    }

    QStringList args;
    if (isPythonLauncher(python)) {
        args << "-3";
    }
    args << script
         << "--mode" << mode
         << "--runtime" << runtimeDir()
         << "--frame-width" << "800"
         << "--frame-height" << "450";
    if (!source.isEmpty()) {
        args << "--source" << source;
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONUNBUFFERED", "1");
    env.insert("DRIVEGUARD_PYTHON", python);
    env.insert("DRIVEGUARD_ROOT", projectRoot());
    env.insert("DRIVEGUARD_RUNTIME_DIR", runtimeDir());
    m_detector->setProcessEnvironment(env);
    m_detector->setWorkingDirectory(projectRoot());
    m_stdoutBuffer.clear();
    m_lastDetectorError.clear();

    m_scoreSeries->clear();
    m_perclosSeries->clear();
    m_sampleIndex = 0;
    m_axisX->setRange(0, kChartWindow);
    m_lastMode = mode;
    m_lastSource = source;
    setRunningUi(true);
    const QString startupModeText = mode == "simulation" ? "模拟模式" :
        mode == "camera" ? "摄像头模式" : "视频模式";
    m_modeLabel->setText("启动中：" + startupModeText);
    appendLog("正在启动检测进程：" + python + " " + args.join(' '));
    m_detector->start(python, args);
}

void MainWindow::readDetectorOutput()
{
    m_stdoutBuffer += m_detector->readAllStandardOutput();
    int newlineIndex = -1;
    while ((newlineIndex = m_stdoutBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdoutBuffer.left(newlineIndex).trimmed();
        m_stdoutBuffer.remove(0, newlineIndex + 1);
        if (!line.isEmpty()) {
            processJsonLine(line);
        }
    }
}

void MainWindow::readDetectorError()
{
    const QString text = QString::fromUtf8(m_detector->readAllStandardError()).trimmed();
    if (!text.isEmpty()) {
        const QStringList lines = text.split('\n', Qt::SkipEmptyParts);
        for (const QString &rawLine : lines) {
            const QString line = rawLine.trimmed();
            if (line.startsWith("INFO: Created TensorFlow Lite")
                || line.startsWith("WARNING: All log messages")
                || line.contains("inference_feedback_manager.cc")) {
                continue;
            }
            m_lastDetectorError += line + "\n";
            if (m_lastDetectorError.size() > 4000) {
                m_lastDetectorError = m_lastDetectorError.right(4000);
            }
            appendLog("[detector] " + line);
        }
    }
}

void MainWindow::handleDetectorFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    setRunningUi(false);
    if (m_modeLabel) {
        m_modeLabel->setText("已停止");
    }
    appendLog(QString("检测进程退出：exitCode=%1, status=%2")
                  .arg(exitCode)
                  .arg(exitStatus == QProcess::NormalExit ? "NormalExit" : "CrashExit"));
    if (exitStatus != QProcess::NormalExit || exitCode != 0) {
        const QString detail = m_lastDetectorError.trimmed().isEmpty()
            ? "没有捕获到 stderr。请检查 Python 依赖、脚本路径、视频文件或摄像头权限。"
            : m_lastDetectorError.trimmed();
        if (m_reasonLabel) {
            m_reasonLabel->setText("检测进程异常退出：" + detail.left(240));
        }
        QMessageBox::warning(this, "检测进程已退出",
            QString("Python 检测进程异常退出。\n\nexitCode=%1\nstatus=%2\n\n%3")
                .arg(exitCode)
                .arg(exitStatus == QProcess::NormalExit ? "NormalExit" : "CrashExit")
                .arg(detail.left(1800)));
    }
}

void MainWindow::handleDetectorStarted()
{
    const QString modeText = m_lastMode == "simulation" ? "模拟模式" :
        m_lastMode == "camera" ? "摄像头模式" :
        m_lastMode == "video" ? "视频模式" : m_lastMode;
    if (m_modeLabel) {
        m_modeLabel->setText(modeText);
    }
    appendLog("检测进程已启动：" + modeText + (m_lastSource.isEmpty() ? QString() : " | " + m_lastSource));
}

void MainWindow::handleDetectorError(QProcess::ProcessError error)
{
    setRunningUi(false);
    QString hint;
    switch (error) {
    case QProcess::FailedToStart:
        hint = "无法启动 Python 检测进程。请确认 Python 已安装，或设置 DRIVEGUARD_PYTHON。";
        break;
    case QProcess::Crashed:
        hint = "检测进程异常退出。请检查 scripts/requirements.txt 依赖和摄像头/视频源。";
        break;
    case QProcess::Timedout:
        hint = "检测进程响应超时。";
        break;
    case QProcess::ReadError:
    case QProcess::WriteError:
        hint = "检测进程通信失败。";
        break;
    default:
        hint = "检测进程发生未知错误。";
        break;
    }
    appendLog(hint + " " + m_detector->errorString());
    QMessageBox::warning(this, "检测进程错误", hint + "\n\n" + m_detector->errorString());
}

void MainWindow::handleDetectorStateChanged(QProcess::ProcessState state)
{
    QString text = "未运行";
    if (state == QProcess::Starting) {
        text = "启动中";
    } else if (state == QProcess::Running) {
        text = "运行中";
    }
    appendLog("检测进程状态：" + text);
}

void MainWindow::processJsonLine(const QByteArray &line)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        appendLog("JSON 解析失败：" + QString::fromUtf8(line.left(160)));
        return;
    }
    updateSample(sampleFromJson(doc.object()));
}

void MainWindow::setupVoiceAlerts()
{
    const QStringList files = {"light.wav", "moderate.wav", "severe.wav", "invalid.wav"};

    const QDir audioDir(QDir(projectRoot()).filePath("assets/audio"));
    int readyCount = 0;
    for (const QString &file : files) {
        const QString path = audioDir.filePath(file);
        if (!QFileInfo::exists(path)) {
            appendLog("语音提醒文件缺失：" + path);
            continue;
        }
        ++readyCount;
    }

    appendLog(QString("语音提醒已就绪：%1 个 WAV，使用 Windows 原生音频播放").arg(readyCount));
}

void MainWindow::playVoiceAlert(const FatigueSample &sample)
{
    QString key;
    int cooldownMs = 12000;

    if (sample.level == "invalid") {
        key = "invalid";
        cooldownMs = 18000;
    } else if (sample.level == "severe" || sample.score >= 82) {
        key = "severe";
        cooldownMs = 6000;
    } else if (sample.level == "moderate" || sample.score >= 58) {
        key = "moderate";
        cooldownMs = 9000;
    } else if (sample.level == "light" || sample.score >= 32) {
        key = "light";
        cooldownMs = 15000;
    }

    if (key.isEmpty()) {
        m_lastVoiceKey.clear();
        return;
    }

    if (m_voiceClock.isValid()
        && m_voiceClock.elapsed() < cooldownMs
        && m_lastVoiceKey == key) {
        return;
    }

    const QString wavPath = QDir(projectRoot()).filePath("assets/audio/" + key + ".wav");
    bool played = false;

#ifdef Q_OS_WIN
    m_currentVoicePath = wavPath;
    played = PlaySoundW(reinterpret_cast<LPCWSTR>(m_currentVoicePath.utf16()),
                        nullptr,
                        SND_FILENAME | SND_ASYNC | SND_NODEFAULT);
#endif

    if (!played) {
        QApplication::beep();
    }

    m_lastVoiceKey = key;
    m_voiceClock.restart();
}

FatigueSample MainWindow::sampleFromJson(const QJsonObject &object) const
{
    FatigueSample sample;
    sample.timestamp = object.value("timestamp").toString(QDateTime::currentDateTime().toString(Qt::ISODate));
    sample.mode = object.value("mode").toString();
    sample.level = object.value("level").toString("normal");
    sample.reason = object.value("reason").toString();
    sample.framePath = object.value("frame_path").toString();
    sample.snapshotPath = object.value("snapshot_path").toString();
    sample.attentionState = object.value("attention_state").toString();
    sample.score = object.value("fatigue_score").toInt();
    sample.qualityScore = object.value("quality_score").toInt();
    sample.calibrationProgress = object.value("calibration_progress").toInt();
    sample.ear = object.value("ear").toDouble();
    sample.mar = object.value("mar").toDouble();
    sample.perclos = object.value("perclos").toDouble();
    sample.adaptiveEarThreshold = object.value("adaptive_ear_threshold").toDouble(0.20);
    sample.adaptiveMarThreshold = object.value("adaptive_mar_threshold").toDouble(0.62);
    sample.blinkRate = object.value("blink_rate").toDouble();
    sample.yawnCount = object.value("yawn_count").toInt();
    sample.eyeClosedSeconds = object.value("eye_closed_seconds").toDouble();
    sample.pitch = object.value("pitch").toDouble();
    sample.yaw = object.value("yaw").toDouble();
    sample.roll = object.value("roll").toDouble();
    sample.protocolVersion = object.value("protocol_version").toInt(1);
    sample.detectorBackend = object.value("detector_backend").toString();
    sample.featureOrigin = object.value("feature_origin").toString();
    sample.perceptionState = object.value("perception_state").toString();
    sample.measurementValid = object.value("measurement_valid").toBool(false);
    sample.processingFps = object.value("processing_fps").toDouble();
    sample.latencyMs = object.value("latency_ms").toDouble();

    QStringList factors;
    const QJsonArray riskArray = object.value("risk_factors").toArray();
    for (const QJsonValue &value : riskArray) {
        const QJsonObject factor = value.toObject();
        const QString name = factor.value("name").toString();
        const QString metric = factor.value("value").toVariant().toString();
        const int impact = factor.value("impact").toInt();
        if (!name.isEmpty()) {
            factors << QString("%1 %2(%3)").arg(name, metric).arg(impact);
        }
    }
    sample.riskFactors = factors.join(" | ");
    return sample;
}

void MainWindow::updateSample(const FatigueSample &sample)
{
    const QColor color = levelColor(sample.level);
    const QString colorStyle = QString("background:%1;color:white;border-radius:8px;font-size:24px;font-weight:900;")
                                   .arg(color.name());
    m_statusBadge->setStyleSheet(colorStyle);
    m_statusBadge->setText(levelText(sample.level));

    m_scoreBar->setValue(sample.score);
    m_scoreLabel->setText(QString("%1 / 100").arg(sample.score));
    m_reasonLabel->setText(sample.reason.isEmpty() ? "暂无风险原因" : sample.reason);

    if (m_attentionLabel) {
        m_attentionLabel->setText(sample.attentionState.isEmpty() ? "--" : sample.attentionState);
        m_qualityLabel->setText(QString("%1 / 100").arg(sample.qualityScore));
        m_qualityBar->setValue(sample.qualityScore);
        m_calibrationLabel->setText(QString("%1%").arg(sample.calibrationProgress));
        m_calibrationBar->setValue(sample.calibrationProgress);
        m_thresholdLabel->setText(QString("EAR %1 / MAR %2")
                                      .arg(fmt(sample.adaptiveEarThreshold, 2), fmt(sample.adaptiveMarThreshold, 2)));
        m_riskLabel->setText(sample.riskFactors.isEmpty() ? "风险因子低" : sample.riskFactors);
        const QString snapshotText = sample.snapshotPath.isEmpty()
            ? "等待事件触发"
            : QFileInfo(sample.snapshotPath).fileName();
        m_snapshotLabel->setText(snapshotText);
        m_snapshotLabel->setToolTip(sample.snapshotPath);
    }

    m_earLabel->setText(fmt(sample.ear));
    m_marLabel->setText(fmt(sample.mar));
    m_perclosLabel->setText(fmt(sample.perclos));
    m_blinkLabel->setText(QString("%1 次/分钟").arg(fmt(sample.blinkRate, 1)));
    m_yawnLabel->setText(QString::number(sample.yawnCount));
    m_eyeClosedLabel->setText(QString("%1 s").arg(fmt(sample.eyeClosedSeconds, 1)));
    m_poseLabel->setText(QString("P %1 / Y %2").arg(fmt(sample.pitch, 1), fmt(sample.yaw, 1)));

    const bool invalidInput = sample.level == "invalid";
    const bool dangerous = !invalidInput && (sample.level == "moderate" || sample.level == "severe" || sample.score >= 65);
    const bool lightWarning = !invalidInput && !dangerous && (sample.level == "light" || sample.score >= 32);
    if (invalidInput) {
        m_alarmLabel->setStyleSheet("background:#eef2f5;color:#4f5e6b;border-radius:6px;padding:10px;font-size:17px;font-weight:900;");
        m_alarmLabel->setText("摄像头无有效画面，请检查镜头盖、隐私开关、系统权限或会议软件占用");
        playVoiceAlert(sample);
        m_lastAlarmScore = 0;
    } else if (dangerous) {
        m_alarmLabel->setStyleSheet(QString("background:%1;color:white;border-radius:6px;padding:10px;font-size:17px;font-weight:900;").arg(color.name()));
        m_alarmLabel->setText(sample.level == "severe" ? "严重疲劳风险，请立即停车休息" : "检测到疲劳风险，请提高注意力");
        if (m_alarmClock.elapsed() > 2500 || sample.score > m_lastAlarmScore + 10) {
            playVoiceAlert(sample);
            m_alarmClock.restart();
            m_lastAlarmScore = sample.score;
        }
    } else if (lightWarning) {
        m_alarmLabel->setStyleSheet("background:#fff4df;color:#9a5b00;border-radius:6px;padding:10px;font-size:17px;font-weight:900;");
        m_alarmLabel->setText("轻度疲劳请注意，保持清醒");
        playVoiceAlert(sample);
        m_lastAlarmScore = 0;
    } else {
        m_alarmLabel->setStyleSheet("background:#e8f3ec;color:#167347;border-radius:6px;padding:10px;font-size:17px;font-weight:900;");
        m_alarmLabel->setText("状态稳定，持续监测中");
        m_lastAlarmScore = 0;
    }

    updateVideoFrame(sample.framePath);
    updateChart(sample);
    updateHealthPanel(sample);
    updateCalibrationPanel(sample);

    const QString eventType = eventTypeForSample(sample);
    if (!eventType.isEmpty()) {
        insertSafetyEvent(sample, eventType);
    }

    if (m_dbWriteClock.elapsed() > (dangerous ? 700 : 2000)) {
        insertRecord(sample);
        loadHistory();
        refreshReportSummary();
        m_dbWriteClock.restart();
    }
}

void MainWindow::updateVideoFrame(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    m_currentFramePath = path;
    QPixmap latest;
    if (latest.load(path)) {
        m_currentPixmap = latest;
        refreshVideoPixmap();
    }
}

void MainWindow::refreshVideoPixmap()
{
    if (!m_videoLabel || m_currentPixmap.isNull()) {
        return;
    }
    m_videoLabel->setPixmap(m_currentPixmap.scaled(
        m_videoLabel->size(),
        Qt::KeepAspectRatio,
        Qt::SmoothTransformation
    ));
}

void MainWindow::updateChart(const FatigueSample &sample)
{
    ++m_sampleIndex;
    m_scoreSeries->append(m_sampleIndex, sample.score);
    m_perclosSeries->append(m_sampleIndex, sample.perclos * 100.0);

    while (m_scoreSeries->count() > kChartWindow) {
        m_scoreSeries->remove(0);
    }
    while (m_perclosSeries->count() > kChartWindow) {
        m_perclosSeries->remove(0);
    }

    const int minX = qMax(0, m_sampleIndex - kChartWindow);
    const int maxX = qMax(kChartWindow, m_sampleIndex);
    m_axisX->setRange(minX, maxX);
}

void MainWindow::appendLog(const QString &message)
{
    if (!m_log) {
        return;
    }
    const QString line = QString("[%1] %2")
                             .arg(QDateTime::currentDateTime().toString("HH:mm:ss"), message);
    m_log->appendPlainText(line);
}

void MainWindow::setRunningUi(bool running)
{
    if (m_simButton) {
        m_simButton->setEnabled(!running);
        m_cameraButton->setEnabled(!running);
        m_videoButton->setEnabled(!running);
        m_continueButton->setEnabled(!running && !m_lastMode.isEmpty());
        m_stopButton->setEnabled(running);
    }
    if (!running && m_modeLabel) {
        m_modeLabel->setText("未启动");
    }
}

QString MainWindow::eventTypeForSample(const FatigueSample &sample) const
{
    if (sample.level == "invalid") {
        if (sample.perceptionState == "face_missing") {
            return "面部丢失";
        }
        return "画面无效";
    }
    if (sample.level == "severe") {
        return "重度疲劳";
    }
    if (sample.eyeClosedSeconds >= 2.0) {
        return "连续闭眼";
    }
    if (sample.perclos >= 0.45) {
        return "PERCLOS过高";
    }
    if (sample.mar >= sample.adaptiveMarThreshold && sample.mar >= 0.55) {
        return "疑似哈欠";
    }
    if (qAbs(sample.pitch) >= 18.0 || qAbs(sample.yaw) >= 28.0) {
        return "姿态偏移";
    }
    if (sample.level == "moderate" || sample.score >= 65) {
        return "疲劳风险";
    }
    return {};
}

void MainWindow::updateHealthPanel(const FatigueSample &sample)
{
    if (!m_healthProcessLabel) {
        return;
    }

    const QString pythonPath = resolvePythonExecutable();
    m_healthProcessLabel->setText(QString("%1 | Python: %2")
        .arg(m_detector && m_detector->state() != QProcess::NotRunning ? "运行中" : "已停止",
             pythonPath.isEmpty() ? "未找到" : pythonPath));
    m_healthDbLabel->setText(m_db.isOpen()
        ? "已连接：" + QDir(runtimeDir()).filePath("driveguard.db")
        : "未连接");
    m_healthModeLabel->setText(QString("%1 | 版本 %2")
        .arg(sample.mode.isEmpty() ? "--" : sample.mode,
             qApp->applicationVersion().isEmpty() ? "dev" : qApp->applicationVersion()));
    m_healthFrameLabel->setText(sample.framePath.isEmpty()
        ? "暂无画面"
        : sample.framePath + "\nRuntime: " + runtimeDir());
    m_healthFrameLabel->setToolTip(sample.framePath);
    m_healthQualityLabel->setText(QString("%1 / 100，校准 %2%，FPS %3，延迟 %4 ms")
        .arg(sample.qualityScore)
        .arg(sample.calibrationProgress)
        .arg(fmt(sample.processingFps, 1), fmt(sample.latencyMs, 1)));

    QString model;
    if (sample.detectorBackend == "simulation_engine") {
        model = "虚拟场景仿真引擎";
    } else if (sample.detectorBackend == "mediapipe_face_mesh") {
        model = "MediaPipe 人脸关键点实测";
    } else if (sample.detectorBackend == "haar_cascade") {
        model = "Haar 基础检测 + 仿真特征补全";
    } else if (sample.detectorBackend == "camera_health_check") {
        model = "摄像头画面健康诊断";
    } else if (sample.detectorBackend == "face_detection") {
        model = "驾驶员面部未检测";
    } else if (sample.reason.contains("Haar")) {
        model = "Haar 基础检测 + 仿真特征补全";
    } else if (sample.reason.contains("MediaPipe")) {
        model = "MediaPipe 人脸关键点实测";
    } else {
        model = "视觉检测链路";
    }
    m_healthModelLabel->setText(QString("%1\n后端：%2\n来源：%3\n状态：%4\n测量有效：%5")
        .arg(model,
             sample.detectorBackend.isEmpty() ? "--" : sample.detectorBackend,
             sample.featureOrigin.isEmpty() ? "--" : sample.featureOrigin,
             sample.perceptionState.isEmpty() ? "--" : sample.perceptionState,
             sample.measurementValid ? "是" : "否"));
}

void MainWindow::updateCalibrationPanel(const FatigueSample &sample)
{
    if (!m_calibrationPageBar) {
        return;
    }

    m_calibrationPageBar->setValue(sample.calibrationProgress);
    m_calibrationStateLabel->setText(sample.calibrationProgress >= 100
        ? "个体校准完成，可使用动态阈值进行判定"
        : QString("校准中：%1%，请保持完整正脸和稳定光照").arg(sample.calibrationProgress));
    m_calibrationThresholdLabel->setText(QString("动态阈值：EAR %1 / MAR %2")
                                             .arg(fmt(sample.adaptiveEarThreshold, 2), fmt(sample.adaptiveMarThreshold, 2)));
}

QString MainWindow::levelText(const QString &level) const
{
    if (level == "normal" || level == "正常") {
        return "正常";
    }
    if (level == "light" || level == "轻度疲劳") {
        return "轻度疲劳";
    }
    if (level == "moderate" || level == "中度疲劳") {
        return "中度疲劳";
    }
    if (level == "severe" || level == "重度疲劳") {
        return "重度疲劳";
    }
    if (level == "invalid" || level == "无有效画面") {
        return "无有效画面";
    }
    return level.isEmpty() ? "未知" : level;
}

QColor MainWindow::levelColor(const QString &level) const
{
    if (level == "normal" || level == "正常") {
        return QColor("#19a66a");
    }
    if (level == "light" || level == "轻度疲劳") {
        return QColor("#f0a51a");
    }
    if (level == "moderate" || level == "中度疲劳") {
        return QColor("#e06b2d");
    }
    if (level == "severe" || level == "重度疲劳") {
        return QColor("#d8243c");
    }
    if (level == "invalid" || level == "无有效画面") {
        return QColor("#6b7785");
    }
    return QColor("#7c8791");
}

QString MainWindow::projectRoot() const
{
    auto isRoot = [](const QString &path) {
        if (path.isEmpty()) {
            return false;
        }
        const QDir dir(path);
        return QFileInfo::exists(dir.filePath("scripts/detector.py"))
            && QFileInfo::exists(dir.filePath("assets/audio"));
    };

    const QString envRoot = qEnvironmentVariable("DRIVEGUARD_ROOT");
    if (isRoot(envRoot)) {
        return QDir(envRoot).absolutePath();
    }

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QStringList candidates = {
        appDir.absolutePath(),
        appDir.filePath(".."),
        appDir.filePath("../.."),
        QString::fromUtf8(DRIVEGUARD_SOURCE_DIR)
    };
    for (const QString &candidate : candidates) {
        const QString absolute = QDir(candidate).absolutePath();
        if (isRoot(absolute)) {
            return absolute;
        }
    }
    return QDir(QString::fromUtf8(DRIVEGUARD_SOURCE_DIR)).absolutePath();
}

QString MainWindow::runtimeDir() const
{
    const QString envRuntime = qEnvironmentVariable("DRIVEGUARD_RUNTIME_DIR");
    if (!envRuntime.isEmpty()) {
        QDir().mkpath(envRuntime);
        return QDir(envRuntime).absolutePath();
    }

    const QString projectRuntime = QDir(projectRoot()).filePath("runtime");
    if (QDir().mkpath(projectRuntime)) {
        const QFileInfo info(projectRuntime);
        if (info.isWritable()) {
            QDir(projectRuntime).mkpath("snapshots");
            QDir(projectRuntime).mkpath("reports");
            return QDir(projectRuntime).absolutePath();
        }
    }

    const QString fallback = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/runtime";
    QDir().mkpath(fallback);
    QDir(fallback).mkpath("snapshots");
    QDir(fallback).mkpath("reports");
    return QDir(fallback).absolutePath();
}

QString MainWindow::detectorScriptPath() const
{
    return QDir(projectRoot()).filePath("scripts/detector.py");
}

QString MainWindow::resolvePythonExecutable() const
{
    const QString envPython = qEnvironmentVariable("DRIVEGUARD_PYTHON");
    if (!envPython.isEmpty() && QFileInfo::exists(envPython) && pythonCanRunDetector(envPython)) {
        return QDir::toNativeSeparators(envPython);
    }

    const QDir root(projectRoot());
    const QStringList localCandidates = {
        root.filePath(".venv/Scripts/python.exe"),
        root.filePath("python/python.exe")
    };
    for (const QString &candidate : localCandidates) {
        if (QFileInfo::exists(candidate) && pythonCanRunDetector(candidate)) {
            return QDir::toNativeSeparators(candidate);
        }
    }

    const QStringList names = {"python.exe", "python", "py.exe", "py"};
    for (const QString &name : names) {
        const QString found = QStandardPaths::findExecutable(name);
        if (!found.isEmpty() && pythonCanRunDetector(found)) {
            return QDir::toNativeSeparators(found);
        }
    }
    return {};
}
