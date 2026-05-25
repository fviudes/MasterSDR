#include "gui/DigitalModePanel.h"
#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"
#include "gui/FramelessResizer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QSplitter>
#include <QScrollBar>
#include <QApplication>
#include <QDebug>

namespace MasterSDR {

const QString DigitalModePanel::DIALOG_TITLE = QStringLiteral("Digital Modes - FT8/FT4/JT65/WSPR");
const QString DigitalModePanel::GEOMETRY_KEY = QStringLiteral("DigitalModePanelGeometry");

DigitalModePanel::DigitalModePanel(AudioEngine* audio,
                                   RadioModel* radio,
                                   SliceModel* initialSlice,
                                   QWidget* parent)
    : PersistentDialog(DIALOG_TITLE, GEOMETRY_KEY, parent)
    , m_audio(audio)
    , m_radio(radio)
{
    setMinimumSize(900, 700);
    resize(960, 780);

    m_engine = new DigitalModeEngine(this);

    setupUi();
    applyStyles();
    wireSignals();
    populateFreqPresets();
    setAttachedSlice(initialSlice);

    FramelessResizer::install(this);
    m_engine->start();

    qDebug() << "DigitalModePanel created";
}

DigitalModePanel::~DigitalModePanel()
{
    m_engine->stop();
}

void DigitalModePanel::setupUi()
{
    auto* body = bodyWidget();
    auto* root = new QVBoxLayout(body);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(6);

    auto* mainSplitter = new QSplitter(Qt::Horizontal, body);

    auto* leftPanel = new QWidget(mainSplitter);
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(8, 4, 4, 4);
    leftLayout->setSpacing(6);

    buildModeSelector(leftLayout);
    buildFrequencyPanel(leftLayout);
    buildCallPanel(leftLayout);
    buildMessagePanel(leftLayout);
    buildTxPanel(leftLayout);
    leftLayout->addStretch();

    auto* rightPanel = new QWidget(mainSplitter);
    auto* rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(4, 4, 8, 4);
    rightLayout->setSpacing(6);

    buildDecodePanel(rightPanel);
    buildStatusBar(rightLayout);

    mainSplitter->addWidget(leftPanel);
    mainSplitter->addWidget(rightPanel);
    mainSplitter->setStretchFactor(0, 2);
    mainSplitter->setStretchFactor(1, 3);

    root->addWidget(mainSplitter);
}

void DigitalModePanel::buildModeSelector(QVBoxLayout* layout)
{
    auto* modeGroup = new QGroupBox("Mode", bodyWidget());
    auto* modeLayout = new QFormLayout(modeGroup);

    m_modeCombo = new QComboBox(modeGroup);
    m_modeCombo->addItem("FT8 (15s T/R)", static_cast<int>(DigitalModeEngine::Mode::FT8));
    m_modeCombo->addItem("FT4 (7.5s T/R)", static_cast<int>(DigitalModeEngine::Mode::FT4));
    m_modeCombo->addItem("JT65 (60s T/R)", static_cast<int>(DigitalModeEngine::Mode::JT65));
    m_modeCombo->addItem("JT9 (60s T/R)", static_cast<int>(DigitalModeEngine::Mode::JT9));
    m_modeCombo->addItem("WSPR (120s T/R)", static_cast<int>(DigitalModeEngine::Mode::WSPR));
    m_modeCombo->addItem("Q65 (30s T/R)", static_cast<int>(DigitalModeEngine::Mode::Q65));
    m_modeCombo->addItem("FST4 (60s T/R)", static_cast<int>(DigitalModeEngine::Mode::FST4));
    m_modeCombo->addItem("MSK144 (15s T/R)", static_cast<int>(DigitalModeEngine::Mode::MSK144));
    m_modeCombo->setMinimumHeight(30);
    modeLayout->addRow("Mode:", m_modeCombo);

    m_trPeriodLabel = new QLabel("T/R Period: 15.0s", modeGroup);
    modeLayout->addRow("", m_trPeriodLabel);

    layout->addWidget(modeGroup);
}

void DigitalModePanel::buildFrequencyPanel(QVBoxLayout* layout)
{
    auto* freqGroup = new QGroupBox("Frequency", bodyWidget());
    auto* freqLayout = new QFormLayout(freqGroup);

    m_freqPresetCombo = new QComboBox(freqGroup);
    m_freqPresetCombo->setMinimumHeight(30);
    freqLayout->addRow("Band/Mode:", m_freqPresetCombo);

    m_dialFreqLabel = new QLabel("14.074.000 Hz", freqGroup);
    m_dialFreqLabel->setStyleSheet("font-size: 16px; font-weight: bold; font-family: monospace;");
    freqLayout->addRow("Dial:", m_dialFreqLabel);

    m_rxFreqSpin = new QSpinBox(freqGroup);
    m_rxFreqSpin->setRange(0, 5000);
    m_rxFreqSpin->setValue(1500);
    m_rxFreqSpin->setSingleStep(10);
    m_rxFreqSpin->setSuffix(" Hz");
    m_rxFreqSpin->setMinimumHeight(28);
    freqLayout->addRow("RX DF:", m_rxFreqSpin);

    m_txFreqSpin = new QSpinBox(freqGroup);
    m_txFreqSpin->setRange(0, 5000);
    m_txFreqSpin->setValue(1500);
    m_txFreqSpin->setSingleStep(10);
    m_txFreqSpin->setSuffix(" Hz");
    m_txFreqSpin->setMinimumHeight(28);
    freqLayout->addRow("TX DF:", m_txFreqSpin);

    layout->addWidget(freqGroup);
}

void DigitalModePanel::buildCallPanel(QVBoxLayout* layout)
{
    auto* callGroup = new QGroupBox("Station", bodyWidget());
    auto* callLayout = new QFormLayout(callGroup);

    m_dxCallEdit = new QLineEdit(callGroup);
    m_dxCallEdit->setPlaceholderText("DX Callsign (e.g. K1ABC)");
    m_dxCallEdit->setMinimumHeight(30);
    callLayout->addRow("DX Call:", m_dxCallEdit);

    m_dxGridEdit = new QLineEdit(callGroup);
    m_dxGridEdit->setPlaceholderText("DX Grid (e.g. FN42)");
    m_dxGridEdit->setMinimumHeight(30);
    callLayout->addRow("DX Grid:", m_dxGridEdit);

    m_snrLabel = new QLabel("SNR: --- dB", callGroup);
    callLayout->addRow("", m_snrLabel);

    m_syncLabel = new QLabel("Sync: --", callGroup);
    callLayout->addRow("", m_syncLabel);

    layout->addWidget(callGroup);
}

void DigitalModePanel::buildMessagePanel(QVBoxLayout* layout)
{
    auto* msgGroup = new QGroupBox("Messages", bodyWidget());
    auto* msgLayout = new QVBoxLayout(msgGroup);

    m_messageCombo = new QComboBox(msgGroup);
    m_messageCombo->setEditable(true);
    m_messageCombo->setMinimumHeight(30);
    m_messageCombo->setInsertPolicy(QComboBox::NoInsert);
    msgLayout->addWidget(m_messageCombo);

    m_genMsgBtn = new QPushButton("Generate Standard Messages", msgGroup);
    m_genMsgBtn->setMinimumHeight(30);
    msgLayout->addWidget(m_genMsgBtn);

    layout->addWidget(msgGroup);
}

void DigitalModePanel::buildTxPanel(QVBoxLayout* layout)
{
    auto* txGroup = new QGroupBox("Transmit", bodyWidget());
    auto* txLayout = new QVBoxLayout(txGroup);

    auto* txRow = new QHBoxLayout;
    m_txEnableCheck = new QCheckBox("Enable TX", txGroup);
    m_txEnableCheck->setMinimumHeight(28);
    txRow->addWidget(m_txEnableCheck);

    m_tuneBtn = new QPushButton("Tune", txGroup);
    m_tuneBtn->setCheckable(true);
    m_tuneBtn->setMinimumHeight(28);
    m_tuneBtn->setFixedWidth(80);
    txRow->addWidget(m_tuneBtn);

    m_txStatusLabel = new QLabel("RX", txGroup);
    m_txStatusLabel->setAlignment(Qt::AlignCenter);
    m_txStatusLabel->setMinimumHeight(28);
    m_txStatusLabel->setStyleSheet("font-weight: bold; font-size: 14px; padding: 4px; color: #20c060; background: #102810; border-radius: 4px;");
    txRow->addWidget(m_txStatusLabel);

    m_timerLabel = new QLabel("Next TX: --", txGroup);
    m_timerLabel->setAlignment(Qt::AlignCenter);
    m_timerLabel->setStyleSheet("font-size: 10px; color: #a0b4c4;");
    txLayout->addLayout(txRow);
    txLayout->addWidget(m_timerLabel);

    m_sequenceProgress = new QProgressBar(txGroup);
    m_sequenceProgress->setRange(0, 1000);
    m_sequenceProgress->setValue(0);
    m_sequenceProgress->setMaximumHeight(8);
    m_sequenceProgress->setTextVisible(false);
    m_sequenceProgress->setStyleSheet(
        "QProgressBar { border: none; background: #1a2530; border-radius: 4px; }"
        "QProgressBar::chunk { background: #00b4d8; border-radius: 4px; }");
    txLayout->addWidget(m_sequenceProgress);

    layout->addWidget(txGroup);
}

void DigitalModePanel::buildDecodePanel(QWidget* container)
{
    auto* decodeGroup = new QGroupBox("Band Activity", bodyWidget());
    auto* decodeLayout = new QVBoxLayout(decodeGroup);
    decodeLayout->setSpacing(4);

    // Upper panel - decoded callsigns
    auto* decodeLabel = new QLabel("Decoded Stations:", decodeGroup);
    decodeLabel->setStyleSheet("color: #a0b4c4; font-size: 10px; font-weight: bold; padding: 0;");
    decodeLayout->addWidget(decodeLabel);

    m_decodeLog = new QTextEdit(decodeGroup);
    m_decodeLog->setReadOnly(true);
    m_decodeLog->setMinimumHeight(150);
    m_decodeLog->setFont(QFont("Consolas, Courier New, monospace", 10));
    decodeLayout->addWidget(m_decodeLog);

    // Lower panel - RX message text
    m_rxMsgLabel = new QLabel("RX Messages:", decodeGroup);
    m_rxMsgLabel->setStyleSheet("color: #a0b4c4; font-size: 10px; font-weight: bold; padding: 0;");
    decodeLayout->addWidget(m_rxMsgLabel);

    m_rxMsgLog = new QTextEdit(decodeGroup);
    m_rxMsgLog->setReadOnly(true);
    m_rxMsgLog->setMinimumHeight(120);
    m_rxMsgLog->setFont(QFont("Consolas, Courier New, monospace", 10));
    decodeLayout->addWidget(m_rxMsgLog);

    m_clearBtn = new QPushButton("Clear", decodeGroup);
    m_clearBtn->setMinimumHeight(28);
    m_clearBtn->setFixedWidth(80);
    auto* clearRow = new QHBoxLayout;
    clearRow->addStretch();
    clearRow->addWidget(m_clearBtn);
    decodeLayout->addLayout(clearRow);

    container->layout()->addWidget(decodeGroup);
}

void DigitalModePanel::buildStatusBar(QVBoxLayout* layout)
{
    m_statusLabel = new QLabel("Ready", bodyWidget());
    m_statusLabel->setAlignment(Qt::AlignLeft);
    m_statusLabel->setMinimumHeight(24);
    layout->addWidget(m_statusLabel);
}

void DigitalModePanel::applyStyles()
{
    QString style = QStringLiteral(
        "QGroupBox { color: #c0d0e0; border: 1px solid #2a3540; border-radius: 5px; "
        "margin-top: 12px; padding-top: 14px; font-weight: bold; font-size: 11px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        "QLineEdit { background: #1a2530; color: #e7f1fb; border: 1px solid #2a3540; "
        "border-radius: 4px; padding: 4px 8px; font-size: 13px; }"
        "QComboBox { background: #1a2530; color: #e7f1fb; border: 1px solid #2a3540; "
        "border-radius: 4px; padding: 4px 8px; font-size: 12px; }"
        "QComboBox::drop-down { border: none; }"
        "QPushButton { background: #1a2530; color: #e7f1fb; border: 1px solid #2a3540; "
        "border-radius: 4px; padding: 6px 14px; font-size: 12px; }"
        "QPushButton:hover { background: #243040; }"
        "QSpinBox { background: #1a2530; color: #e7f1fb; border: 1px solid #2a3540; "
        "border-radius: 4px; padding: 4px 8px; font-size: 12px; }"
        "QCheckBox { color: #e7f1fb; font-size: 13px; }"
        "QTextEdit { background: #0d1520; color: #e7f1fb; border: 1px solid #2a3540; "
        "border-radius: 4px; padding: 4px; font-size: 12px; }"
        "QTextEdit:focus { border-color: #00b4d8; }"
        "QLabel { color: #c0d0e0; font-size: 11px; background: transparent; }"
        "QSplitter::handle { background: #2a3540; width: 3px; }");

    bodyWidget()->setStyleSheet(style);
}

void DigitalModePanel::wireSignals()
{
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DigitalModePanel::onModeChanged);

    connect(m_txEnableCheck, &QCheckBox::toggled,
            this, &DigitalModePanel::onTxEnableToggled);

    connect(m_dxCallEdit, &QLineEdit::textChanged,
            this, &DigitalModePanel::onDxCallChanged);

    connect(m_genMsgBtn, &QPushButton::clicked,
            this, &DigitalModePanel::onGenerateMessages);

    connect(m_messageCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DigitalModePanel::onTxMessageSelected);

    connect(m_clearBtn, &QPushButton::clicked,
            this, &DigitalModePanel::onClearDecodes);

    connect(m_rxFreqSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &DigitalModePanel::onRxFrequencyChanged);

    connect(m_txFreqSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &DigitalModePanel::onTxFrequencyChanged);

    connect(m_freqPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DigitalModePanel::onFreqPresetChanged);

    connect(m_engine, &DigitalModeEngine::decodeReady,
            this, &DigitalModePanel::handleDecode);

    connect(m_engine, &DigitalModeEngine::snrUpdated,
            this, &DigitalModePanel::handleSnrUpdated);

    connect(m_engine, &DigitalModeEngine::syncDetected,
            this, &DigitalModePanel::handleSyncDetected);

    connect(m_engine, &DigitalModeEngine::trStateChanged,
            this, &DigitalModePanel::handleTrStateChanged);

    connect(m_engine, &DigitalModeEngine::sequenceProgressUpdated,
            this, &DigitalModePanel::handleSequenceProgress);

    connect(m_engine, &DigitalModeEngine::syncQualityChanged,
            this, &DigitalModePanel::handleSyncQuality);

    connect(m_engine, &DigitalModeEngine::pttRequested, this, [this](bool on) {
        m_radio->transmitModel().setMox(on);
        emit txStateChanged(on);
    });
}

void DigitalModePanel::setAttachedSlice(SliceModel* slice)
{
    m_attachedSlice = slice;
    if (slice) {
        m_attachedSliceId = slice->sliceId();
        m_engine->setDialFrequency(static_cast<uint64_t>(slice->frequency() * 1e6));
        m_dialFreqLabel->setText(QString("%1 Hz").arg(static_cast<uint64_t>(slice->frequency() * 1e6)));
    }
}

void DigitalModePanel::populateFreqPresets()
{
    m_freqPresetCombo->blockSignals(true);
    m_freqPresetCombo->clear();

    struct FreqPreset { QString band; double mhz; };
    QVector<FreqPreset> presets;

    switch (m_engine->mode()) {
    case DigitalModeEngine::Mode::FT8:
        presets = {{"160m FT8", 1.840}, {"80m FT8", 3.573}, {"60m FT8", 5.357},
                   {"40m FT8", 7.074}, {"30m FT8", 10.136}, {"20m FT8", 14.074},
                   {"17m FT8", 18.100}, {"15m FT8", 21.074}, {"12m FT8", 24.915},
                   {"10m FT8", 28.074}, {"6m FT8", 50.313}, {"2m FT8", 144.174}};
        break;
    case DigitalModeEngine::Mode::FT4:
        presets = {{"80m FT4", 3.575}, {"40m FT4", 7.047}, {"30m FT4", 10.140},
                   {"20m FT4", 14.080}, {"17m FT4", 18.104}, {"15m FT4", 21.080},
                   {"12m FT4", 24.919}, {"10m FT4", 28.180}, {"6m FT4", 50.318},
                   {"2m FT4", 144.170}};
        break;
    case DigitalModeEngine::Mode::JT65:
        presets = {{"160m JT65", 1.838}, {"80m JT65", 3.576}, {"40m JT65", 7.076},
                   {"30m JT65", 10.139}, {"20m JT65", 14.076}, {"17m JT65", 18.102},
                   {"15m JT65", 21.076}, {"12m JT65", 24.917}, {"10m JT65", 28.076}};
        break;
    case DigitalModeEngine::Mode::JT9:
        presets = {{"160m JT9", 1.839}, {"80m JT9", 3.572}, {"40m JT9", 7.078},
                   {"30m JT9", 10.130}, {"20m JT9", 14.078}, {"17m JT9", 18.104},
                   {"15m JT9", 21.078}, {"12m JT9", 24.920}, {"10m JT9", 28.078}};
        break;
    case DigitalModeEngine::Mode::WSPR:
        presets = {{"160m WSPR", 1.838}, {"80m WSPR", 3.594}, {"60m WSPR", 5.366},
                   {"40m WSPR", 7.040}, {"30m WSPR", 10.140}, {"20m WSPR", 14.097},
                   {"17m WSPR", 18.106}, {"15m WSPR", 21.096}, {"12m WSPR", 24.926},
                   {"10m WSPR", 28.126}, {"6m WSPR", 50.293}, {"2m WSPR", 144.489}};
        break;
    default:
        presets = {{"20m FT8", 14.074}, {"40m FT8", 7.074}, {"30m FT8", 10.136}};
        break;
    }

    for (const auto& p : presets) {
        m_freqPresetCombo->addItem(QString("%1  %2 MHz").arg(p.band, -14).arg(p.mhz, 7, 'f', 3), p.mhz * 1e6);
    }
    m_freqPresetCombo->blockSignals(false);
}

void DigitalModePanel::onFreqPresetChanged(int index)
{
    if (index < 0 || !m_radio) return;

    double mhz = m_freqPresetCombo->itemData(index).toDouble() / 1e6;
    uint64_t freqHz = static_cast<uint64_t>(mhz * 1e6);

    m_engine->setDialFrequency(freqHz);
    m_dialFreqLabel->setText(QString("%1 Hz").arg(freqHz));

    if (m_attachedSlice) {
        m_attachedSlice->tuneAndRecenter(mhz);
    } else if (auto* s = m_radio->slice(0)) {
        s->tuneAndRecenter(mhz);
    }
}

void DigitalModePanel::onModeChanged(int index)
{
    auto mode = static_cast<DigitalModeEngine::Mode>(m_modeCombo->itemData(index).toInt());
    m_engine->setMode(mode);
    m_trPeriodLabel->setText(QString("T/R Period: %1s").arg(m_engine->trPeriodSeconds()));

    populateFreqPresets();
    m_engine->reset();
}

void DigitalModePanel::onTxEnableToggled(bool checked)
{
    m_engine->enableTx(checked);
    m_txStatusLabel->setText(checked ? "TX ENABLED" : "RX");
    m_txStatusLabel->setStyleSheet(checked
        ? "font-weight: bold; font-size: 14px; padding: 4px; color: #d84848; background: #3a1010; border-radius: 4px;"
        : "font-weight: bold; font-size: 14px; padding: 4px; color: #20c060; background: #102810; border-radius: 4px;");
}

void DigitalModePanel::onDxCallChanged(const QString& text)
{
    m_engine->setDxCall(text.toUpper().trimmed());
}

void DigitalModePanel::onGenerateMessages()
{
    m_messageCombo->clear();

    QStringList msgs = generateStandardMessages();
    for (const auto& msg : msgs) {
        m_messageCombo->addItem(msg);
    }

    if (m_messageCombo->count() > 0) {
        m_messageCombo->setCurrentIndex(0);
    }
}

void DigitalModePanel::onTxMessageSelected(int index)
{
    if (index >= 0 && index < m_messageCombo->count()) {
        m_engine->setTxMessage(m_messageCombo->currentText());
    }
}

void DigitalModePanel::onRxFrequencyChanged(int dfHz)
{
    m_engine->setRxOffset(static_cast<uint32_t>(dfHz));
}

void DigitalModePanel::onTxFrequencyChanged(int dfHz)
{
    m_engine->setTxOffset(static_cast<uint32_t>(dfHz));
}

void DigitalModePanel::handleDecode(const DigitalDecode& decode)
{
    QString color = decode.lowConfidence ? "#e8a040" : "#20c060";
    QString prefix = decode.isNew ? "*" : " ";

    // Upper panel - decoded station info (callsign, SNR, freq, time)
    QString line = QString("<span style='color: %5'>%6 %1 %2 %3 dB %4 Hz</span>")
        .arg(QDateTime::currentDateTime().toString("hhmm"), -4)
        .arg(decode.mode, -5)
        .arg(decode.snrDb, 5, 'f', 0)
        .arg(decode.freqHz, 8, 'f', 0)
        .arg(color)
        .arg(prefix);

    m_decodeLog->append(line);

    // Lower panel - decoded message text
    if (!decode.message.isEmpty()) {
        if (decode.message.contains("CQ") || decode.message.contains("QRZ")) {
            m_rxMsgLog->append(QString("<span style='color: #00b4d8;'>%1</span>").arg(decode.message));
        } else {
            m_rxMsgLog->append(QString("<span style='color: #e7f1fb;'>%1</span>").arg(decode.message));
        }
    }

    auto* sb = m_decodeLog->verticalScrollBar();
    if (sb) sb->setValue(sb->maximum());
    auto* sb2 = m_rxMsgLog->verticalScrollBar();
    if (sb2) sb2->setValue(sb2->maximum());
}

void DigitalModePanel::onClearDecodes()
{
    m_decodeLog->clear();
    m_rxMsgLog->clear();
}

void DigitalModePanel::handleSnrUpdated(float snrDb)
{
    m_snrLabel->setText(QString("SNR: %1 dB").arg(snrDb, 5, 'f', 1));
}

void DigitalModePanel::handleSyncDetected(bool synced)
{
    m_syncLabel->setText(synced ? "Sync: OK" : "Sync: --");
    m_syncLabel->setStyleSheet(synced
        ? "color: #20c060; font-size: 11px;"
        : "color: #d84848; font-size: 11px;");
}

void DigitalModePanel::handleTrStateChanged(DigitalModeEngine::TrState state)
{
    switch (state) {
    case DigitalModeEngine::TrState::Rx:
        m_txStatusLabel->setText("RX");
        m_txStatusLabel->setStyleSheet("font-weight: bold; font-size: 14px; padding: 4px; color: #20c060; background: #102810; border-radius: 4px;");
        m_sequenceProgress->setStyleSheet(
            "QProgressBar { border: none; background: #1a2530; border-radius: 4px; }"
            "QProgressBar::chunk { background: #00b4d8; border-radius: 4px; }");
        break;
    case DigitalModeEngine::TrState::PreTx:
        m_txStatusLabel->setText("PRE-TX");
        m_txStatusLabel->setStyleSheet("font-weight: bold; font-size: 14px; padding: 4px; color: #e8a040; background: #302010; border-radius: 4px;");
        m_sequenceProgress->setStyleSheet(
            "QProgressBar { border: none; background: #1a2530; border-radius: 4px; }"
            "QProgressBar::chunk { background: #e8a040; border-radius: 4px; }");
        break;
    case DigitalModeEngine::TrState::Tx:
        m_txStatusLabel->setText("TX");
        m_txStatusLabel->setStyleSheet("font-weight: bold; font-size: 14px; padding: 4px; color: #d84848; background: #3a1010; border-radius: 4px;");
        m_sequenceProgress->setStyleSheet(
            "QProgressBar { border: none; background: #1a2530; border-radius: 4px; }"
            "QProgressBar::chunk { background: #d84848; border-radius: 4px; }");
        break;
    default:
        break;
    }
}

void DigitalModePanel::handleSequenceProgress(int secondsRemaining, double progress)
{
    m_timerLabel->setText(QString("Next TX: %1s  Sync: %2/21").arg(secondsRemaining).arg(m_engine->nsync()));
    m_sequenceProgress->setValue(static_cast<int>(progress * 1000.0));
}

void DigitalModePanel::handleSyncQuality(int quality)
{
    Q_UNUSED(quality);
}

QStringList DigitalModePanel::generateStandardMessages() const
{
    QStringList msgs;
    QString call = m_dxCallEdit->text().trimmed().toUpper();
    QString grid = m_dxGridEdit->text().trimmed().toUpper();

    if (call.isEmpty()) {
        msgs << "CQ FT8 FN42";
        msgs << "CQ DX FN42";
        return msgs;
    }

    msgs << QString("%1 %2").arg(call).arg(grid);
    msgs << QString("CQ %1 %2").arg(call).arg(grid);

    return msgs;
}

} // namespace MasterSDR
