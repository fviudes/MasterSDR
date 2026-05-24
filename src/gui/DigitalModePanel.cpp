#include "gui/DigitalModePanel.h"
#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
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
    auto* txLayout = new QHBoxLayout(txGroup);

    m_txEnableCheck = new QCheckBox("Enable TX", txGroup);
    m_txEnableCheck->setMinimumHeight(28);
    txLayout->addWidget(m_txEnableCheck);

    m_tuneBtn = new QPushButton("Tune", txGroup);
    m_tuneBtn->setCheckable(true);
    m_tuneBtn->setMinimumHeight(28);
    m_tuneBtn->setFixedWidth(80);
    txLayout->addWidget(m_tuneBtn);

    m_txStatusLabel = new QLabel("RX", txGroup);
    m_txStatusLabel->setAlignment(Qt::AlignCenter);
    m_txStatusLabel->setMinimumHeight(28);
    m_txStatusLabel->setStyleSheet("font-weight: bold; font-size: 14px; padding: 4px;");
    txLayout->addWidget(m_txStatusLabel);

    layout->addWidget(txGroup);
}

void DigitalModePanel::buildDecodePanel(QWidget* container)
{
    auto* decodeGroup = new QGroupBox("Band Activity", bodyWidget());
    auto* decodeLayout = new QVBoxLayout(decodeGroup);

    m_decodeLog = new QTextEdit(decodeGroup);
    m_decodeLog->setReadOnly(true);
    m_decodeLog->setMinimumHeight(300);
    m_decodeLog->setFont(QFont("Consolas, Courier New, monospace", 10));
    decodeLayout->addWidget(m_decodeLog);

    m_clearBtn = new QPushButton("Clear", decodeGroup);
    m_clearBtn->setMinimumHeight(28);
    m_clearBtn->setFixedWidth(80);
    auto* clearRow = new QHBoxLayout;
    clearRow->addStretch();
    clearRow->addWidget(m_clearBtn);
    decodeLayout->addLayout(clearRow);

    container->layout()->addWidget(decodeGroup, 1);
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

    connect(m_engine, &DigitalModeEngine::decodeReady,
            this, &DigitalModePanel::handleDecode);

    connect(m_engine, &DigitalModeEngine::snrUpdated,
            this, &DigitalModePanel::handleSnrUpdated);

    connect(m_engine, &DigitalModeEngine::syncDetected,
            this, &DigitalModePanel::handleSyncDetected);

    connect(m_engine, &DigitalModeEngine::statusChanged, this, [this]() {
        m_statusLabel->setText(m_engine->isTxEnabled() ? "TX Enabled" : "Ready");
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

void DigitalModePanel::onModeChanged(int index)
{
    auto mode = static_cast<DigitalModeEngine::Mode>(m_modeCombo->itemData(index).toInt());
    m_engine->setMode(mode);
    m_trPeriodLabel->setText(QString("T/R Period: %1s").arg(m_engine->trPeriodSeconds()));

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

void DigitalModePanel::onClearDecodes()
{
    m_decodeLog->clear();
}

void DigitalModePanel::onRxFrequencyChanged(int dfHz)
{
    m_engine->setRxFrequency(static_cast<uint32_t>(dfHz));
}

void DigitalModePanel::onTxFrequencyChanged(int dfHz)
{
    m_engine->setTxFrequency(static_cast<uint32_t>(dfHz));
}

void DigitalModePanel::handleDecode(const DigitalDecode& decode)
{
    QString color = decode.lowConfidence ? "#e8a040" : "#20c060";
    QString prefix = decode.isNew ? "*" : " ";

    QString line = QString("<span style='color: %5'>%6 %1 %2 %3 dB %4 Hz</span>")
        .arg(QDateTime::currentDateTime().toString("hhmm"), -4)
        .arg(decode.mode, -5)
        .arg(decode.snrDb, 5, 'f', 0)
        .arg(decode.freqHz, 8, 'f', 0)
        .arg(color)
        .arg(prefix);

    m_decodeLog->append(line);

    if (!decode.message.isEmpty()) {
        m_decodeLog->append(QString("<span style='color: #e7f1fb;'>  %1</span>").arg(decode.message));
    }

    auto* sb = m_decodeLog->verticalScrollBar();
    if (sb) sb->setValue(sb->maximum());
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

    msgs << QString("%1 %2 %3").arg(call, grid.isEmpty() ? "CQ" : call, grid);
    msgs << QString("CQ %1 %2").arg(call, grid.isEmpty() ? "" : grid).simplified();
    msgs << QString("%1 %2 -10").arg(call, grid.isEmpty() ? "" : grid).simplified();
    msgs << QString("%1 %2 R-10").arg(call, grid.isEmpty() ? "" : grid).simplified();
    msgs << QString("%1 %2 RR73").arg(call, grid.isEmpty() ? "" : grid).simplified();
    msgs << QString("%1 %2 73").arg(call, grid.isEmpty() ? "" : grid).simplified();

    return msgs;
}

} // namespace MasterSDR
