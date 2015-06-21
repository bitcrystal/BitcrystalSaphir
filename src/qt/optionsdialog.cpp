#include "optionsdialog.h"
#include "ui_optionsdialog.h"

#include "bitcoinunits.h"
#include "monitoreddatamapper.h"
#include "netbase.h"
#include "optionsmodel.h"

#include <QDir>
#include <QIntValidator>
#include <QLocale>
#include <QMessageBox>
#include <QRegExp>
#include <QRegExpValidator>

OptionsDialog::OptionsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::OptionsDialog),
    model(0),
    mapper(0),
    fRestartWarningDisplayed_Proxy(false),
    fRestartWarningDisplayed_Lang(false),
    fProxyIpValid(true)
{
    ui->setupUi(this);

    /* Network elements init */
#ifndef USE_UPNP
    ui->mapPortUpnp->setEnabled(false);
#endif

    ui->proxyIp->setEnabled(false);
    ui->proxyPort->setEnabled(false);
    ui->proxyPort->setValidator(new QIntValidator(1, 65535, this));

    ui->socksVersion->setEnabled(false);
    ui->socksVersion->addItem("5", 5);
    ui->socksVersion->addItem("4", 4);
    ui->socksVersion->setCurrentIndex(0);

    connect(ui->connectSocks, SIGNAL(toggled(bool)), ui->proxyIp, SLOT(setEnabled(bool)));
    connect(ui->connectSocks, SIGNAL(toggled(bool)), ui->proxyPort, SLOT(setEnabled(bool)));
    connect(ui->connectSocks, SIGNAL(toggled(bool)), ui->socksVersion, SLOT(setEnabled(bool)));
    connect(ui->connectSocks, SIGNAL(clicked(bool)), this, SLOT(showRestartWarning_Proxy()));

    ui->proxyIp->installEventFilter(this);

    /* Window elements init */
#ifdef Q_OS_MAC
    ui->tabWindow->setVisible(false);
#endif

    /* Display elements init */
    QDir translations(":translations");
    ui->lang->addItem(QString("(") + tr("default") + QString(")"), QVariant(""));
    foreach(const QString &langStr, translations.entryList())
    {
        QLocale locale(langStr);

        /** check if the locale name consists of 2 parts (language_country) */
        if(langStr.contains("_"))
        {
#if QT_VERSION >= 0x040800
            /** display language strings as "native language - native country (locale name)", e.g. "Deutsch - Deutschland (de)" */
            ui->lang->addItem(locale.nativeLanguageName() + QString(" - ") + locale.nativeCountryName() + QString(" (") + langStr + QString(")"), QVariant(langStr));
#else
            /** display language strings as "language - country (locale name)", e.g. "German - Germany (de)" */
            ui->lang->addItem(QLocale::languageToString(locale.language()) + QString(" - ") + QLocale::countryToString(locale.country()) + QString(" (") + langStr + QString(")"), QVariant(langStr));
#endif
        }
        else
        {
#if QT_VERSION >= 0x040800
            /** display language strings as "native language (locale name)", e.g. "Deutsch (de)" */
            ui->lang->addItem(locale.nativeLanguageName() + QString(" (") + langStr + QString(")"), QVariant(langStr));
#else
            /** display language strings as "language (locale name)", e.g. "German (de)" */
            ui->lang->addItem(QLocale::languageToString(locale.language()) + QString(" (") + langStr + QString(")"), QVariant(langStr));
#endif
        }
    }

    ui->unit->setModel(new BitcoinUnits(this));

	ui->checkboxMiningEnabled->setEnabled(true);
    
    ui->comboMiningProcLimit->setEnabled(true);
    ui->comboMiningProcLimit->addItem("Disabled",  0);
    ui->comboMiningProcLimit->addItem("1 thread",  1);
    ui->comboMiningProcLimit->addItem("2 threads", 2);
    ui->comboMiningProcLimit->addItem("3 threads", 3);
    ui->comboMiningProcLimit->addItem("4 threads", 4);
	ui->comboMiningProcLimit->addItem("5 threads", 5);
    ui->comboMiningProcLimit->addItem("6 threads", 6);
    ui->comboMiningProcLimit->addItem("7 threads", 7);
    ui->comboMiningProcLimit->addItem("8 threads", 8);
	ui->comboMiningProcLimit->addItem("9 threads", 9);
    ui->comboMiningProcLimit->addItem("10 threads", 10);
    ui->comboMiningProcLimit->addItem("11 threads", 11);
	ui->comboMiningProcLimit->addItem("12 threads", 12);
    ui->comboMiningProcLimit->addItem("13 threads", 13);
    ui->comboMiningProcLimit->addItem("14 threads", 14);
    ui->comboMiningProcLimit->addItem("15 threads", 15);
	ui->comboMiningProcLimit->addItem("16 threads", 16);
    ui->comboMiningProcLimit->addItem("17 threads", 17);
    ui->comboMiningProcLimit->addItem("18 threads", 18);
	ui->comboMiningProcLimit->addItem("19 threads", 19);
    ui->comboMiningProcLimit->addItem("20 threads", 20);
    ui->comboMiningProcLimit->addItem("21 threads", 21);
    ui->comboMiningProcLimit->addItem("22 threads", 22);
	ui->comboMiningProcLimit->addItem("23 threads", 23);
	ui->comboMiningProcLimit->addItem("24 threads", 24);
    ui->comboMiningProcLimit->addItem("25 threads", 25);
    ui->comboMiningProcLimit->addItem("26 threads", 26);
	ui->comboMiningProcLimit->addItem("27 threads", 27);
	ui->comboMiningProcLimit->addItem("28 threads", 28);
    ui->comboMiningProcLimit->addItem("29 threads", 29);
    ui->comboMiningProcLimit->addItem("30 threads", 30);
	ui->comboMiningProcLimit->addItem("31 threads", 31);
	ui->comboMiningProcLimit->addItem("32 threads", 32);
    ui->comboMiningProcLimit->addItem("Maximum", -1);
    ui->comboMiningProcLimit->setCurrentIndex(0);
	
	ui->checkboxSMiningEnabled->setEnabled(true);
    
    ui->comboSMiningProcLimit->setEnabled(true);
    ui->comboSMiningProcLimit->addItem("Disabled", 0);
    ui->comboSMiningProcLimit->addItem("1 thread", 1);
    ui->comboSMiningProcLimit->addItem("2 threads", 2);
    ui->comboSMiningProcLimit->addItem("3 threads", 3);
    ui->comboSMiningProcLimit->addItem("4 threads", 4);
	ui->comboSMiningProcLimit->addItem("5 threads", 5);
    ui->comboSMiningProcLimit->addItem("6 threads", 6);
    ui->comboSMiningProcLimit->addItem("7 threads", 7);
    ui->comboSMiningProcLimit->addItem("8 threads", 8);
	ui->comboSMiningProcLimit->addItem("9 threads", 9);
	ui->comboSMiningProcLimit->addItem("10 threads", 10);
    ui->comboSMiningProcLimit->addItem("11 threads", 11);
	ui->comboSMiningProcLimit->addItem("12 threads", 12);
    ui->comboSMiningProcLimit->addItem("13 threads", 13);
    ui->comboSMiningProcLimit->addItem("14 threads", 14);
    ui->comboSMiningProcLimit->addItem("15 threads", 15);
	ui->comboSMiningProcLimit->addItem("16 threads", 16);
    ui->comboSMiningProcLimit->addItem("17 threads", 17);
    ui->comboSMiningProcLimit->addItem("18 threads", 18);
	ui->comboSMiningProcLimit->addItem("19 threads", 19);
    ui->comboSMiningProcLimit->addItem("20 threads", 20);
    ui->comboSMiningProcLimit->addItem("21 threads", 21);
    ui->comboSMiningProcLimit->addItem("22 threads", 22);
	ui->comboSMiningProcLimit->addItem("23 threads", 23);
	ui->comboSMiningProcLimit->addItem("24 threads", 24);
    ui->comboSMiningProcLimit->addItem("25 threads", 25);
    ui->comboSMiningProcLimit->addItem("26 threads", 26);
	ui->comboSMiningProcLimit->addItem("27 threads", 27);
	ui->comboSMiningProcLimit->addItem("28 threads", 28);
    ui->comboSMiningProcLimit->addItem("29 threads", 29);
    ui->comboSMiningProcLimit->addItem("30 threads", 30);
	ui->comboSMiningProcLimit->addItem("31 threads", 31);
	ui->comboSMiningProcLimit->addItem("32 threads", 32);
    ui->comboSMiningProcLimit->addItem("Maximum", -1);
    ui->comboSMiningProcLimit->setCurrentIndex(0);
    
    /* Widget-to-option mapper */
    mapper = new MonitoredDataMapper(this);
    mapper->setSubmitPolicy(QDataWidgetMapper::ManualSubmit);
    mapper->setOrientation(Qt::Vertical);

    /* enable apply button when data modified */
    connect(mapper, SIGNAL(viewModified()), this, SLOT(enableApplyButton()));
    /* disable apply button when new data loaded */
    connect(mapper, SIGNAL(currentIndexChanged(int)), this, SLOT(disableApplyButton()));
    /* setup/change UI elements when proxy IP is invalid/valid */
    connect(this, SIGNAL(proxyIpValid(QValidatedLineEdit *, bool)), this, SLOT(handleProxyIpValid(QValidatedLineEdit *, bool)));
}

OptionsDialog::~OptionsDialog()
{
    delete ui;
}

void OptionsDialog::setModel(OptionsModel *model)
{
    this->model = model;

    if(model)
    {
        connect(model, SIGNAL(displayUnitChanged(int)), this, SLOT(updateDisplayUnit()));

        mapper->setModel(model);
        setMapper();
        mapper->toFirst();
    }

    /* update the display unit, to not use the default ("BTC") */
    updateDisplayUnit();

    /* warn only when language selection changes by user action (placed here so init via mapper doesn't trigger this) */
    connect(ui->lang, SIGNAL(valueChanged()), this, SLOT(showRestartWarning_Lang()));

    /* disable apply button after settings are loaded as there is nothing to save */
    disableApplyButton();
}

void OptionsDialog::setMapper()
{
    /* Main */
    mapper->addMapping(ui->transactionFee, OptionsModel::Fee);
    mapper->addMapping(ui->reserveBalance, OptionsModel::ReserveBalance);
    mapper->addMapping(ui->bitcoinAtStartup, OptionsModel::StartAtStartup);
    mapper->addMapping(ui->detachDatabases, OptionsModel::DetachDatabases);

    /* Network */
    mapper->addMapping(ui->mapPortUpnp, OptionsModel::MapPortUPnP);

    mapper->addMapping(ui->connectSocks, OptionsModel::ProxyUse);
    mapper->addMapping(ui->proxyIp, OptionsModel::ProxyIP);
    mapper->addMapping(ui->proxyPort, OptionsModel::ProxyPort);
    mapper->addMapping(ui->socksVersion, OptionsModel::ProxySocksVersion);

    /* Window */
#ifndef Q_OS_MAC
    mapper->addMapping(ui->minimizeToTray, OptionsModel::MinimizeToTray);
    mapper->addMapping(ui->minimizeOnClose, OptionsModel::MinimizeOnClose);
#endif

    /* Display */
    mapper->addMapping(ui->lang, OptionsModel::Language);
    mapper->addMapping(ui->unit, OptionsModel::DisplayUnit);
    mapper->addMapping(ui->displayAddresses, OptionsModel::DisplayAddresses);
    mapper->addMapping(ui->coinControlFeatures, OptionsModel::CoinControlFeatures);
	
	/* Mining */
    mapper->addMapping(ui->checkboxMiningEnabled, OptionsModel::MiningEnabled);
    mapper->addMapping(ui->comboMiningProcLimit, OptionsModel::MiningIntensity);
	
	mapper->addMapping(ui->checkboxSMiningEnabled, OptionsModel::sMiningEnabled);
    mapper->addMapping(ui->comboSMiningProcLimit, OptionsModel::sMiningIntensity);
}

void OptionsDialog::enableApplyButton()
{
    ui->applyButton->setEnabled(true);
}

void OptionsDialog::disableApplyButton()
{
    ui->applyButton->setEnabled(false);
}

void OptionsDialog::enableSaveButtons()
{
    /* prevent enabling of the save buttons when data modified, if there is an invalid proxy address present */
    if(fProxyIpValid)
        setSaveButtonState(true);
}

void OptionsDialog::disableSaveButtons()
{
    setSaveButtonState(false);
}

void OptionsDialog::setSaveButtonState(bool fState)
{
    ui->applyButton->setEnabled(fState);
    ui->okButton->setEnabled(fState);
}

void OptionsDialog::on_okButton_clicked()
{
    mapper->submit();
    accept();
}

void OptionsDialog::on_cancelButton_clicked()
{
    reject();
}

void OptionsDialog::on_applyButton_clicked()
{
    mapper->submit();
    disableApplyButton();
}

void OptionsDialog::showRestartWarning_Proxy()
{
    if(!fRestartWarningDisplayed_Proxy)
    {
        QMessageBox::warning(this, tr("Warning"), tr("This setting will take effect after restarting BitCrystalSaphir."), QMessageBox::Ok);
        fRestartWarningDisplayed_Proxy = true;
    }
}

void OptionsDialog::showRestartWarning_Lang()
{
    if(!fRestartWarningDisplayed_Lang)
    {
        QMessageBox::warning(this, tr("Warning"), tr("This setting will take effect after restarting BitCrystalSaphir."), QMessageBox::Ok);
        fRestartWarningDisplayed_Lang = true;
    }
}

void OptionsDialog::updateDisplayUnit()
{
    if(model)
    {
        /* Update transactionFee with the current unit */
        ui->transactionFee->setDisplayUnit(model->getDisplayUnit());
    }
}

void OptionsDialog::handleProxyIpValid(QValidatedLineEdit *object, bool fState)
{
    // this is used in a check before re-enabling the save buttons
    fProxyIpValid = fState;

    if(fProxyIpValid)
    {
        enableSaveButtons();
        ui->statusLabel->clear();
    }
    else
    {
        disableSaveButtons();
        object->setValid(fProxyIpValid);
        ui->statusLabel->setStyleSheet("QLabel { color: red; }");
        ui->statusLabel->setText(tr("The supplied proxy address is invalid."));
    }
}

bool OptionsDialog::eventFilter(QObject *object, QEvent *event)
{
    if(event->type() == QEvent::FocusOut)
    {
        if(object == ui->proxyIp)
        {
            CService addr;
            /* Check proxyIp for a valid IPv4/IPv6 address and emit the proxyIpValid signal */
            emit proxyIpValid(ui->proxyIp, LookupNumeric(ui->proxyIp->text().toStdString().c_str(), addr));
        }
    }
    return QDialog::eventFilter(object, event);
}
