#include "gui/LogbookLoginDialog.h"
#include "core/AppSettings.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QDesktopServices>
#include <QUrl>
#include <QApplication>
#include <QDebug>

namespace MasterSDR {

const QString LogbookLoginDialog::SETTINGS_KEY = QStringLiteral("LogbookApiKey");

LogbookLoginDialog::LogbookLoginDialog(QWidget* parent)
    : QDialog(parent)
    , m_client(new LogbookClient(this))
{
    setupUi();
    applyStyles();

    setWindowTitle("MasterSDR - Integracao Logbook");
    setMinimumSize(520, 420);
    setMaximumSize(580, 520);
    resize(540, 450);

    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
}

void LogbookLoginDialog::setApiKey(const QString& key)
{
    m_apiKeyEdit->setText(key);
}

void LogbookLoginDialog::setupUi()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(12);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    auto* titleLabel = new QLabel("Configuracao do Logbook", this);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: #e7f1fb;");
    titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(titleLabel);

    m_instructionLabel = new QLabel(this);
    m_instructionLabel->setWordWrap(true);
    m_instructionLabel->setText(
        "Para integrar o MasterSDR com o logbook online, voce precisa de uma chave API.<br><br>"
        "1. Acesse <a href='https://logbook-portal.onrender.com/' style='color: #00b4d8;'>https://logbook-portal.onrender.com/</a><br>"
        "2. Faca login ou crie sua conta<br>"
        "3. Gere sua chave API no portal<br>"
        "4. Cole a chave abaixo<br><br>"
        "<b>Documentacao da API:</b><br>"
        "Use a chave no header: <code>Authorization: Bearer &lt;key&gt;</code><br>"
        "<code>POST /api/rest/qso</code> - Enviar QSO<br>"
        "<code>POST /api/rest/qso/bulk</code> - Enviar multiplos QSOs<br>"
        "<code>GET /api/rest/qso</code> - Listar QSOs<br>"
        "<code>GET /api/rest/stats</code> - Estatisticas<br>"
        "<code>DELETE /api/rest/qso</code> - Remover QSOs"
    );
    m_instructionLabel->setStyleSheet("color: #a0b4c4; font-size: 11px; line-height: 1.4;");
    mainLayout->addWidget(m_instructionLabel);

    auto* portalBtn = new QPushButton("Abrir Portal do Logbook", this);
    portalBtn->setCursor(Qt::PointingHandCursor);
    portalBtn->setStyleSheet(
        "QPushButton { background: transparent; color: #00b4d8; border: 1px solid #00b4d8; "
        "border-radius: 4px; padding: 6px 12px; font-size: 11px; }"
        "QPushButton:hover { background: rgba(0, 180, 216, 0.15); }");
    connect(portalBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(QUrl("https://logbook-portal.onrender.com/"));
    });
    mainLayout->addWidget(portalBtn);

    auto* keyGroup = new QGroupBox("Chave API", this);
    auto* keyLayout = new QVBoxLayout(keyGroup);

    m_apiKeyEdit = new QLineEdit(this);
    m_apiKeyEdit->setPlaceholderText("Cole sua chave API aqui...");
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    m_apiKeyEdit->setMinimumHeight(32);
    keyLayout->addWidget(m_apiKeyEdit);

    auto* showKeyBtn = new QPushButton("Mostrar/Esconder", this);
    showKeyBtn->setFixedWidth(120);
    showKeyBtn->setCursor(Qt::PointingHandCursor);
    showKeyBtn->setStyleSheet("font-size: 10px; padding: 2px 4px;");
    connect(showKeyBtn, &QPushButton::clicked, this, [this]() {
        m_apiKeyEdit->setEchoMode(m_apiKeyEdit->echoMode() == QLineEdit::Password
            ? QLineEdit::Normal : QLineEdit::Password);
    });
    keyLayout->addWidget(showKeyBtn);

    mainLayout->addWidget(keyGroup);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 0);
    m_progressBar->setMaximumHeight(6);
    m_progressBar->setTextVisible(false);
    m_progressBar->setVisible(false);
    m_progressBar->setStyleSheet(
        "QProgressBar { border: none; background: #1a2530; border-radius: 3px; }"
        "QProgressBar::chunk { background: #00b4d8; border-radius: 3px; }");
    mainLayout->addWidget(m_progressBar);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    m_statusLabel->setVisible(false);
    mainLayout->addWidget(m_statusLabel);

    mainLayout->addStretch();

    auto* buttonLayout = new QHBoxLayout;
    buttonLayout->setSpacing(10);

    m_skipBtn = new QPushButton("Pular / Usar Depois", this);
    m_skipBtn->setMinimumHeight(36);
    buttonLayout->addWidget(m_skipBtn);

    m_validateBtn = new QPushButton("Validar Chave e Conectar", this);
    m_validateBtn->setMinimumHeight(36);
    m_validateBtn->setDefault(true);
    buttonLayout->addWidget(m_validateBtn);

    mainLayout->addLayout(buttonLayout);

    connect(m_validateBtn, &QPushButton::clicked, this, &LogbookLoginDialog::onValidateClicked);
    connect(m_skipBtn, &QPushButton::clicked, this, &LogbookLoginDialog::onSkipClicked);
}

void LogbookLoginDialog::applyStyles()
{
    setStyleSheet(
        "LogbookLoginDialog { background-color: #0d1520; }"
        "QGroupBox { color: #a0b4c4; border: 1px solid #2a3540; border-radius: 6px; "
        "margin-top: 10px; padding-top: 16px; font-size: 12px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; }"
        "QLineEdit { background: #1a2530; color: #e7f1fb; border: 1px solid #2a3540; "
        "border-radius: 4px; padding: 6px 10px; font-size: 13px; }"
        "QLineEdit:focus { border-color: #00b4d8; }"
        "QPushButton { background: #1a2530; color: #e7f1fb; border: 1px solid #2a3540; "
        "border-radius: 4px; padding: 8px 16px; font-size: 12px; }"
        "QPushButton:hover { background: #243040; }"
        "QPushButton:default { background: #00b4d8; color: #0d1520; font-weight: bold; "
        "border-color: #00b4d8; }"
        "QPushButton:default:hover { background: #00c8f0; }");
}

void LogbookLoginDialog::onValidateClicked()
{
    if (m_validating) return;

    QString key = apiKey();
    if (key.isEmpty()) {
        m_statusLabel->setText("Por favor, insira uma chave API.");
        m_statusLabel->setStyleSheet("color: #e8a040; font-size: 12px; padding: 6px;");
        m_statusLabel->setVisible(true);
        return;
    }

    m_validating = true;
    m_validateBtn->setEnabled(false);
    m_progressBar->setVisible(true);
    m_statusLabel->setText("Validando chave API...");
    m_statusLabel->setStyleSheet("color: #a0b4c4; font-size: 12px; padding: 6px;");
    m_statusLabel->setVisible(true);

    m_client->setApiKey(key);
    m_client->validateKey([this](bool valid, const QString& message) {
        m_validating = false;
        m_validateBtn->setEnabled(true);
        m_progressBar->setVisible(false);

        if (valid) {
            m_statusLabel->setText("Chave API valida! Integracao configurada com sucesso.");
            m_statusLabel->setStyleSheet("color: #20c060; font-size: 12px; padding: 6px;");

            auto& s = AppSettings::instance();
            s.setValue(SETTINGS_KEY, apiKey());
            s.save();

            m_accepted = true;
            QTimer::singleShot(1200, this, &QDialog::accept);
        } else {
            m_statusLabel->setText("Falha na validacao: " + message);
            m_statusLabel->setStyleSheet("color: #d84848; font-size: 12px; padding: 6px;");
        }
    });
}

void LogbookLoginDialog::onSkipClicked()
{
    m_accepted = true;
    reject();
}

} // namespace MasterSDR
