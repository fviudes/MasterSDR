#pragma once

#include "core/RadioDiscovery.h"
#include "core/SmartLinkClient.h"
#include "core/HermesDiscovery.h"

#include <QWidget>
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QButtonGroup>
#include <QCommandLinkButton>
#include <QStackedWidget>
#include <QToolButton>
#include <QSpinBox>
#include <QFormLayout>

class QVBoxLayout;

namespace MasterSDR {

// Novice-first dialog for local, SmartLink, and manual/VPN radio connections.
class ConnectionPanel : public QWidget {
    Q_OBJECT

public:
    explicit ConnectionPanel(QWidget* parent = nullptr);

    void setFramelessMode(bool on);
    void setConnected(bool connected);
    void setStatusText(const QString& text);
    void probeRadio(const QString& ip);
    void setHermesDiscovery(HermesDiscovery* discovery);

protected:
    void paintEvent(QPaintEvent* event) override;
    bool event(QEvent* e) override;

public slots:
    void onRadioDiscovered(const RadioInfo& radio);
    void onRadioUpdated(const RadioInfo& radio);
    void onRadioLost(const QString& serial);

    // SmartLink
    void setSmartLinkClient(SmartLinkClient* client);

    // Hermes Lite 2
    void onHermesDiscovered(const HermesRadioInfo& radio);
    void onHermesLost(const QString& ipAddress);

signals:
    void connectRequested(const RadioInfo& radio);
    void wanConnectRequested(const WanRadioInfo& radio);
    void wanDisconnectClientsRequested(const WanRadioInfo& radio);
    void disconnectRequested();
    void routedRadioFound(const RadioInfo& radio);
    void retryDiscoveryRequested();
    void networkDiagnosticsRequested();
    void smartLinkLoginRequested(const QString& email, const QString& password);
    void hermesConnectRequested(const HermesRadioInfo& radio);
    void hermesManualConnectRequested(const QString& ip, uint16_t port);
    void icomIpConnectRequested(const QString& ip, uint16_t ctrlPort,
                                uint16_t rxPort, uint16_t txPort,
                                const QString& username, const QString& password,
                                const QString& model);
    void serialCatConnectRequested(const QString& portName, qint32 baudRate,
                                   const QString& protocolType, uint8_t civAddr = 0x70);

private slots:
    void onConnectionModeClicked(int id);
    void onListSelectionChanged();
    void onWanSelectionChanged();
    void onLocalConnectClicked();
    void onWanConnectClicked();
    void onWanDisconnectClientsClicked();
    void onManualIpChanged(const QString& ip);
    void onManualConnectClicked();
    void onManualAdvancedToggled(bool checked);
    void onHermesConnectClicked();
    void onHermesListSelectionChanged();
    void onHermesManualConnectClicked();
    void onIcomIpConnectClicked();
    void onSerialCatConnectClicked();
    void onSerialCatProtocolChanged(int index);
    void onSerialPortRefreshClicked();
    void onIcomModelChanged(int index);

private:
    enum ConnectionMode {
        FlexLocal = 0,
        SmartLink = 1,
        FlexManual = 2,
        IcomIp = 3,
        Hermes = 4,
        SerialCat = 5
    };

    void setCurrentMode(ConnectionMode mode);
    void updateLocalPageState();
    void updateSmartLinkUi();
    void updateActionState();
    void updateLowBandwidthVisibility();
    void updateManualAdvancedVisibility();
    void refreshManualSourceOptions(const RadioBindSettings* selected = nullptr);
    void applySavedSourceSelection(const QString& ip);
    RadioBindSettings currentManualBindSettings(bool* staleSelection = nullptr) const;
    void loadRecentManualIps();
    void rememberManualIp(const QString& ip);
    void saveManualProfile(const QString& targetIp,
                           const RadioBindSettings& settings,
                           const QHostAddress& lastSuccessfulLocalIp);
    void saveLowBandwidthPreference(bool enabled);
    void setManualMessage(const QString& text, bool error = false);
    QString formatLocalRadioLabel(const RadioInfo& radio) const;
    QString formatWanRadioLabel(const WanRadioInfo& radio) const;

    QWidget*     m_titleBar{nullptr};
    QVBoxLayout* m_rootLayout{nullptr};

    QButtonGroup* m_modeButtons{nullptr};
    QStackedWidget* m_modeStack{nullptr};
    QCommandLinkButton* m_flexLocalBtn{nullptr};
    QCommandLinkButton* m_smartLinkBtn{nullptr};
    QCommandLinkButton* m_flexManualBtn{nullptr};
    QCommandLinkButton* m_icomIpBtn{nullptr};
    QCommandLinkButton* m_hermesBtn{nullptr};
    QCommandLinkButton* m_serialCatBtn{nullptr};
    QComboBox* m_serialPortCombo{nullptr};
    QPushButton* m_serialRefreshBtn{nullptr};
    QComboBox* m_serialBaudCombo{nullptr};
    QComboBox* m_serialProtocolCombo{nullptr};
    QComboBox* m_icomModelCombo{nullptr};
    QSpinBox* m_civAddrSpin{nullptr};
    QWidget* m_icomConfigWidget{nullptr};
    QPushButton* m_serialCatConnectBtn{nullptr};
    QLabel* m_serialCatStatusLabel{nullptr};

    // Icom via IP
    QWidget* m_icomIpPage{nullptr};
    QLineEdit* m_icomIpAddr{nullptr};
    QSpinBox* m_icomCtrlPort{nullptr};
    QSpinBox* m_icomRxPort{nullptr};
    QSpinBox* m_icomTxPort{nullptr};
    QLineEdit* m_icomUsername{nullptr};
    QLineEdit* m_icomPassword{nullptr};
    QComboBox* m_icomIpModelCombo{nullptr};
    QPushButton* m_icomIpConnectBtn{nullptr};
    QLabel* m_icomIpStatusLabel{nullptr};

    SmartLinkClient* m_smartLink{nullptr};
    HermesDiscovery* m_hermesDiscovery{nullptr};

    QLabel*      m_statusLabel;
    QPushButton* m_disconnectBtn{nullptr};
    QListWidget* m_radioList{nullptr};
    QStackedWidget* m_localStateStack{nullptr};
    QWidget* m_localEmptyState{nullptr};
    QPushButton* m_localConnectBtn{nullptr};
    QList<RadioInfo> m_radios;
    bool m_connected{false};

    // SmartLink
    QWidget*     m_loginForm{nullptr};
    QLineEdit*   m_emailEdit{nullptr};
    QLineEdit*   m_passwordEdit{nullptr};
    QPushButton* m_loginBtn{nullptr};
    QPushButton* m_logoutBtn{nullptr};
    QLabel*      m_slUserLabel{nullptr};
    QListWidget* m_wanList{nullptr};
    QLabel*      m_smartLinkEmptyLabel{nullptr};
    QPushButton* m_wanDisconnectClientsBtn{nullptr};
    QPushButton* m_wanConnectBtn{nullptr};
    QList<WanRadioInfo> m_wanRadios;

    // Manual Flex
    QComboBox*   m_manualIpCombo{nullptr};
    QLineEdit*   m_manualIpEdit{nullptr};
    QLabel*      m_manualResultLabel{nullptr};
    QToolButton* m_manualAdvancedToggle{nullptr};
    QWidget*     m_manualAdvancedWidget{nullptr};
    QComboBox*   m_manualSourceCombo{nullptr};
    QLabel*      m_manualSourceWarningLabel{nullptr};
    QPushButton* m_manualConnectBtn{nullptr};
    QString      m_manualProfileIp;
    bool         m_manualConnectPending{false};

    QCheckBox*   m_autoConnectCheck{nullptr};

    // Hermes
    QListWidget* m_hermesList{nullptr};
    QPushButton* m_hermesConnectBtn{nullptr};
    QLabel* m_hermesEmptyLabel{nullptr};
    QLineEdit* m_hermesManualIp{nullptr};
    QLineEdit* m_hermesManualPort{nullptr};
    QPushButton* m_hermesManualConnectBtn{nullptr};
    QList<HermesRadioInfo> m_hermesRadios;

    QWidget*     m_linkOptionsWidget{nullptr};
    QLabel*      m_lowBwHintLabel{nullptr};
    QCheckBox*   m_lowBwCheck{nullptr};
};

} // namespace MasterSDR
