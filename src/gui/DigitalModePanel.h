#pragma once

#include "core/DigitalModeEngine.h"
#include "gui/PersistentDialog.h"

#include <QPointer>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QTextEdit>
#include <QCheckBox>
#include <QSpinBox>
#include <QProgressBar>
#include <QRadioButton>
#include <QButtonGroup>
#include <QGroupBox>
#include <QGridLayout>

namespace MasterSDR {

class AudioEngine;
class RadioModel;
class SliceModel;
class SpectrumWidget;

class DigitalModePanel : public PersistentDialog {
    Q_OBJECT

public:
    explicit DigitalModePanel(AudioEngine* audio, RadioModel* radio,
                              SliceModel* initialSlice, SpectrumWidget* spectrum,
                              QWidget* parent = nullptr);
    ~DigitalModePanel() override;

    void setAttachedSlice(SliceModel* slice);
    void setFramelessMode(bool on);

    static const QString DIALOG_TITLE;
    static const QString GEOMETRY_KEY;

signals:
    void txStateChanged(bool transmitting);

private slots:
    void onModeChanged(int index);
    void onBandButtonClicked(int bandIndex);
    void onTxEnableToggled(bool checked);
    void onDxCallChanged(const QString& text);
    void onDxGridChanged(const QString& text);
    void onGenerateMessages();
    void onTxMessageSlotChanged(int slot);
    void onTxMessageSendClicked(int slot);
    void onClearDecodes();
    void onRxFrequencyChanged(int dfHz);
    void onTxFrequencyChanged(int dfHz);
    void onFreqPresetChanged(int index);
    void onMonitorToggled(bool on);
    void onAutoSeqToggled(bool on);
    void onHaltTxClicked();
    void onTuneClicked(bool on);
    void onTxPowerChanged(int dbm);

    void handleDecode(const DigitalDecode& decode);
    void handleSnrUpdated(float snrDb);
    void handleSyncDetected(bool synced);
    void handleTrStateChanged(DigitalModeEngine::TrState state);
    void handleSequenceProgress(int secRemaining, double progress);
    void handleSyncQuality(int quality);

private:
    void setupUi();
    void applyStyles();
    void buildLeftPanel(QVBoxLayout* leftLayout);
    void buildBandButtons(QGroupBox* group);
    void buildModeButtons(QGroupBox* group);
    void buildPeriodSelector(QGroupBox* group);
    void buildFrequencyControls(QGroupBox* group);
    void buildMessageSlots(QGroupBox* group);
    void buildTxRxControls(QGroupBox* group);
    void buildRightPanel(QVBoxLayout* rightLayout);
    void wireSignals();
    void populateFreqPresets();
    QStringList generateStandardMessages() const;
    void setTxMessageSlot(int slot, const QString& msg);

    AudioEngine* m_audio{nullptr};
    RadioModel* m_radio{nullptr};
    DigitalModeEngine* m_engine{nullptr};
    SpectrumWidget* m_spectrum{nullptr};
    QPointer<SliceModel> m_attachedSlice;

    // Band buttons (10 bands HF)
    QPushButton* m_bandBtns[10]{};
    static constexpr double BAND_FREQS[10] = {
        1.840, 3.573, 5.357, 7.074, 10.136, 14.074, 18.100, 21.074, 24.915, 28.074
    };
    static const char* BAND_NAMES[10];

    // Mode buttons
    QButtonGroup* m_modeGroup{nullptr};
    QRadioButton* m_ft8Btn{nullptr};
    QRadioButton* m_ft4Btn{nullptr};
    QRadioButton* m_jt65Btn{nullptr};
    QRadioButton* m_jt9Btn{nullptr};
    QRadioButton* m_wsprBtn{nullptr};
    QLabel* m_trPeriodLabel{nullptr};

    // T/R Period
    QButtonGroup* m_periodGroup{nullptr};
    QRadioButton* m_period15s{nullptr};
    QRadioButton* m_period30s{nullptr};
    QRadioButton* m_period60s{nullptr};
    QRadioButton* m_period120s{nullptr};

    // Frequency
    QComboBox* m_freqPresetCombo{nullptr};
    QSpinBox* m_rxFreqSpin{nullptr};
    QSpinBox* m_txFreqSpin{nullptr};
    QSpinBox* m_fTolSpin{nullptr};
    QLabel* m_dialFreqLabel{nullptr};

    // DX Call
    QLineEdit* m_dxCallEdit{nullptr};
    QLineEdit* m_dxGridEdit{nullptr};
    QPushButton* m_lookupBtn{nullptr};
    QPushButton* m_addBtn{nullptr};
    QPushButton* m_genMsgBtn{nullptr};
    QLabel* m_snrLabel{nullptr};
    QLabel* m_syncLabel{nullptr};

    // Message slots (6)
    QButtonGroup* m_msgSlotGroup{nullptr};
    QRadioButton* m_msgRb[6]{};
    QLineEdit* m_msgEdit[6]{};
    QPushButton* m_msgSendBtn[6]{};

    // TX/RX Controls
    QCheckBox* m_txEnableCheck{nullptr};
    QPushButton* m_haltTxBtn{nullptr};
    QPushButton* m_tuneBtn{nullptr};
    QCheckBox* m_monitorCheck{nullptr};
    QCheckBox* m_autoSeqCheck{nullptr};
    QSpinBox* m_txPowerSpin{nullptr};
    QPushButton* m_clearBtn{nullptr};
    QLabel* m_txStatusLabel{nullptr};
    QProgressBar* m_sequenceProgress{nullptr};
    QLabel* m_timerLabel{nullptr};

    // Decode panels
    QTextEdit* m_decodeLog{nullptr};
    QTextEdit* m_rxMsgLog{nullptr};

    // Status
    QLabel* m_statusLabel{nullptr};

    QStringList m_pendingMessages;
    int m_currentTxSlot{0};
    bool m_monitoring{true};
};

} // namespace MasterSDR
