#include "gui/DigitalModePanel.h"
#include "core/AppSettings.h"
#include "core/AudioEngine.h"
#include "models/RadioModel.h"
#include "models/SliceModel.h"
#include "models/TransmitModel.h"
#include "gui/SpectrumWidget.h"
#include "gui/FramelessResizer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QSplitter>
#include <QScrollBar>
#include <QGridLayout>
#include <QApplication>
#include <QDebug>

namespace MasterSDR {

const QString DigitalModePanel::DIALOG_TITLE = QStringLiteral("Digital Modes - WSJT-X Style");
const QString DigitalModePanel::GEOMETRY_KEY = QStringLiteral("DigitalModePanelGeometry");
const char* DigitalModePanel::BAND_NAMES[10] = {
    "160m", "80m", "60m", "40m", "30m", "20m", "17m", "15m", "12m", "10m"
};

DigitalModePanel::DigitalModePanel(AudioEngine* audio, RadioModel* radio,
                                   SliceModel* initialSlice, SpectrumWidget* spectrum,
                                   QWidget* parent)
    : PersistentDialog(DIALOG_TITLE, GEOMETRY_KEY, parent)
    , m_audio(audio)
    , m_radio(radio)
    , m_spectrum(spectrum)
{
    setMinimumSize(1100, 700);
    resize(1150, 780);

    m_engine = new DigitalModeEngine(this);
    setupUi();
    applyStyles();
    wireSignals();
    populateFreqPresets();
    setAttachedSlice(initialSlice);

    FramelessResizer::install(this);
    m_engine->start();
}

DigitalModePanel::~DigitalModePanel() { m_engine->stop(); }

void DigitalModePanel::setupUi()
{
    auto* body = bodyWidget();
    auto* root = new QHBoxLayout(body);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(4);

    // Left panel (takes more space - no center spectrum)
    auto* leftWidget = new QWidget(body);
    auto* leftLayout = new QVBoxLayout(leftWidget);
    leftLayout->setContentsMargins(4, 4, 2, 4);
    leftLayout->setSpacing(3);
    buildLeftPanel(leftLayout);

    // Right panel (decode windows + DX call)
    auto* rightWidget = new QWidget(body);
    auto* rightLayout = new QVBoxLayout(rightWidget);
    rightLayout->setContentsMargins(2, 4, 4, 4);
    rightLayout->setSpacing(4);
    buildRightPanel(rightLayout);

    root->addWidget(leftWidget, 3);
    root->addWidget(rightWidget, 4);
}

void DigitalModePanel::buildLeftPanel(QVBoxLayout* leftLayout)
{
    auto* bandGroup = new QGroupBox("Band", bodyWidget());
    buildBandButtons(bandGroup);
    leftLayout->addWidget(bandGroup);

    auto* modeGroup = new QGroupBox("Mode", bodyWidget());
    buildModeButtons(modeGroup);
    leftLayout->addWidget(modeGroup);

    auto* periodGroup = new QGroupBox("T/R", bodyWidget());
    buildPeriodSelector(periodGroup);
    leftLayout->addWidget(periodGroup);

    auto* freqGroup = new QGroupBox("Freq", bodyWidget());
    buildFrequencyControls(freqGroup);
    leftLayout->addWidget(freqGroup);

    auto* msgGroup = new QGroupBox("Messages", bodyWidget());
    buildMessageSlots(msgGroup);
    leftLayout->addWidget(msgGroup);

    auto* txrxGroup = new QGroupBox("Control", bodyWidget());
    buildTxRxControls(txrxGroup);
    leftLayout->addWidget(txrxGroup);

    leftLayout->addStretch();
}

void DigitalModePanel::buildBandButtons(QGroupBox* group)
{
    auto* grid = new QGridLayout(group);
    grid->setSpacing(2);
    for (int i = 0; i < 10; ++i) {
        m_bandBtns[i] = new QPushButton(BAND_NAMES[i], group);
        m_bandBtns[i]->setMinimumHeight(22);
        m_bandBtns[i]->setMinimumWidth(44);
        m_bandBtns[i]->setCheckable(true);
        m_bandBtns[i]->setFont(QFont("Arial", 8));
        grid->addWidget(m_bandBtns[i], i / 2, i % 2);
    }
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
}

void DigitalModePanel::buildModeButtons(QGroupBox* group)
{
    auto* gl = new QHBoxLayout(group);
    gl->setSpacing(2);
    m_modeGroup = new QButtonGroup(this);

    m_ft8Btn   = new QRadioButton("FT8",  group);
    m_ft4Btn   = new QRadioButton("FT4",  group);
    m_jt65Btn  = new QRadioButton("JT65", group);
    m_jt9Btn   = new QRadioButton("JT9",  group);
    m_wsprBtn  = new QRadioButton("WSPR", group);

    m_modeGroup->addButton(m_ft8Btn, 0);
    m_modeGroup->addButton(m_ft4Btn, 1);
    m_modeGroup->addButton(m_jt65Btn, 2);
    m_modeGroup->addButton(m_jt9Btn, 3);
    m_modeGroup->addButton(m_wsprBtn, 4);

    gl->addWidget(m_ft8Btn);
    gl->addWidget(m_ft4Btn);
    gl->addWidget(m_jt65Btn);
    gl->addWidget(m_jt9Btn);
    gl->addWidget(m_wsprBtn);

    m_ft8Btn->setChecked(true);

    m_trPeriodLabel = new QLabel("T/R: 15s", group);
    m_trPeriodLabel->setAlignment(Qt::AlignCenter);
    m_trPeriodLabel->setStyleSheet("font-size: 10px; color: #a0b4c4;");
    group->layout()->addWidget(m_trPeriodLabel);
}

void DigitalModePanel::buildPeriodSelector(QGroupBox* group)
{
    auto* gl = new QHBoxLayout(group);
    gl->setSpacing(2);
    m_periodGroup = new QButtonGroup(this);

    m_period15s  = new QRadioButton("15s", group);
    m_period30s  = new QRadioButton("30s", group);
    m_period60s  = new QRadioButton("60s", group);
    m_period120s = new QRadioButton("120s", group);

    m_periodGroup->addButton(m_period15s, 0);
    m_periodGroup->addButton(m_period30s, 1);
    m_periodGroup->addButton(m_period60s, 2);
    m_periodGroup->addButton(m_period120s, 3);

    gl->addWidget(m_period15s);
    gl->addWidget(m_period30s);
    gl->addWidget(m_period60s);
    gl->addWidget(m_period120s);
    m_period15s->setChecked(true);
}

void DigitalModePanel::buildFrequencyControls(QGroupBox* group)
{
    auto* fl = new QVBoxLayout(group);

    m_freqPresetCombo = new QComboBox(group);
    m_freqPresetCombo->setMinimumHeight(24);
    fl->addWidget(m_freqPresetCombo);

    m_dialFreqLabel = new QLabel("14.074.000", group);
    m_dialFreqLabel->setAlignment(Qt::AlignCenter);
    fl->addWidget(m_dialFreqLabel);

    auto* ff = new QFormLayout;
    m_rxFreqSpin = new QSpinBox(group);
    m_rxFreqSpin->setRange(0, 5000);
    m_rxFreqSpin->setValue(1500);
    m_rxFreqSpin->setSuffix(" Hz");
    ff->addRow("Rx:", m_rxFreqSpin);

    m_txFreqSpin = new QSpinBox(group);
    m_txFreqSpin->setRange(0, 5000);
    m_txFreqSpin->setValue(1500);
    m_txFreqSpin->setSuffix(" Hz");
    ff->addRow("Tx:", m_txFreqSpin);

    m_fTolSpin = new QSpinBox(group);
    m_fTolSpin->setRange(10, 500);
    m_fTolSpin->setValue(100);
    m_fTolSpin->setSuffix(" Hz");
    ff->addRow("Tol:", m_fTolSpin);
    fl->addLayout(ff);
}

void DigitalModePanel::buildMessageSlots(QGroupBox* group)
{
    auto* gl = new QVBoxLayout(group);
    m_msgSlotGroup = new QButtonGroup(this);

    const char* labels[] = {"Tx1", "Tx2", "Tx3", "Tx4", "Tx5", "Tx6"};
    for (int i = 0; i < 6; ++i) {
        auto* row = new QHBoxLayout;
        m_msgRb[i] = new QRadioButton(labels[i], group);
        m_msgRb[i]->setFixedWidth(42);
        m_msgSlotGroup->addButton(m_msgRb[i], i);
        row->addWidget(m_msgRb[i]);

        m_msgEdit[i] = new QLineEdit(group);
        m_msgEdit[i]->setMinimumHeight(22);
        m_msgEdit[i]->setPlaceholderText("message");
        row->addWidget(m_msgEdit[i], 1);

        m_msgSendBtn[i] = new QPushButton(">", group);
        m_msgSendBtn[i]->setFixedWidth(22);
        m_msgSendBtn[i]->setFixedHeight(22);
        row->addWidget(m_msgSendBtn[i]);
        gl->addLayout(row);
    }
    m_msgRb[0]->setChecked(true);

    m_genMsgBtn = new QPushButton("Gen Msgs", group);
    gl->addWidget(m_genMsgBtn);
}

void DigitalModePanel::buildTxRxControls(QGroupBox* group)
{
    auto* gl = new QVBoxLayout(group);

    m_txEnableCheck = new QCheckBox("Enable Tx", group);
    gl->addWidget(m_txEnableCheck);

    m_haltTxBtn = new QPushButton("Halt Tx", group);
    gl->addWidget(m_haltTxBtn);

    m_tuneBtn = new QPushButton("Tune", group);
    m_tuneBtn->setCheckable(true);
    gl->addWidget(m_tuneBtn);

    auto* pwRow = new QHBoxLayout;
    pwRow->addWidget(new QLabel("dBm:", group));
    m_txPowerSpin = new QSpinBox(group);
    m_txPowerSpin->setRange(0, 60);
    m_txPowerSpin->setValue(37);
    m_txPowerSpin->setMaximumWidth(50);
    pwRow->addWidget(m_txPowerSpin);
    gl->addLayout(pwRow);

    m_monitorCheck = new QCheckBox("Monitor", group);
    m_monitorCheck->setChecked(true);
    gl->addWidget(m_monitorCheck);

    m_autoSeqCheck = new QCheckBox("Auto Seq", group);
    gl->addWidget(m_autoSeqCheck);

    m_clearBtn = new QPushButton("Clear", group);
    gl->addWidget(m_clearBtn);

    m_txStatusLabel = new QLabel("RX", group);
    m_txStatusLabel->setAlignment(Qt::AlignCenter);
    m_txStatusLabel->setMinimumHeight(20);
    gl->addWidget(m_txStatusLabel);

    m_sequenceProgress = new QProgressBar(group);
    m_sequenceProgress->setRange(0, 1000);
    m_sequenceProgress->setMaximumHeight(6);
    m_sequenceProgress->setTextVisible(false);
    gl->addWidget(m_sequenceProgress);

    m_timerLabel = new QLabel("Next: --", group);
    m_timerLabel->setAlignment(Qt::AlignCenter);
    gl->addWidget(m_timerLabel);

    // SNR and Sync display
    m_snrLabel = new QLabel("SNR: --- dB", group);
    m_snrLabel->setAlignment(Qt::AlignCenter);
    m_snrLabel->setStyleSheet("font-size: 10px; color: #a0b4c4;");
    gl->addWidget(m_snrLabel);

    m_syncLabel = new QLabel("Sync: --", group);
    m_syncLabel->setAlignment(Qt::AlignCenter);
    m_syncLabel->setStyleSheet("font-size: 10px;");
    gl->addWidget(m_syncLabel);

    m_statusLabel = new QLabel("Ready", group);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setStyleSheet("font-size: 10px; color: #a0b4c4;");
    gl->addWidget(m_statusLabel);
}

void DigitalModePanel::buildRightPanel(QVBoxLayout* rightLayout)
{
    auto* dxGroup = new QGroupBox("DX Station", bodyWidget());
    buildDxCallPanel(dxGroup);
    rightLayout->addWidget(dxGroup);

    auto* decodeGroup = new QGroupBox("Band Activity", bodyWidget());
    buildDecodePanels(decodeGroup);
    rightLayout->addWidget(decodeGroup, 1);
}

void DigitalModePanel::buildDxCallPanel(QGroupBox* group)
{
    auto* fl = new QFormLayout(group);

    m_dxCallEdit = new QLineEdit(group);
    m_dxCallEdit->setPlaceholderText("DX Call");
    fl->addRow("Call:", m_dxCallEdit);

    m_dxGridEdit = new QLineEdit(group);
    m_dxGridEdit->setPlaceholderText("Grid");
    fl->addRow("Grid:", m_dxGridEdit);

    auto* btnRow = new QHBoxLayout;
    m_lookupBtn = new QPushButton("Lookup", group);
    m_addBtn = new QPushButton("Add", group);
    btnRow->addWidget(m_lookupBtn);
    btnRow->addWidget(m_addBtn);
    fl->addRow("", btnRow);
}

void DigitalModePanel::buildDecodePanels(QGroupBox* group)
{
    auto* dl = new QVBoxLayout(group);
    dl->setSpacing(2);

    auto* bandLabel = new QLabel("Band Activity", group);
    bandLabel->setStyleSheet("font-weight: bold; font-size: 10px; color: #a0b4c4;");
    dl->addWidget(bandLabel);

    m_decodeLog = new QTextEdit(group);
    m_decodeLog->setReadOnly(true);
    m_decodeLog->setMinimumHeight(140);
    m_decodeLog->setFont(QFont("Consolas, Courier New, monospace", 9));
    dl->addWidget(m_decodeLog);

    auto* rxLabel = new QLabel("RX Frequency", group);
    rxLabel->setStyleSheet("font-weight: bold; font-size: 10px; color: #a0b4c4;");
    dl->addWidget(rxLabel);

    m_rxMsgLog = new QTextEdit(group);
    m_rxMsgLog->setReadOnly(true);
    m_rxMsgLog->setMinimumHeight(100);
    m_rxMsgLog->setFont(QFont("Consolas, Courier New, monospace", 9));
    dl->addWidget(m_rxMsgLog);
}

void DigitalModePanel::applyStyles()
{
    QString style = QStringLiteral(
        "QGroupBox { color: #c0d0e0; border: 1px solid #2a3540; border-radius: 5px; "
        "margin-top: 10px; padding-top: 12px; font-weight: bold; font-size: 10px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 8px; padding: 0 3px; }"
        "QLineEdit { background: #1a2530; color: #e7f1fb; border: 1px solid #2a3540; "
        "border-radius: 3px; padding: 2px 6px; font-size: 11px; }"
        "QComboBox { background: #1a2530; color: #e7f1fb; border: 1px solid #2a3540; "
        "border-radius: 3px; padding: 2px 6px; font-size: 11px; }"
        "QPushButton { background: #1a2530; color: #e7f1fb; border: 1px solid #2a3540; "
        "border-radius: 3px; padding: 3px 8px; font-size: 10px; min-height: 20px; }"
        "QPushButton:hover { background: #243040; }"
        "QPushButton:checked { background: #00b4d8; color: #0d1520; }"
        "QSpinBox { background: #1a2530; color: #e7f1fb; border: 1px solid #2a3540; "
        "border-radius: 3px; padding: 2px 4px; font-size: 11px; }"
        "QCheckBox { color: #e7f1fb; font-size: 11px; }"
        "QRadioButton { color: #e7f1fb; font-size: 11px; }"
        "QTextEdit { background: #0d1520; color: #e7f1fb; border: 1px solid #2a3540; "
        "border-radius: 3px; padding: 2px; font-size: 10px; }"
        "QLabel { color: #c0d0e0; font-size: 10px; background: transparent; }"
        "QSplitter::handle { background: #2a3540; width: 2px; }"
        "QProgressBar { border: none; background: #1a2530; border-radius: 3px; }"
        "QProgressBar::chunk { background: #00b4d8; border-radius: 3px; }");

    bodyWidget()->setStyleSheet(style);
}

void DigitalModePanel::wireSignals()
{
    connect(m_modeGroup, QOverload<int>::of(&QButtonGroup::idClicked),
            this, &DigitalModePanel::onModeChanged);

    for (int i = 0; i < 10; ++i) {
        connect(m_bandBtns[i], &QPushButton::clicked, this, [this, i] {
            onBandButtonClicked(i);
        });
    }

    connect(m_txEnableCheck, &QCheckBox::toggled, this, &DigitalModePanel::onTxEnableToggled);
    connect(m_dxCallEdit, &QLineEdit::textChanged, this, &DigitalModePanel::onDxCallChanged);
    connect(m_dxGridEdit, &QLineEdit::textChanged, this, &DigitalModePanel::onDxGridChanged);
    connect(m_genMsgBtn, &QPushButton::clicked, this, &DigitalModePanel::onGenerateMessages);
    connect(m_clearBtn, &QPushButton::clicked, this, &DigitalModePanel::onClearDecodes);
    connect(m_haltTxBtn, &QPushButton::clicked, this, &DigitalModePanel::onHaltTxClicked);
    connect(m_tuneBtn, &QPushButton::toggled, this, &DigitalModePanel::onTuneClicked);
    connect(m_monitorCheck, &QCheckBox::toggled, this, &DigitalModePanel::onMonitorToggled);
    connect(m_autoSeqCheck, &QCheckBox::toggled, this, &DigitalModePanel::onAutoSeqToggled);
    connect(m_txPowerSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &DigitalModePanel::onTxPowerChanged);

    connect(m_msgSlotGroup, QOverload<int>::of(&QButtonGroup::idClicked),
            this, &DigitalModePanel::onTxMessageSlotChanged);

    for (int i = 0; i < 6; ++i) {
        connect(m_msgSendBtn[i], &QPushButton::clicked, this, [this, i] {
            onTxMessageSendClicked(i);
        });
    }

    connect(m_rxFreqSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &DigitalModePanel::onRxFrequencyChanged);
    connect(m_txFreqSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &DigitalModePanel::onTxFrequencyChanged);
    connect(m_freqPresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &DigitalModePanel::onFreqPresetChanged);

    connect(m_engine, &DigitalModeEngine::decodeReady, this, &DigitalModePanel::handleDecode);
    connect(m_engine, &DigitalModeEngine::snrUpdated, this, &DigitalModePanel::handleSnrUpdated);
    connect(m_engine, &DigitalModeEngine::syncDetected, this, &DigitalModePanel::handleSyncDetected);
    connect(m_engine, &DigitalModeEngine::trStateChanged, this, &DigitalModePanel::handleTrStateChanged);
    connect(m_engine, &DigitalModeEngine::sequenceProgressUpdated, this, &DigitalModePanel::handleSequenceProgress);
    connect(m_engine, &DigitalModeEngine::syncQualityChanged, this, &DigitalModePanel::handleSyncQuality);

    connect(m_engine, &DigitalModeEngine::pttRequested, this, [this](bool on) {
        m_radio->transmitModel().setMox(on);
        emit txStateChanged(on);
    });
}

void DigitalModePanel::setAttachedSlice(SliceModel* slice)
{
    m_attachedSlice = slice;
    if (slice) {
        m_engine->setDialFrequency(static_cast<uint64_t>(slice->frequency() * 1e6));
        m_dialFreqLabel->setText(QString::number(slice->frequency(), 'f', 3));
    }
}

void DigitalModePanel::onBandButtonClicked(int bandIndex)
{
    if (bandIndex < 0 || bandIndex > 9) return;

    // Auto-select frequency based on band + current mode
    double mhz = BAND_FREQS[bandIndex];
    DigitalModeEngine::Mode mode = m_engine->mode();

    // Adjust frequency per mode per band (standard WSJT-X frequencies)
    struct BandModeFreq { double mhz; };
    static const BandModeFreq ft8Freqs[10] = {
        {1.840}, {3.573}, {5.357}, {7.074}, {10.136}, {14.074},
        {18.100}, {21.074}, {24.915}, {28.074}
    };
    static const BandModeFreq ft4Freqs[10] = {
        {1.844}, {3.575}, {5.359}, {7.047}, {10.140}, {14.080},
        {18.104}, {21.080}, {24.919}, {28.180}
    };
    static const BandModeFreq jt65Freqs[10] = {
        {1.838}, {3.576}, {5.366}, {7.076}, {10.139}, {14.076},
        {18.102}, {21.076}, {24.917}, {28.076}
    };
    static const BandModeFreq wsprFreqs[10] = {
        {1.838}, {3.594}, {5.366}, {7.040}, {10.140}, {14.097},
        {18.106}, {21.096}, {24.926}, {28.126}
    };

    switch (mode) {
    case DigitalModeEngine::Mode::FT4:  mhz = ft4Freqs[bandIndex].mhz; break;
    case DigitalModeEngine::Mode::JT65:
    case DigitalModeEngine::Mode::JT9:  mhz = jt65Freqs[bandIndex].mhz; break;
    case DigitalModeEngine::Mode::WSPR: mhz = wsprFreqs[bandIndex].mhz; break;
    default: mhz = ft8Freqs[bandIndex].mhz; break;
    }

    m_engine->setDialFrequency(static_cast<uint64_t>(mhz * 1e6));
    m_dialFreqLabel->setText(QString::number(mhz, 'f', 3));

    if (m_attachedSlice) m_attachedSlice->tuneAndRecenter(mhz);
    else if (auto* s = m_radio->slice(0)) s->tuneAndRecenter(mhz);

    // Update the frequency combo to match
    for (int i = 0; i < m_freqPresetCombo->count(); ++i) {
        double itemMhz = m_freqPresetCombo->itemData(i).toDouble() / 1e6;
        if (qAbs(itemMhz - mhz) < 0.001) {
            m_freqPresetCombo->setCurrentIndex(i);
            break;
        }
    }

    for (int i = 0; i < 10; ++i) m_bandBtns[i]->setChecked(i == bandIndex);
}

void DigitalModePanel::onModeChanged(int index)
{
    static const DigitalModeEngine::Mode modes[] = {
        DigitalModeEngine::Mode::FT8, DigitalModeEngine::Mode::FT4,
        DigitalModeEngine::Mode::JT65, DigitalModeEngine::Mode::JT9,
        DigitalModeEngine::Mode::WSPR
    };
    if (index < 0 || index > 4) return;
    m_engine->setMode(modes[index]);
    m_trPeriodLabel->setText(QString("T/R: %1s").arg(m_engine->trPeriodSeconds(), 0, 'f', 0));
    populateFreqPresets();
    m_engine->reset();
}

void DigitalModePanel::onTxEnableToggled(bool checked)
{
    m_engine->enableTx(checked);
}

void DigitalModePanel::onDxCallChanged(const QString& text)
{
    m_engine->setDxCall(text.toUpper().trimmed());
}

void DigitalModePanel::onDxGridChanged(const QString& text)
{
    m_engine->setDxGrid(text.toUpper().trimmed());
}

void DigitalModePanel::onGenerateMessages()
{
    QStringList msgs = generateStandardMessages();
    for (int i = 0; i < qMin(msgs.size(), 6); ++i)
        m_msgEdit[i]->setText(msgs[i]);
}

void DigitalModePanel::onTxMessageSlotChanged(int slot)
{
    m_currentTxSlot = slot;
}

void DigitalModePanel::onTxMessageSendClicked(int slot)
{
    if (slot < 0 || slot > 5) return;
    m_engine->setTxMessage(m_msgEdit[slot]->text().trimmed());
}

void DigitalModePanel::onClearDecodes()
{
    m_decodeLog->clear();
    m_rxMsgLog->clear();
}

void DigitalModePanel::onRxFrequencyChanged(int dfHz)
{
    m_engine->setRxOffset(static_cast<uint32_t>(dfHz));
}

void DigitalModePanel::onTxFrequencyChanged(int dfHz)
{
    m_engine->setTxOffset(static_cast<uint32_t>(dfHz));
}

void DigitalModePanel::onFreqPresetChanged(int index)
{
    if (index < 0 || !m_radio) return;
    double mhz = m_freqPresetCombo->itemData(index).toDouble() / 1e6;
    uint64_t freqHz = static_cast<uint64_t>(mhz * 1e6);
    m_engine->setDialFrequency(freqHz);
    m_dialFreqLabel->setText(QString::number(mhz, 'f', 3));
    if (m_attachedSlice) m_attachedSlice->tuneAndRecenter(mhz);
    else if (auto* s = m_radio->slice(0)) s->tuneAndRecenter(mhz);
}

void DigitalModePanel::onMonitorToggled(bool on) { m_monitoring = on; }
void DigitalModePanel::onAutoSeqToggled(bool on) { Q_UNUSED(on); }
void DigitalModePanel::onHaltTxClicked() { m_engine->enableTx(false); m_txEnableCheck->setChecked(false); }
void DigitalModePanel::onTuneClicked(bool on) { Q_UNUSED(on); }
void DigitalModePanel::onTxPowerChanged(int dbm) { Q_UNUSED(dbm); }

void DigitalModePanel::handleDecode(const DigitalDecode& decode)
{
    QString color = decode.lowConfidence ? "#e8a040" : "#20c060";
    QString prefix = decode.isNew ? "*" : " ";

    QString line = QString("<span style='color:%1'>%2 %3 %4 dB %5 Hz</span>")
        .arg(color).arg(prefix)
        .arg(decode.mode, -4)
        .arg(decode.snrDb, 4, 'f', 0)
        .arg(decode.freqHz, 7, 'f', 0);

    m_decodeLog->append(line);

    if (!decode.message.isEmpty()) {
        bool isCq = decode.message.contains("CQ") || decode.message.contains("QRZ");
        m_rxMsgLog->append(QString("<span style='color:%1'>%2</span>")
            .arg(isCq ? "#00b4d8" : "#e7f1fb").arg(decode.message));
    }

    auto* sb = m_decodeLog->verticalScrollBar(); if (sb) sb->setValue(sb->maximum());
    auto* sb2 = m_rxMsgLog->verticalScrollBar(); if (sb2) sb2->setValue(sb2->maximum());
}

void DigitalModePanel::handleSnrUpdated(float snrDb)
{
    m_snrLabel->setText(QString("SNR: %1 dB").arg(snrDb, 5, 'f', 1));
}

void DigitalModePanel::handleSyncDetected(bool synced)
{
    m_syncLabel->setText(synced ? "Sync: OK" : "Sync: --");
    m_syncLabel->setStyleSheet(synced ? "color: #20c060;" : "color: #d84848;");
}

void DigitalModePanel::handleTrStateChanged(DigitalModeEngine::TrState state)
{
    switch (state) {
    case DigitalModeEngine::TrState::Rx:
        m_txStatusLabel->setText("RX");
        m_txStatusLabel->setStyleSheet("color: #20c060; background: #102010; border-radius:3px; padding:2px;");
        break;
    case DigitalModeEngine::TrState::PreTx:
        m_txStatusLabel->setText("PRE-TX");
        m_txStatusLabel->setStyleSheet("color: #e8a040; background: #302010; border-radius:3px; padding:2px;");
        break;
    case DigitalModeEngine::TrState::Tx:
        m_txStatusLabel->setText("TX");
        m_txStatusLabel->setStyleSheet("color: #d84848; background: #3a1010; border-radius:3px; padding:2px;");
        break;
    default: break;
    }
}

void DigitalModePanel::handleSequenceProgress(int secRemaining, double progress)
{
    m_timerLabel->setText(QString("Next: %1s").arg(secRemaining));
    m_sequenceProgress->setValue(static_cast<int>(progress * 1000.0));
}

void DigitalModePanel::handleSyncQuality(int quality) { Q_UNUSED(quality); }

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
                   {"17m FT8", 18.100}, {"15m FT8", 21.074}, {"12m FT8", 24.915}, {"10m FT8", 28.074}};
        break;
    case DigitalModeEngine::Mode::FT4:
        presets = {{"80m FT4", 3.575}, {"40m FT4", 7.047}, {"20m FT4", 14.080}, {"15m FT4", 21.080}};
        break;
    case DigitalModeEngine::Mode::JT65:
        presets = {{"80m JT65", 3.576}, {"40m JT65", 7.076}, {"20m JT65", 14.076}};
        break;
    case DigitalModeEngine::Mode::JT9:
        presets = {{"80m JT9", 3.572}, {"40m JT9", 7.078}, {"20m JT9", 14.078}};
        break;
    case DigitalModeEngine::Mode::WSPR:
        presets = {{"80m WSPR", 3.594}, {"40m WSPR", 7.040}, {"20m WSPR", 14.097}};
        break;
    default:
        presets = {{"20m FT8", 14.074}, {"40m FT8", 7.074}};
        break;
    }

    for (const auto& p : presets)
        m_freqPresetCombo->addItem(QString("%1 %2 MHz").arg(p.band, -14).arg(p.mhz, 7, 'f', 3), p.mhz * 1e6);

    m_freqPresetCombo->blockSignals(false);
}

QStringList DigitalModePanel::generateStandardMessages() const
{
    QStringList msgs;
    QString call = m_dxCallEdit->text().trimmed().toUpper();
    QString grid = m_dxGridEdit->text().trimmed().toUpper();

    if (call.isEmpty()) {
        msgs << "CQ TEST FN42" << "CQ DX FN42" << "" << "" << "" << "";
        return msgs;
    }

    msgs << QString("CQ %1 %2").arg(call).arg(grid);
    msgs << QString("%1 %2 %3").arg(call).arg(call).arg(grid);
    msgs << QString("%1 %2 -12").arg(call).arg(grid);
    msgs << QString("%1 %2 R-08").arg(call).arg(grid);
    msgs << QString("%1 %2 RR73").arg(call).arg(grid);
    msgs << QString("%1 %2 73").arg(call).arg(grid);
    return msgs;
}

void DigitalModePanel::setTxMessageSlot(int slot, const QString& msg)
{
    if (slot >= 0 && slot < 6) m_msgEdit[slot]->setText(msg);
}

} // namespace MasterSDR
