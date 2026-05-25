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

class QVBoxLayout;
class QHBoxLayout;
class QGroupBox;

namespace MasterSDR {

class AudioEngine;
class RadioModel;
class SliceModel;

class DigitalModePanel : public PersistentDialog {
    Q_OBJECT

public:
    explicit DigitalModePanel(AudioEngine* audio,
                              RadioModel* radio,
                              SliceModel* initialSlice = nullptr,
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
    void onTxEnableToggled(bool checked);
    void onDxCallChanged(const QString& text);
    void onGenerateMessages();
    void onTxMessageSelected(int index);
    void onClearDecodes();
    void onRxFrequencyChanged(int dfHz);
    void onTxFrequencyChanged(int dfHz);
    void onFreqPresetChanged(int index);

    void handleDecode(const DigitalDecode& decode);
    void handleSnrUpdated(float snrDb);
    void handleSyncDetected(bool synced);

private:
    void setupUi();
    void applyStyles();
    void buildModeSelector(QVBoxLayout* layout);
    void buildFrequencyPanel(QVBoxLayout* layout);
    void buildCallPanel(QVBoxLayout* layout);
    void buildMessagePanel(QVBoxLayout* layout);
    void buildTxPanel(QVBoxLayout* layout);
    void buildDecodePanel(QWidget* container);
    void buildStatusBar(QVBoxLayout* layout);
    void wireSignals();
    void populateFreqPresets();
    QStringList generateStandardMessages() const;

    AudioEngine* m_audio{nullptr};
    RadioModel* m_radio{nullptr};
    DigitalModeEngine* m_engine{nullptr};
    QPointer<SliceModel> m_attachedSlice;
    int m_attachedSliceId{-1};

    // Mode panel
    QComboBox* m_modeCombo{nullptr};
    QLabel* m_trPeriodLabel{nullptr};

    // Frequency panel
    QComboBox* m_freqPresetCombo{nullptr};
    QLabel* m_dialFreqLabel{nullptr};
    QSpinBox* m_rxFreqSpin{nullptr};
    QSpinBox* m_txFreqSpin{nullptr};

    // Call panel
    QLineEdit* m_dxCallEdit{nullptr};
    QLineEdit* m_dxGridEdit{nullptr};
    QLabel* m_snrLabel{nullptr};
    QLabel* m_syncLabel{nullptr};

    // Message panel
    QComboBox* m_messageCombo{nullptr};
    QPushButton* m_genMsgBtn{nullptr};

    // TX panel
    QCheckBox* m_txEnableCheck{nullptr};
    QPushButton* m_tuneBtn{nullptr};
    QLabel* m_txStatusLabel{nullptr};

    // Decode panel
    QTextEdit* m_decodeLog{nullptr};
    QTextEdit* m_rxMsgLog{nullptr};
    QLabel* m_rxMsgLabel{nullptr};
    QPushButton* m_clearBtn{nullptr};

    // Status
    QLabel* m_statusLabel{nullptr};

    QStringList m_pendingMessages;
    bool m_darkTheme{true};
};

} // namespace MasterSDR
