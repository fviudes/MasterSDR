#pragma once

#include "core/LogbookClient.h"

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>

namespace MasterSDR {

class LogbookLoginDialog : public QDialog {
    Q_OBJECT

public:
    explicit LogbookLoginDialog(QWidget* parent = nullptr);

    QString apiKey() const { return m_apiKeyEdit->text().trimmed(); }
    void setApiKey(const QString& key);
    bool wasAccepted() const { return m_accepted; }

    static const QString SETTINGS_KEY;

private slots:
    void onValidateClicked();
    void onSkipClicked();

private:
    void setupUi();
    void applyStyles();

    QLineEdit* m_apiKeyEdit{nullptr};
    QPushButton* m_validateBtn{nullptr};
    QPushButton* m_skipBtn{nullptr};
    QLabel* m_statusLabel{nullptr};
    QLabel* m_instructionLabel{nullptr};
    QProgressBar* m_progressBar{nullptr};
    LogbookClient* m_client{nullptr};
    bool m_accepted{false};
    bool m_validating{false};
};

} // namespace MasterSDR
