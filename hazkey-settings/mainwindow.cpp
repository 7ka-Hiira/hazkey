#include "mainwindow.h"

#include <qlabel.h>
#include <qnamespace.h>

#include <QAbstractButton>
#include <QCheckBox>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include "./ui_mainwindow.h"
#include "config_definitions.h"
#include "config_macros.h"
#include "serverconnector.h"

MainWindow::MainWindow(QWidget* parent)
    : QWidget(parent),
      ui_(new Ui::MainWindow),
      server_(ServerConnector()),
      isUpdatingFromAdvanced_(false),
      networkManager_(new QNetworkAccessManager(this)),
      currentDownload_(nullptr),
      downloadProgressDialog_(nullptr) {
    ui_->setupUi(this);

    // Expand table settings mode change tab
    ui_->inputTableConfigModeTabWidget->tabBar()->setExpanding(true);

    // Connect UI signals
    connectSignals();

    // Setup input table lists
    setupInputTableLists();

    // Setup keymap lists
    setupKeymapLists();

    // Load configuration
    if (!loadCurrentConfig()) {
        // If config loading fails, disable UI elements
        setEnabled(false);
        QMessageBox::critical(
            this, tr("Configuration Error"),
            tr("Failed to load configuration. Please check your "
               "connection to the hazkey server."));
    }
}

void MainWindow::connectSignals() {
    // Connect dialog buttons
    connect(ui_->dialogButtonBox, &QDialogButtonBox::accepted, this,
            &MainWindow::onApply);
    connect(ui_->dialogButtonBox, &QDialogButtonBox::clicked, this,
            &MainWindow::onButtonClicked);

    // Connect Reset button
    QPushButton* resetButton =
        ui_->dialogButtonBox->button(QDialogButtonBox::Reset);
    if (resetButton) {
        connect(resetButton, &QPushButton::clicked, this,
                &MainWindow::onResetConfiguration);
    }

    // Connect history checkbox to enable/disable dependent controls
    connect(ui_->useHistory, &QCheckBox::toggled, this,
            &MainWindow::onUseHistoryToggled);

    // Connect input table management buttons
    connect(ui_->enableTable, &QToolButton::clicked, this,
            &MainWindow::onEnableTable);
    connect(ui_->disableTable, &QToolButton::clicked, this,
            &MainWindow::onDisableTable);
    connect(ui_->tableMoveUp, &QToolButton::clicked, this,
            &MainWindow::onTableMoveUp);
    connect(ui_->tableMoveDown, &QToolButton::clicked, this,
            &MainWindow::onTableMoveDown);

    // Connect list selection changes to update button states
    connect(ui_->enabledTableList, &QListWidget::itemSelectionChanged, this,
            &MainWindow::onEnabledTableSelectionChanged);
    connect(ui_->availableTableList, &QListWidget::itemSelectionChanged, this,
            &MainWindow::onAvailableTableSelectionChanged);

    // Connect keymap management buttons
    connect(ui_->enableKeymap, &QToolButton::clicked, this,
            &MainWindow::onEnableKeymap);
    connect(ui_->disableKeymap, &QToolButton::clicked, this,
            &MainWindow::onDisableKeymap);
    connect(ui_->keymapMoveUp, &QToolButton::clicked, this,
            &MainWindow::onKeymapMoveUp);
    connect(ui_->keymapMoveDown, &QToolButton::clicked, this,
            &MainWindow::onKeymapMoveDown);

    // Connect keymap list selection changes to update button states
    connect(ui_->enabledKeymapList, &QListWidget::itemSelectionChanged, this,
            &MainWindow::onEnabledKeymapSelectionChanged);
    connect(ui_->availableKeymapList, &QListWidget::itemSelectionChanged, this,
            &MainWindow::onAvailableKeymapSelectionChanged);

    // Connect Basic tab input style changes
    connect(ui_->mainInputStyle,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onBasicInputStyleChanged);
    connect(ui_->punctuationStyle,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onBasicSettingChanged);
    connect(ui_->numberStyle,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onBasicSettingChanged);
    connect(ui_->commonSymbolStyle,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onBasicSettingChanged);
    connect(ui_->spaceStyleLabel,
            QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onBasicSettingChanged);

    // Connect submode entry point chars change to sync with Basic tab
    connect(ui_->submodeEntryPointChars, &QLineEdit::textChanged, this,
            &MainWindow::onSubmodeEntryChanged);

    // Connect special conversion buttons
    connect(ui_->checkAllConversion, &QPushButton::clicked, this,
            &MainWindow::onCheckAllConversion);
    connect(ui_->uncheckAllConversion, &QPushButton::clicked, this,
            &MainWindow::onUncheckAllConversion);

    // Connect clear learning data button
    connect(ui_->clearLearningData, &QPushButton::clicked, this,
            &MainWindow::onClearLearningData);
}

void MainWindow::onButtonClicked(QAbstractButton* button) {
    QDialogButtonBox::StandardButton standardButton =
        ui_->dialogButtonBox->standardButton(button);

    switch (standardButton) {
        case QDialogButtonBox::Ok:
            if (saveCurrentConfig()) {
                close();
            }
            break;
        case QDialogButtonBox::Apply:
            saveCurrentConfig();
            break;
        case QDialogButtonBox::Cancel:
            close();
            break;
        default:
            break;
    }
}

void MainWindow::onApply() { saveCurrentConfig(); }

void MainWindow::onUseHistoryToggled(bool enabled) {
    ui_->stopStoreNewHistory->setEnabled(enabled);
}

bool MainWindow::loadCurrentConfig() {
    auto configOpt = server_.getConfig();

    if (!configOpt.has_value()) {
        return false;
    }

    currentConfig_ = configOpt.value();
    if (currentConfig_.profiles_size() == 0) {
        return false;
    }

    currentProfile_ = currentConfig_.mutable_profiles(0);
    if (!currentProfile_) {
        return false;
    }

    // Remove any existing warning widgets on AI tab
    if (ui_->aiTabScrollContentsLayout->count() > 1) {
        QLayoutItem* item = ui_->aiTabScrollContentsLayout->itemAt(1);
        if (item && item->widget()) {
            QWidget* widget = item->widget();
            if (widget->styleSheet().contains("background-color: yellow") ||
                widget->styleSheet().contains("background-color: lightblue")) {
                ui_->aiTabScrollContentsLayout->removeWidget(widget);
                widget->deleteLater();
            }
        }
    }

    if (currentConfig_.available_zenzai_backend_devices_size() <= 0) {
        ui_->enableZenzai->setEnabled(false);
        ui_->zenzaiContextualConversion->setEnabled(false);
        ui_->zenzaiInferenceLimit->setEnabled(false);
        ui_->zenzaiUserPlofile->setEnabled(false);
        ui_->zenzaiBackendDevice->setEnabled(false);

        QWidget* warningWidget = createWarningWidget(
            tr("<b>Warning:</b> Zenzai support not installed."), "yellow");
        ui_->aiTabScrollContentsLayout->insertWidget(1, warningWidget);
    } else if (!currentConfig_.zenzai_model_available()) {
        ui_->enableZenzai->setEnabled(false);
        ui_->zenzaiContextualConversion->setEnabled(false);
        ui_->zenzaiInferenceLimit->setEnabled(false);
        ui_->zenzaiUserPlofile->setEnabled(false);
        ui_->zenzaiBackendDevice->setEnabled(false);

        QWidget* warningWidget = createWarningWidget(
            tr("<b>Warning:</b> Zenzai model not found."), "yellow",
            tr("Download Model"), [this]() { onDownloadZenzaiModel(); });
        ui_->aiTabScrollContentsLayout->insertWidget(1, warningWidget);
    } else {
        ui_->enableZenzai->setEnabled(true);
        ui_->zenzaiContextualConversion->setEnabled(true);
        ui_->zenzaiInferenceLimit->setEnabled(true);
        ui_->zenzaiUserPlofile->setEnabled(true);
        ui_->zenzaiBackendDevice->setEnabled(true);

        // Check if model needs update by comparing checksums
        QString modelPath =
            QString::fromStdString(currentConfig_.zenzai_model_path());
        if (!modelPath.isEmpty()) {
            QString currentChecksum = calculateFileSHA256(modelPath);
            QString expectedChecksum =
                "4de930c06bef8c263aa1aa40684af206db4ce1b96375b3b8ed0ea508e0b14f"
                "6c";

            if (!currentChecksum.isEmpty() &&
                currentChecksum != expectedChecksum) {
                QWidget* warningWidget = createWarningWidget(
                    tr("The current model is not the latest version."),
                    "lightblue", tr("Download Update"),
                    [this]() { onDownloadZenzaiModel(); });
                ui_->aiTabScrollContentsLayout->insertWidget(1, warningWidget);
            }
        }
    }

    // Load zenzai backend devices
    ui_->zenzaiBackendDevice->clear();
    for (int i = 0; i < currentConfig_.available_zenzai_backend_devices_size();
         ++i) {
        const auto& device = currentConfig_.available_zenzai_backend_devices(i);
        QString deviceName = QString::fromStdString(device.name());
        QString deviceDesc = QString::fromStdString(device.desc());
        QString displayText = deviceName;
        if (!deviceDesc.isEmpty()) {
            displayText += " : " + deviceDesc;
        }
        ui_->zenzaiBackendDevice->addItem(displayText, deviceName);
    }

    // Set current device selection
    QString currentDevice =
        QString::fromStdString(currentProfile_->zenzai_backend_device_name());
    if (!currentDevice.isEmpty()) {
        int index = ui_->zenzaiBackendDevice->findData(currentDevice);
        if (index >= 0) {
            ui_->zenzaiBackendDevice->setCurrentIndex(index);
        }
    }

    SET_COMBO_FROM_CONFIG(ConfigDefs::AutoConvertMode, ui_->autoConvertion,
                          currentProfile_->auto_convert_mode());
    SET_COMBO_FROM_CONFIG(ConfigDefs::AuxTextMode, ui_->auxiliaryText,
                          currentProfile_->aux_text_mode());
    SET_COMBO_FROM_CONFIG(ConfigDefs::SuggestionListMode, ui_->suggestionList,
                          currentProfile_->suggestion_list_mode());

    SET_SPINBOX(ui_->numSuggestion, currentProfile_->num_suggestions(),
                ConfigDefs::SpinboxDefaults::NUM_SUGGESTIONS);
    SET_SPINBOX(ui_->numCandidatesPerPage,
                currentProfile_->num_candidates_per_page(),
                ConfigDefs::SpinboxDefaults::NUM_CANDIDATES_PER_PAGE);
    SET_SPINBOX(ui_->zenzaiInferenceLimit,
                currentProfile_->zenzai_infer_limit(),
                ConfigDefs::SpinboxDefaults::ZENZAI_INFERENCE_LIMIT);

    SET_CHECKBOX(ui_->useHistory, currentProfile_->use_input_history(),
                 ConfigDefs::CheckboxDefaults::USE_HISTORY);
    SET_CHECKBOX(ui_->stopStoreNewHistory,
                 currentProfile_->stop_store_new_history(),
                 ConfigDefs::CheckboxDefaults::STOP_STORE_NEW_HISTORY);
    SET_CHECKBOX(ui_->enableZenzai, currentProfile_->zenzai_enable(),
                 ConfigDefs::CheckboxDefaults::ENABLE_ZENZAI);
    SET_CHECKBOX(ui_->zenzaiContextualConversion,
                 currentProfile_->zenzai_contextual_mode(),
                 ConfigDefs::CheckboxDefaults::ZENZAI_CONTEXTUAL);

    auto specialConversions = &currentProfile_->special_conversion_mode();
    SET_CHECKBOX(ui_->halfwidthKatakanaConversion,
                 specialConversions->halfwidth_katakana(),
                 ConfigDefs::CheckboxDefaults::HALFWIDTH_KATAKANA);
    SET_CHECKBOX(ui_->extendedEmojiConversion,
                 specialConversions->extended_emoji(),
                 ConfigDefs::CheckboxDefaults::EXTENDED_EMOJI);
    SET_CHECKBOX(ui_->commaSeparatedNumCoversion,
                 specialConversions->comma_separated_number(),
                 ConfigDefs::CheckboxDefaults::COMMA_SEPARATED_NUMBER);
    SET_CHECKBOX(ui_->calendarConversion, specialConversions->calendar(),
                 ConfigDefs::CheckboxDefaults::CALENDER);
    SET_CHECKBOX(ui_->timeConversion, specialConversions->time(),
                 ConfigDefs::CheckboxDefaults::TIME);
    SET_CHECKBOX(ui_->mailDomainConversion, specialConversions->mail_domain(),
                 ConfigDefs::CheckboxDefaults::MAIL_DOMAIN);
    SET_CHECKBOX(ui_->unicodeCodePointConversion,
                 specialConversions->unicode_codepoint(),
                 ConfigDefs::CheckboxDefaults::UNICODE_CODEPOINT);
    SET_CHECKBOX(ui_->romanTypographyConversion,
                 specialConversions->roman_typography(),
                 ConfigDefs::CheckboxDefaults::ROMAN_TYPOGRAPHY);
    SET_CHECKBOX(ui_->hazkeyVersionConversion,
                 specialConversions->hazkey_version(),
                 ConfigDefs::CheckboxDefaults::HAZKEY_VERSION);

    ui_->stopStoreNewHistory->setEnabled(currentProfile_->use_input_history());

    SET_LINEEDIT(ui_->submodeEntryPointChars,
                 currentProfile_->submode_entry_point_chars(),
                 "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    SET_LINEEDIT(ui_->zenzaiUserPlofile, currentProfile_->zenzai_profile(), "");

    // Load input table configuration
    loadInputTables();

    // Load keymap configuration
    loadKeymaps();

    // Sync Advanced tab settings to Basic tab
    syncAdvancedToBasic();

    return true;
}

bool MainWindow::saveCurrentConfig() {
    if (!currentProfile_) {
        QMessageBox::warning(this, tr("Error"),
                             tr("No configuration profile loaded."));
        return false;
    }

    currentProfile_->set_auto_convert_mode(
        GET_COMBO_TO_CONFIG(ConfigDefs::AutoConvertMode, ui_->autoConvertion));
    currentProfile_->set_aux_text_mode(
        GET_COMBO_TO_CONFIG(ConfigDefs::AuxTextMode, ui_->auxiliaryText));
    currentProfile_->set_suggestion_list_mode(GET_COMBO_TO_CONFIG(
        ConfigDefs::SuggestionListMode, ui_->suggestionList));

    currentProfile_->set_num_suggestions(GET_SPINBOX_INT(ui_->numSuggestion));
    currentProfile_->set_num_candidates_per_page(
        GET_SPINBOX_INT(ui_->numCandidatesPerPage));
    currentProfile_->set_zenzai_infer_limit(
        GET_SPINBOX_INT(ui_->zenzaiInferenceLimit));

    currentProfile_->set_use_input_history(GET_CHECKBOX_BOOL(ui_->useHistory));
    currentProfile_->set_stop_store_new_history(
        GET_CHECKBOX_BOOL(ui_->stopStoreNewHistory));
    currentProfile_->set_zenzai_enable(GET_CHECKBOX_BOOL(ui_->enableZenzai));
    currentProfile_->set_zenzai_contextual_mode(
        GET_CHECKBOX_BOOL(ui_->zenzaiContextualConversion));

    auto* specialConversions =
        currentProfile_->mutable_special_conversion_mode();
    specialConversions->set_halfwidth_katakana(
        GET_CHECKBOX_BOOL(ui_->halfwidthKatakanaConversion));
    specialConversions->set_extended_emoji(
        GET_CHECKBOX_BOOL(ui_->extendedEmojiConversion));
    specialConversions->set_comma_separated_number(
        GET_CHECKBOX_BOOL(ui_->commaSeparatedNumCoversion));
    specialConversions->set_calendar(
        GET_CHECKBOX_BOOL(ui_->calendarConversion));
    specialConversions->set_time(GET_CHECKBOX_BOOL(ui_->timeConversion));
    specialConversions->set_mail_domain(
        GET_CHECKBOX_BOOL(ui_->mailDomainConversion));
    specialConversions->set_unicode_codepoint(
        GET_CHECKBOX_BOOL(ui_->unicodeCodePointConversion));
    specialConversions->set_roman_typography(
        GET_CHECKBOX_BOOL(ui_->romanTypographyConversion));
    specialConversions->set_hazkey_version(
        GET_CHECKBOX_BOOL(ui_->hazkeyVersionConversion));

    currentProfile_->set_submode_entry_point_chars(
        GET_LINEEDIT_STRING(ui_->submodeEntryPointChars));
    currentProfile_->set_zenzai_profile(
        GET_LINEEDIT_STRING(ui_->zenzaiUserPlofile));

    // Save zenzai backend device
    QString selectedDevice = ui_->zenzaiBackendDevice->currentData().toString();
    currentProfile_->set_zenzai_backend_device_name(
        selectedDevice.toStdString());

    // Save input table configuration
    saveInputTables();

    // Save keymap configuration
    saveKeymaps();

    // Save to server
    try {
        server_.setCurrentConfig(currentConfig_);
        return true;
    } catch (const std::exception& e) {
        QMessageBox::critical(
            this, tr("Save Error"),
            tr("Failed to save configuration: %1").arg(e.what()));
        return false;
    } catch (...) {
        QMessageBox::critical(
            this, tr("Save Error"),
            tr("An unknown error occurred while saving configuration."));
        return false;
    }
}

void MainWindow::setupInputTableLists() {
    // Enable double-click to move items between lists
    connect(ui_->enabledTableList, &QListWidget::itemDoubleClicked, this,
            &MainWindow::onDisableTable);
    connect(ui_->availableTableList, &QListWidget::itemDoubleClicked, this,
            &MainWindow::onEnableTable);

    // Update button states initially
    updateTableButtonStates();
}

void MainWindow::loadInputTables() {
    if (!currentProfile_) {
        return;
    }

    // Clear existing items
    ui_->enabledTableList->clear();
    ui_->availableTableList->clear();

    // Create sets for quick lookup - use (name, isBuiltin) pairs for uniqueness
    QSet<QPair<QString, bool>> enabledTableKeys;

    // Load enabled tables
    for (int i = 0; i < currentProfile_->enabled_tables_size(); ++i) {
        const auto& enabledTable = currentProfile_->enabled_tables(i);
        QString tableName = QString::fromStdString(enabledTable.name());
        bool isBuiltIn = enabledTable.is_built_in();
        enabledTableKeys.insert(QPair<QString, bool>(tableName, isBuiltIn));

        QString displayName =
            translateTableName(tableName, enabledTable.is_built_in());
        QListWidgetItem* item = new QListWidgetItem(displayName);

        // Check if this table is available
        bool isAvailable = false;

        for (int j = 0; j < currentConfig_.available_tables_size(); ++j) {
            const auto& availableTable = currentConfig_.available_tables(j);
            if (availableTable.name() == enabledTable.name() &&
                availableTable.is_built_in() == enabledTable.is_built_in()) {
                isAvailable = true;
                break;
            }
        }

        // Set item appearance based on status
        if (isBuiltIn) {
            displayName = displayName + " " + tr("[built-in]");
        }
        if (!isAvailable) {
            displayName = displayName + " " + tr("[not found]");
            item->setForeground(QColor(Qt::red));
        }
        item->setText(displayName);

        // Store original name and metadata
        item->setData(Qt::UserRole, tableName);
        item->setData(Qt::UserRole + 1, isBuiltIn);
        item->setData(Qt::UserRole + 2, isAvailable);

        ui_->enabledTableList->addItem(item);
    }

    // Load available tables (excluding already enabled ones)
    for (int i = 0; i < currentConfig_.available_tables_size(); ++i) {
        const auto& availableTable = currentConfig_.available_tables(i);
        QString tableName = QString::fromStdString(availableTable.name());
        bool isBuiltIn = availableTable.is_built_in();
        QPair<QString, bool> tableKey(tableName, isBuiltIn);

        if (!enabledTableKeys.contains(tableKey)) {
            QString displayName =
                translateTableName(tableName, availableTable.is_built_in());
            QListWidgetItem* item = new QListWidgetItem(displayName);

            if (availableTable.is_built_in()) {
                item->setText(displayName + " " + tr("[built-in]"));
            }

            // Store metadata
            item->setData(Qt::UserRole, tableName);
            item->setData(Qt::UserRole + 1, availableTable.is_built_in());
            item->setData(Qt::UserRole + 2, true);  // available

            ui_->availableTableList->addItem(item);
        }
    }

    updateTableButtonStates();
}

void MainWindow::saveInputTables() {
    if (!currentProfile_) {
        return;
    }

    // Clear existing enabled tables
    currentProfile_->clear_enabled_tables();

    // Save enabled tables in order
    for (int i = 0; i < ui_->enabledTableList->count(); ++i) {
        QListWidgetItem* item = ui_->enabledTableList->item(i);
        QString tableName = item->data(Qt::UserRole).toString();
        bool isBuiltIn = item->data(Qt::UserRole + 1).toBool();
        bool isAvailable = item->data(Qt::UserRole + 2).toBool();

        auto* enabledTable = currentProfile_->add_enabled_tables();
        enabledTable->set_name(tableName.toStdString());
        enabledTable->set_is_built_in(isBuiltIn);

        // Find filename from available tables if available
        if (isAvailable) {
            for (int j = 0; j < currentConfig_.available_tables_size(); ++j) {
                const auto& availableTable = currentConfig_.available_tables(j);
                if (availableTable.name() == tableName.toStdString() &&
                    availableTable.is_built_in() == isBuiltIn) {
                    enabledTable->set_filename(availableTable.filename());
                    break;
                }
            }
        }
    }
}

void MainWindow::onEnableTable() {
    QListWidgetItem* item = ui_->availableTableList->currentItem();
    if (!item) {
        return;
    }

    // Move item from available to enabled list
    int row = ui_->availableTableList->row(item);
    ui_->availableTableList->takeItem(row);
    ui_->enabledTableList->addItem(item);

    updateTableButtonStates();
    saveInputTables();
    syncAdvancedToBasic();
}

void MainWindow::onDisableTable() {
    QListWidgetItem* item = ui_->enabledTableList->currentItem();
    if (!item) {
        return;
    }

    // Only move to available list if the table is actually available
    bool isAvailable = item->data(Qt::UserRole + 2).toBool();

    int row = ui_->enabledTableList->row(item);
    ui_->enabledTableList->takeItem(row);

    if (isAvailable) {
        // Reset display text for available list
        QString tableName = item->data(Qt::UserRole).toString();
        bool isBuiltIn = item->data(Qt::UserRole + 1).toBool();
        QString displayName = translateTableName(tableName, isBuiltIn);

        if (isBuiltIn) {
            item->setText(displayName + " " + tr("[built-in]"));
        } else {
            item->setText(displayName);
        }
        item->setForeground(QColor());  // Reset color

        ui_->availableTableList->addItem(item);
    } else {
        // Delete item if table is not available
        delete item;
    }

    updateTableButtonStates();
    saveInputTables();
    syncAdvancedToBasic();
}

void MainWindow::onTableMoveUp() {
    QListWidgetItem* item = ui_->enabledTableList->currentItem();
    if (!item) {
        return;
    }

    int row = ui_->enabledTableList->row(item);
    if (row > 0) {
        ui_->enabledTableList->takeItem(row);
        ui_->enabledTableList->insertItem(row - 1, item);
        ui_->enabledTableList->setCurrentItem(item);
    }

    updateTableButtonStates();
    saveInputTables();
    syncAdvancedToBasic();
}

void MainWindow::onTableMoveDown() {
    QListWidgetItem* item = ui_->enabledTableList->currentItem();
    if (!item) {
        return;
    }

    int row = ui_->enabledTableList->row(item);
    if (row < ui_->enabledTableList->count() - 1) {
        ui_->enabledTableList->takeItem(row);
        ui_->enabledTableList->insertItem(row + 1, item);
        ui_->enabledTableList->setCurrentItem(item);
    }

    updateTableButtonStates();
    saveInputTables();
    syncAdvancedToBasic();
}

void MainWindow::onEnabledTableSelectionChanged() { updateTableButtonStates(); }

void MainWindow::onAvailableTableSelectionChanged() {
    updateTableButtonStates();
}

void MainWindow::updateTableButtonStates() {
    QListWidgetItem* enabledItem = ui_->enabledTableList->currentItem();
    QListWidgetItem* availableItem = ui_->availableTableList->currentItem();

    // Enable/disable buttons based on selection and position
    ui_->disableTable->setEnabled(enabledItem != nullptr);
    ui_->enableTable->setEnabled(availableItem != nullptr);

    if (enabledItem) {
        int row = ui_->enabledTableList->row(enabledItem);
        ui_->tableMoveUp->setEnabled(row > 0);
        ui_->tableMoveDown->setEnabled(row <
                                       ui_->enabledTableList->count() - 1);
    } else {
        ui_->tableMoveUp->setEnabled(false);
        ui_->tableMoveDown->setEnabled(false);
    }
}

void MainWindow::setupKeymapLists() {
    // Enable double-click to move items between lists
    connect(ui_->enabledKeymapList, &QListWidget::itemDoubleClicked, this,
            &MainWindow::onDisableKeymap);
    connect(ui_->availableKeymapList, &QListWidget::itemDoubleClicked, this,
            &MainWindow::onEnableKeymap);

    // Update button states initially
    updateKeymapButtonStates();
}

void MainWindow::loadKeymaps() {
    if (!currentProfile_) {
        return;
    }

    // Clear existing items
    ui_->enabledKeymapList->clear();
    ui_->availableKeymapList->clear();

    // Create sets for quick lookup
    QSet<QPair<QString, bool>> enabledKeymapKeys;

    // Load enabled keymaps
    for (int i = 0; i < currentProfile_->enabled_keymaps_size(); ++i) {
        const auto& enabledKeymap = currentProfile_->enabled_keymaps(i);
        QString keymapName = QString::fromStdString(enabledKeymap.name());
        bool isBuiltIn = enabledKeymap.is_built_in();
        enabledKeymapKeys.insert(QPair<QString, bool>(keymapName, isBuiltIn));

        QString displayName =
            translateKeymapName(keymapName, enabledKeymap.is_built_in());
        QListWidgetItem* item = new QListWidgetItem(displayName);

        // Check if this keymap is available
        bool isAvailable = false;

        for (int j = 0; j < currentConfig_.available_keymaps_size(); ++j) {
            const auto& availableKeymap = currentConfig_.available_keymaps(j);
            if (availableKeymap.name() == enabledKeymap.name() &&
                availableKeymap.is_built_in() == enabledKeymap.is_built_in()) {
                isAvailable = true;
                isBuiltIn = availableKeymap.is_built_in();
                break;
            }
        }

        // Set item appearance based on status
        if (isBuiltIn) {
            displayName = displayName + " " + tr("[built-in]");
        }
        if (!isAvailable) {
            displayName = displayName + " " + tr("[not found]");
            item->setForeground(QColor(Qt::red));
        }
        item->setText(displayName);

        // Store original name and metadata
        item->setData(Qt::UserRole, keymapName);
        item->setData(Qt::UserRole + 1, isBuiltIn);
        item->setData(Qt::UserRole + 2, isAvailable);

        ui_->enabledKeymapList->addItem(item);
    }

    // Load available keymaps (excluding already enabled ones)
    for (int i = 0; i < currentConfig_.available_keymaps_size(); ++i) {
        const auto& availableKeymap = currentConfig_.available_keymaps(i);
        QString keymapName = QString::fromStdString(availableKeymap.name());
        bool isBuiltIn = availableKeymap.is_built_in();
        QPair<QString, bool> keymapKey(keymapName, isBuiltIn);

        if (!enabledKeymapKeys.contains(keymapKey)) {
            QString displayName =
                translateKeymapName(keymapName, availableKeymap.is_built_in());
            QListWidgetItem* item = new QListWidgetItem(displayName);

            if (availableKeymap.is_built_in()) {
                item->setText(displayName + " " + tr("[built-in]"));
            }

            // Store metadata
            item->setData(Qt::UserRole, keymapName);
            item->setData(Qt::UserRole + 1, availableKeymap.is_built_in());
            item->setData(Qt::UserRole + 2, true);  // available

            ui_->availableKeymapList->addItem(item);
        }
    }

    updateKeymapButtonStates();
}

void MainWindow::saveKeymaps() {
    if (!currentProfile_) {
        return;
    }

    // Clear existing enabled keymaps
    currentProfile_->clear_enabled_keymaps();

    // Save enabled keymaps in order
    for (int i = 0; i < ui_->enabledKeymapList->count(); ++i) {
        QListWidgetItem* item = ui_->enabledKeymapList->item(i);
        QString keymapName = item->data(Qt::UserRole).toString();
        bool isBuiltIn = item->data(Qt::UserRole + 1).toBool();
        bool isAvailable = item->data(Qt::UserRole + 2).toBool();

        auto* enabledKeymap = currentProfile_->add_enabled_keymaps();
        enabledKeymap->set_name(keymapName.toStdString());
        enabledKeymap->set_is_built_in(isBuiltIn);

        // Find filename from available keymaps if available
        if (isAvailable) {
            for (int j = 0; j < currentConfig_.available_keymaps_size(); ++j) {
                const auto& availableKeymap =
                    currentConfig_.available_keymaps(j);
                if (availableKeymap.name() == keymapName.toStdString() &&
                    availableKeymap.is_built_in() == isBuiltIn) {
                    enabledKeymap->set_filename(availableKeymap.filename());
                    break;
                }
            }
        }
    }
}

void MainWindow::onEnableKeymap() {
    QListWidgetItem* item = ui_->availableKeymapList->currentItem();
    if (!item) {
        return;
    }

    // Move item from available to enabled list
    int row = ui_->availableKeymapList->row(item);
    ui_->availableKeymapList->takeItem(row);
    ui_->enabledKeymapList->addItem(item);

    updateKeymapButtonStates();
    saveKeymaps();
    syncAdvancedToBasic();
}

void MainWindow::onDisableKeymap() {
    QListWidgetItem* item = ui_->enabledKeymapList->currentItem();
    if (!item) {
        return;
    }

    // Only move to available list if the keymap is actually available
    bool isAvailable = item->data(Qt::UserRole + 2).toBool();

    int row = ui_->enabledKeymapList->row(item);
    ui_->enabledKeymapList->takeItem(row);

    if (isAvailable) {
        // Reset display text for available list
        QString keymapName = item->data(Qt::UserRole).toString();
        bool isBuiltIn = item->data(Qt::UserRole + 1).toBool();
        QString displayName = translateKeymapName(keymapName, isBuiltIn);

        if (isBuiltIn) {
            item->setText(displayName + " " + tr("[built-in]"));
        } else {
            item->setText(displayName);
        }
        item->setForeground(QColor());  // Reset color

        ui_->availableKeymapList->addItem(item);
    } else {
        // Delete item if keymap is not available
        delete item;
    }

    updateKeymapButtonStates();
    saveKeymaps();
    syncAdvancedToBasic();
}

void MainWindow::onKeymapMoveUp() {
    QListWidgetItem* item = ui_->enabledKeymapList->currentItem();
    if (!item) {
        return;
    }

    int row = ui_->enabledKeymapList->row(item);
    if (row > 0) {
        ui_->enabledKeymapList->takeItem(row);
        ui_->enabledKeymapList->insertItem(row - 1, item);
        ui_->enabledKeymapList->setCurrentItem(item);
    }

    updateKeymapButtonStates();
    saveKeymaps();
    syncAdvancedToBasic();
}

void MainWindow::onKeymapMoveDown() {
    QListWidgetItem* item = ui_->enabledKeymapList->currentItem();
    if (!item) {
        return;
    }

    int row = ui_->enabledKeymapList->row(item);
    if (row < ui_->enabledKeymapList->count() - 1) {
        ui_->enabledKeymapList->takeItem(row);
        ui_->enabledKeymapList->insertItem(row + 1, item);
        ui_->enabledKeymapList->setCurrentItem(item);
    }

    updateKeymapButtonStates();
    saveKeymaps();
    syncAdvancedToBasic();
}

void MainWindow::onEnabledKeymapSelectionChanged() {
    updateKeymapButtonStates();
}

void MainWindow::onAvailableKeymapSelectionChanged() {
    updateKeymapButtonStates();
}

void MainWindow::updateKeymapButtonStates() {
    QListWidgetItem* enabledItem = ui_->enabledKeymapList->currentItem();
    QListWidgetItem* availableItem = ui_->availableKeymapList->currentItem();

    // Enable/disable buttons based on selection and position
    ui_->disableKeymap->setEnabled(enabledItem != nullptr);
    ui_->enableKeymap->setEnabled(availableItem != nullptr);

    if (enabledItem) {
        int row = ui_->enabledKeymapList->row(enabledItem);
        ui_->keymapMoveUp->setEnabled(row > 0);
        ui_->keymapMoveDown->setEnabled(row <
                                        ui_->enabledKeymapList->count() - 1);
    } else {
        ui_->keymapMoveUp->setEnabled(false);
        ui_->keymapMoveDown->setEnabled(false);
    }
}

// Basic tab event handlers
void MainWindow::onSubmodeEntryChanged() {
    if (isUpdatingFromAdvanced_) return;

    // Update currentProfile with the new submode entry value
    if (currentProfile_) {
        currentProfile_->set_submode_entry_point_chars(
            ui_->submodeEntryPointChars->text().toStdString());

        // Check compatibility and update warning
        syncAdvancedToBasic();
    }
}

void MainWindow::onBasicInputStyleChanged() {
    if (isUpdatingFromAdvanced_) return;

    // Enable/disable other options based on input style
    bool isKana = (ui_->mainInputStyle->currentIndex() == 1);  // JIS Kana

    // For Kana mode, only Space style can be changed
    ui_->punctuationStyle->setEnabled(!isKana);
    ui_->numberStyle->setEnabled(!isKana);
    ui_->commonSymbolStyle->setEnabled(!isKana);

    // Update labels to indicate disabled state
    if (isKana) {
        ui_->punctuationStyle->setToolTip(tr("Disabled in Kana mode"));
        ui_->numberStyle->setToolTip(tr("Disabled in Kana mode"));
        ui_->commonSymbolStyle->setToolTip(tr("Disabled in Kana mode"));
    } else {
        ui_->punctuationStyle->setToolTip("");
        ui_->numberStyle->setToolTip("");
        ui_->commonSymbolStyle->setToolTip("");
    }

    syncBasicToAdvanced();
}

void MainWindow::onBasicSettingChanged() {
    if (isUpdatingFromAdvanced_) return;

    syncBasicToAdvanced();
}

void MainWindow::resetInputStyleToDefault() {
    // Set default values (all first options)
    ui_->mainInputStyle->setCurrentIndex(0);     // Romaji
    ui_->punctuationStyle->setCurrentIndex(0);   // Kuten+Toten
    ui_->numberStyle->setCurrentIndex(0);        // Fullwidth
    ui_->commonSymbolStyle->setCurrentIndex(0);  // Fullwidth
    ui_->spaceStyleLabel->setCurrentIndex(0);    // Fullwidth

    // Re-enable all controls (since we're setting to Romaji mode)
    ui_->punctuationStyle->setEnabled(true);
    ui_->numberStyle->setEnabled(true);
    ui_->commonSymbolStyle->setEnabled(true);

    // Clear tooltips
    ui_->punctuationStyle->setToolTip("");
    ui_->numberStyle->setToolTip("");
    ui_->commonSymbolStyle->setToolTip("");

    // Apply changes
    syncBasicToAdvanced();
    hideBasicModeWarning();
}

void MainWindow::syncBasicToAdvanced() {
    if (!currentProfile_) return;

    // Clear existing keymaps and tables
    clearKeymapsAndTables();

    // Apply settings based on Basic tab selections
    applyBasicPunctuationStyle();
    applyBasicNumberStyle();
    applyBasicSymbolStyle();
    applyBasicSpaceStyle();
    // Punctuation style must be set before base style becasue
    // Punctuation keymaps override Japanese Symbol map
    applyBasicInputStyle();

    // Update UI to reflect the changes
    if (currentProfile_) {
        // Temporarily set flag to prevent infinite loop
        isUpdatingFromAdvanced_ = true;
        ui_->submodeEntryPointChars->setText(QString::fromStdString(
            currentProfile_->submode_entry_point_chars()));
        isUpdatingFromAdvanced_ = false;
    }

    // Refresh the Advanced tab display
    loadInputTables();
    loadKeymaps();
}

void MainWindow::syncAdvancedToBasic() {
    if (!currentProfile_) return;

    isUpdatingFromAdvanced_ = true;

    if (isBasicModeCompatible()) {
        hideBasicModeWarning();
        setBasicTabEnabled(true);

        // Try to determine Basic settings from Advanced configuration
        // This is a simplified reverse mapping

        // Check Input Style based on submode entry and input tables
        QString submodeEntry = QString::fromStdString(
            currentProfile_->submode_entry_point_chars());
        bool hasRomajiTable = false;
        bool hasKanaTable = false;

        for (int i = 0; i < currentProfile_->enabled_tables_size(); ++i) {
            const auto& table = currentProfile_->enabled_tables(i);
            QString tableName = QString::fromStdString(table.name());
            if (tableName.contains("Romaji", Qt::CaseInsensitive)) {
                hasRomajiTable = true;
            }
            if (tableName.contains("Kana", Qt::CaseInsensitive)) {
                hasKanaTable = true;
            }
        }

        bool isKanaMode = false;
        // submodeEntry: already checked in inBasicModeCompatible()
        if (hasRomajiTable) {
            ui_->mainInputStyle->setCurrentIndex(0);  // Romaji
        } else if (hasKanaTable) {
            ui_->mainInputStyle->setCurrentIndex(1);  // JIS Kana
            isKanaMode = true;
        }

        // Enable/disable other options based on input style
        ui_->punctuationStyle->setEnabled(!isKanaMode);
        ui_->numberStyle->setEnabled(!isKanaMode);
        ui_->commonSymbolStyle->setEnabled(!isKanaMode);

        // Update tooltips
        if (isKanaMode) {
            ui_->punctuationStyle->setToolTip(tr("Disabled in Kana mode"));
            ui_->numberStyle->setToolTip(tr("Disabled in Kana mode"));
            ui_->commonSymbolStyle->setToolTip(tr("Disabled in Kana mode"));
        } else {
            ui_->punctuationStyle->setToolTip("");
            ui_->numberStyle->setToolTip("");
            ui_->commonSymbolStyle->setToolTip("");
        }

        // Check keymap settings for other styles
        QSet<QString> enabledKeymaps;
        for (int i = 0; i < currentProfile_->enabled_keymaps_size(); ++i) {
            const auto& keymap = currentProfile_->enabled_keymaps(i);
            enabledKeymaps.insert(QString::fromStdString(keymap.name()));
        }

        // Punctuation style
        if (enabledKeymaps.contains("Fullwidth Period") &&
            enabledKeymaps.contains("Fullwidth Comma")) {
            ui_->punctuationStyle->setCurrentIndex(1);  // Period+Comma
        } else if (enabledKeymaps.contains("Fullwidth Comma") &&
                   !enabledKeymaps.contains("Fullwidth Period")) {
            ui_->punctuationStyle->setCurrentIndex(2);  // Kuten+Comma
        } else if (enabledKeymaps.contains("Fullwidth Period") &&
                   !enabledKeymaps.contains("Fullwidth Comma")) {
            ui_->punctuationStyle->setCurrentIndex(3);  // Period+Toten
        } else {
            ui_->punctuationStyle->setCurrentIndex(0);  // Kuten+Toten
        }

        // Number style
        if (enabledKeymaps.contains("Fullwidth Number")) {
            ui_->numberStyle->setCurrentIndex(0);  // Fullwidth
        } else {
            ui_->numberStyle->setCurrentIndex(1);  // Halfwidth
        }

        // Symbol style
        if (enabledKeymaps.contains("Fullwidth Symbol")) {
            ui_->commonSymbolStyle->setCurrentIndex(0);  // Fullwidth
        } else {
            ui_->commonSymbolStyle->setCurrentIndex(1);  // Halfwidth
        }

        // Space style
        if (enabledKeymaps.contains("Fullwidth Space")) {
            ui_->spaceStyleLabel->setCurrentIndex(0);  // Fullwidth
        } else {
            ui_->spaceStyleLabel->setCurrentIndex(1);  // Halfwidth
        }

    } else {
        showBasicModeWarning();
        setBasicTabEnabled(false);
        // Automatically switch to Advanced tab when Basic mode is incompatible
        ui_->inputTableConfigModeTabWidget->setCurrentIndex(1);
    }

    isUpdatingFromAdvanced_ = false;
}

bool MainWindow::isBasicModeCompatible() {
    if (!currentProfile_) return false;

    // Get current settings
    QString submodeEntry =
        QString::fromStdString(currentProfile_->submode_entry_point_chars());

    // Collect enabled keymaps and tables
    QList<QString> enabledCustomKeymaps;
    QList<QString> enabledCustomTables;

    QList<QString> enabledBuiltinKeymaps;
    QList<QString> enabledBuiltinTables;

    for (int i = 0; i < currentProfile_->enabled_keymaps_size(); ++i) {
        const auto& keymap = currentProfile_->enabled_keymaps(i);
        QString name = QString::fromStdString(keymap.name());
        if (keymap.is_built_in()) {
            enabledBuiltinKeymaps.append(name);
        } else {
            enabledCustomKeymaps.append(name);
        }
    }

    for (int i = 0; i < currentProfile_->enabled_tables_size(); ++i) {
        const auto& table = currentProfile_->enabled_tables(i);
        QString name = QString::fromStdString(table.name());
        if (table.is_built_in()) {
            enabledBuiltinTables.append(name);
        } else {
            enabledCustomKeymaps.append(name);
        }
    }

    // Custom keymap check
    if (enabledCustomTables.size() != 0 || enabledCustomKeymaps.size() != 0) {
        return false;
    }

    // Check for valid input style configurations - must be builtin tables
    bool hasBuiltinRomajiTable = enabledBuiltinTables.contains("Romaji");
    bool hasBuiltinKanaTable = enabledBuiltinTables.contains("Kana");
    bool hasBuiltinKanaKeymap = enabledBuiltinKeymaps.contains("JIS Kana");
    bool isRomajiSubmode = (submodeEntry == "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    bool isKanaSubmode = submodeEntry.isEmpty();

    // Valid combinations:
    // 1. Romaji table only + Romaji submode
    // 2. Kana table only + Kana submode (empty)
    bool isValidRomajiInputStyle =
        (hasBuiltinRomajiTable && !hasBuiltinKanaTable && isRomajiSubmode &&
         !hasBuiltinKanaKeymap);
    bool isValidKanaInputStyle =
        (hasBuiltinKanaTable && !hasBuiltinRomajiTable && isKanaSubmode &&
         hasBuiltinKanaKeymap);

    // For Kana mode, only space-related and JIS Kana keymaps are allowed
    if (isValidKanaInputStyle) {
        QSet<QString> allowedKanaModeKeymaps = {"Fullwidth Space", "JIS Kana"};

        for (const QString& keymap : enabledBuiltinKeymaps) {
            if (!allowedKanaModeKeymaps.contains(keymap)) {
                return false;  // Invalid keymap for Kana mode
            }
        }
        return true;  // Valid Kana mode
    } else if (isValidRomajiInputStyle) {
        QSet<QString> validBasicKeymaps = {
            "Fullwidth Period", "Fullwidth Comma", "Fullwidth Number",
            "Fullwidth Symbol", "Fullwidth Space", "Japanese Symbol"};

        // Japanese Symbol map should be enabled and placed after punctuations.
        bool checkedJapaneSymbolMap = false;

        // Check if all enabled keymaps are valid for Basic mode
        for (const QString& keymap : enabledBuiltinKeymaps) {
            if (!validBasicKeymaps.contains(keymap)) {
                return false;
            }
            if (checkedJapaneSymbolMap &&
                (keymap == "Fullwidth Period" || keymap == "Fullwidth Comma")) {
                return false;  // Bad punctuation & Japanese Symbol map order
            }
            if (keymap == "Japanese Symbol") {
                checkedJapaneSymbolMap = true;
            }
        }
        if (!checkedJapaneSymbolMap) {
            return false;  // Missing required map
        }
        return true;  // Valid Romaji mode
    } else {
        return false;
    }
}

void MainWindow::showBasicModeWarning() {
    // First, hide any existing warning to prevent duplicates
    hideBasicModeWarning();

    QVBoxLayout* basicTabLayout = qobject_cast<QVBoxLayout*>(
        ui_->inputStyleSimpleModeScrollAreaContents->layout());

    if (basicTabLayout) {
        QWidget* warningWidget = new QWidget();
        warningWidget->setStyleSheet("background-color: yellow; padding: 5px;");
        QHBoxLayout* warningLayout = new QHBoxLayout(warningWidget);

        QLabel* warningLabel = new QLabel(tr(
            "<b>Warning:</b> Current settings can only be edited in Advanced "
            "mode."));
        warningLabel->setWordWrap(true);
        warningLayout->addWidget(warningLabel);

        QPushButton* resetButton = new QPushButton(tr("Reset Input Style"));
        connect(resetButton, &QPushButton::clicked, this,
                &MainWindow::resetInputStyleToDefault);
        warningLayout->addWidget(resetButton);

        basicTabLayout->insertWidget(0, warningWidget);

        // Disable all Basic tab elements when warning is shown
        setBasicTabEnabled(false);
    }
}

void MainWindow::hideBasicModeWarning() {
    QVBoxLayout* basicTabLayout = qobject_cast<QVBoxLayout*>(
        ui_->inputStyleSimpleModeScrollAreaContents->layout());
    if (basicTabLayout) {
        for (int i = basicTabLayout->count() - 1; i >= 0; --i) {
            QLayoutItem* item = basicTabLayout->itemAt(i);
            if (item && item->widget()) {
                QWidget* widget = item->widget();
                // Check if this is a warning widget (has yellow background)
                if (widget->styleSheet().contains("background-color: yellow")) {
                    basicTabLayout->removeWidget(widget);
                    widget->deleteLater();
                    // Re-enable Basic tab elements when warning is hidden
                    setBasicTabEnabled(true);

                    // If in Kana mode, re-apply the restrictions
                    if (ui_->mainInputStyle->currentIndex() == 1) {
                        ui_->punctuationStyle->setEnabled(false);
                        ui_->numberStyle->setEnabled(false);
                        ui_->commonSymbolStyle->setEnabled(false);
                        ui_->punctuationStyle->setToolTip(
                            "Disabled in Kana mode");
                        ui_->numberStyle->setToolTip("Disabled in Kana mode");
                        ui_->commonSymbolStyle->setToolTip(
                            "Disabled in Kana mode");
                    }
                    return;
                }
            }
        }
    }
}

void MainWindow::setBasicTabEnabled(bool enabled) {
    ui_->inputStylesGrid->setEnabled(enabled);
    ui_->mainInputStyle->setEnabled(enabled);
    ui_->punctuationStyle->setEnabled(enabled);
    ui_->numberStyle->setEnabled(enabled);
    ui_->commonSymbolStyle->setEnabled(enabled);
    ui_->spaceStyleLabel->setEnabled(enabled);
}

void MainWindow::applyBasicInputStyle() {
    int inputStyleIndex = ui_->mainInputStyle->currentIndex();

    if (inputStyleIndex == 0) {  // Romaji
        currentProfile_->set_submode_entry_point_chars(
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        addInputTableIfAvailable("Romaji", true);
        addKeymapIfAvailable("Japanese Symbol", true);
    } else if (inputStyleIndex == 1) {  // JIS Kana
        currentProfile_->set_submode_entry_point_chars("");
        addInputTableIfAvailable("Kana", true);
        addKeymapIfAvailable("JIS Kana", true);
    }
}

void MainWindow::applyBasicPunctuationStyle() {
    // Skip if punctuation style is disabled (Kana mode)
    if (!ui_->punctuationStyle->isEnabled()) {
        return;
    }

    int punctuationIndex = ui_->punctuationStyle->currentIndex();

    switch (punctuationIndex) {
        case 0:  // Kuten+Toten: 。、
            // No additional keymaps needed
            break;
        case 1:  // Period+Comma: ．，
            addKeymapIfAvailable("Fullwidth Period", true);
            addKeymapIfAvailable("Fullwidth Comma", true);
            break;
        case 2:  // Kuten+Comma: 。，
            addKeymapIfAvailable("Fullwidth Comma", true);
            break;
        case 3:  // Period+Toten: ．、
            addKeymapIfAvailable("Fullwidth Period", true);
            break;
    }
}

void MainWindow::applyBasicNumberStyle() {
    // Skip if number style is disabled (Kana mode)
    if (!ui_->numberStyle->isEnabled()) {
        return;
    }

    int numberIndex = ui_->numberStyle->currentIndex();

    if (numberIndex == 0) {  // Fullwidth: １２３４５
        addKeymapIfAvailable("Fullwidth Number", true);
    }
    // Halfwidth is default, no keymap needed
}

void MainWindow::applyBasicSymbolStyle() {
    // Skip if symbol style is disabled (Kana mode)
    if (!ui_->commonSymbolStyle->isEnabled()) {
        return;
    }

    int symbolIndex = ui_->commonSymbolStyle->currentIndex();

    switch (symbolIndex) {
        case 0:  // Fullwidth: ！＃＠（
            addKeymapIfAvailable("Fullwidth Symbol", true);
            break;
        case 1:  // Halfwidth: !#@(
            // No additional keymaps needed
            break;
    }
}

void MainWindow::applyBasicSpaceStyle() {
    int spaceIndex = ui_->spaceStyleLabel->currentIndex();

    if (spaceIndex == 0) {  // Fullwidth: "　"
        addKeymapIfAvailable("Fullwidth Space", true);
    }
    // Halfwidth is default, no keymap needed
}

void MainWindow::addKeymapIfAvailable(const QString& keymapName,
                                      bool isBuiltIn) {
    // Check if keymap is available with exact match on name and built-in status
    for (int i = 0; i < currentConfig_.available_keymaps_size(); ++i) {
        const auto& availableKeymap = currentConfig_.available_keymaps(i);
        if (QString::fromStdString(availableKeymap.name()) == keymapName &&
            availableKeymap.is_built_in() == isBuiltIn) {
            // Add to enabled keymaps
            auto* enabledKeymap = currentProfile_->add_enabled_keymaps();
            enabledKeymap->set_name(availableKeymap.name());
            enabledKeymap->set_is_built_in(availableKeymap.is_built_in());
            enabledKeymap->set_filename(availableKeymap.filename());
            break;
        }
    }
}

void MainWindow::addInputTableIfAvailable(const QString& tableName,
                                          bool isBuiltIn) {
    // Check if input table is available with exact match on name and built-in
    // status
    for (int i = 0; i < currentConfig_.available_tables_size(); ++i) {
        const auto& availableTable = currentConfig_.available_tables(i);
        if (QString::fromStdString(availableTable.name()) == tableName &&
            availableTable.is_built_in() == isBuiltIn) {
            // Add to enabled tables
            auto* enabledTable = currentProfile_->add_enabled_tables();
            enabledTable->set_name(availableTable.name());
            enabledTable->set_is_built_in(availableTable.is_built_in());
            enabledTable->set_filename(availableTable.filename());
            break;
        }
    }
}

void MainWindow::clearKeymapsAndTables() {
    if (currentProfile_) {
        currentProfile_->clear_enabled_keymaps();
        currentProfile_->clear_enabled_tables();
    }
}

void MainWindow::onCheckAllConversion() {
    ui_->halfwidthKatakanaConversion->setChecked(true);
    ui_->extendedEmojiConversion->setChecked(true);
    ui_->commaSeparatedNumCoversion->setChecked(true);
    ui_->calendarConversion->setChecked(true);
    ui_->timeConversion->setChecked(true);
    ui_->mailDomainConversion->setChecked(true);
    ui_->unicodeCodePointConversion->setChecked(true);
    ui_->romanTypographyConversion->setChecked(true);
    ui_->hazkeyVersionConversion->setChecked(true);
}

void MainWindow::onUncheckAllConversion() {
    ui_->halfwidthKatakanaConversion->setChecked(false);
    ui_->extendedEmojiConversion->setChecked(false);
    ui_->commaSeparatedNumCoversion->setChecked(false);
    ui_->calendarConversion->setChecked(false);
    ui_->timeConversion->setChecked(false);
    ui_->mailDomainConversion->setChecked(false);
    ui_->unicodeCodePointConversion->setChecked(false);
    ui_->romanTypographyConversion->setChecked(false);
    ui_->hazkeyVersionConversion->setChecked(false);
}

void MainWindow::onClearLearningData() {
    if (!currentProfile_) {
        QMessageBox::warning(this, tr("Error"),
                             tr("No configuration profile loaded."));
        return;
    }

    // Show confirmation dialog
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, tr("Clear Input History"),
        tr("Are you sure you want to clear all input history data? This action "
           "cannot be undone."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        // Clear the history using the server connector
        bool success = server_.clearAllHistory(currentProfile_->profile_id());

        if (success) {
            QMessageBox::information(
                this, tr("Success"),
                tr("Input history has been cleared successfully."));
        } else {
            QMessageBox::critical(
                this, tr("Error"),
                tr("Failed to clear input history. Please check your "
                   "connection to the hazkey server."));
        }
    }
}

QString MainWindow::translateKeymapName(const QString& keymapName,
                                        bool isBuiltin) {
    if (!isBuiltin) {
        return keymapName;
    }
    if (keymapName == "JIS Kana") {
        return tr("JIS Kana");
    } else if (keymapName == "Japanese Symbol") {
        return tr("Japanese Symbol");
    } else if (keymapName == "Fullwidth Period") {
        return tr("Fullwidth Period");
    } else if (keymapName == "Fullwidth Comma") {
        return tr("Fullwidth Comma");
    } else if (keymapName == "Fullwidth Number") {
        return tr("Fullwidth Number");
    } else if (keymapName == "Fullwidth Symbol") {
        return tr("Fullwidth Symbol");
    } else if (keymapName == "Fullwidth Space") {
        return tr("Fullwidth Space");
    }
    return keymapName;
}

QString MainWindow::translateTableName(const QString& tableName,
                                       bool isBuiltin) {
    if (!isBuiltin) {
        return tableName;
    }
    if (tableName == "Romaji") {
        return tr("Romaji");
    } else if (tableName == "Kana") {
        return tr("Kana");
    }
    return tableName;
}

MainWindow::~MainWindow() {
    if (currentDownload_) {
        currentDownload_->abort();
        currentDownload_->deleteLater();
    }
    if (downloadProgressDialog_) {
        delete downloadProgressDialog_;
    }
    delete ui_;
}

void MainWindow::onDownloadZenzaiModel() {
    // Determine the download path
    QString dataHome = qEnvironmentVariable("XDG_DATA_HOME");
    if (dataHome.isEmpty()) {
        dataHome = QDir::homePath() + "/.local/share";
    }

    QString zenzaiDir = dataHome + "/hazkey/zenzai";
    zenzaiModelPath_ = zenzaiDir + "/zenzai.gguf";

    QDir dir;
    if (!dir.mkpath(zenzaiDir)) {
        QMessageBox::critical(
            this, tr("Download Error"),
            tr("Failed to create directory: %1").arg(zenzaiDir));
        return;
    }

    if (QFile::exists(zenzaiModelPath_)) {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, tr("File Exists"), tr("Overwrite the existing Zenzai model?"),
            QMessageBox::Yes | QMessageBox::No);

        if (reply == QMessageBox::No) {
            return;
        }
    }

    // Progress dialog
    downloadProgressDialog_ = new QProgressDialog(
        tr("Downloading Zenzai model..."), tr("Cancel"), 0, 100, this);
    downloadProgressDialog_->setWindowModality(Qt::WindowModal);
    downloadProgressDialog_->setMinimumDuration(0);
    downloadProgressDialog_->setValue(0);

    connect(downloadProgressDialog_, &QProgressDialog::canceled, this,
            [this]() {
                if (currentDownload_) {
                    currentDownload_->abort();
                }
            });

    // Start download
    QUrl url(
        "https://huggingface.co/Miwa-Keita/zenz-v3.1-small-gguf/resolve/main/"
        "ggml-model-Q5_K_M.gguf");
    QNetworkRequest request(url);

    currentDownload_ = networkManager_->get(request);

    connect(currentDownload_, &QNetworkReply::downloadProgress, this,
            &MainWindow::onDownloadProgress);
    connect(currentDownload_, &QNetworkReply::finished, this,
            &MainWindow::onDownloadFinished);
    connect(currentDownload_,
            QOverload<QNetworkReply::NetworkError>::of(
                &QNetworkReply::errorOccurred),
            this, &MainWindow::onDownloadError);
}

void MainWindow::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    if (downloadProgressDialog_ && bytesTotal > 0) {
        int progress = static_cast<int>((bytesReceived * 100) / bytesTotal);
        downloadProgressDialog_->setValue(progress);

        // Show download size in MB
        double receivedMB = bytesReceived / 1024.0 / 1024.0;
        double totalMB = bytesTotal / 1024.0 / 1024.0;
        downloadProgressDialog_->setLabelText(
            tr("Downloading Zenzai model... %1 MB / %2 MB")
                .arg(receivedMB, 0, 'f', 2)
                .arg(totalMB, 0, 'f', 2));
    }
}

void MainWindow::onDownloadFinished() {
    if (!currentDownload_) {
        return;
    }

    // Close progress dialog
    if (downloadProgressDialog_) {
        downloadProgressDialog_->deleteLater();
        downloadProgressDialog_ = nullptr;
    }

    // Check for errors
    if (currentDownload_->error() != QNetworkReply::NoError) {
        currentDownload_->deleteLater();
        currentDownload_ = nullptr;
        return;  // Error already handled by onDownloadError
    }

    // Read downloaded data
    QByteArray downloadedData = currentDownload_->readAll();
    currentDownload_->deleteLater();
    currentDownload_ = nullptr;

    // Verify SHA256
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(downloadedData);
    QByteArray calculatedHash = hash.result();
    QString calculatedHashHex = calculatedHash.toHex();

    QString expectedHash =
        "4de930c06bef8c263aa1aa40684af206db4ce1b96375b3b8ed0ea508e0b14f6c";

    if (calculatedHashHex != expectedHash) {
        QMessageBox::critical(
            this, tr("Download Error"),
            tr("Downloaded file verification failed. Checksum mismatch.\n"
               "Expected: %1\n"
               "Got: %2")
                .arg(expectedHash)
                .arg(calculatedHashHex));
        return;
    }

    // Save to temporary file first
    QString tempPath = zenzaiModelPath_ + ".tmp";
    QFile tempFile(tempPath);
    if (!tempFile.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(
            this, tr("Download Error"),
            tr("Failed to save model file: %1").arg(tempFile.errorString()));
        return;
    }

    if (tempFile.write(downloadedData) == -1) {
        QMessageBox::critical(
            this, tr("Download Error"),
            tr("Failed to write model file: %1").arg(tempFile.errorString()));
        tempFile.close();
        QFile::remove(tempPath);
        return;
    }

    tempFile.close();

    // Remove old file if it exists and rename temp file
    if (QFile::exists(zenzaiModelPath_)) {
        if (!QFile::remove(zenzaiModelPath_)) {
            QMessageBox::critical(this, tr("Download Error"),
                                  tr("Failed to remove old model file."));
            QFile::remove(tempPath);
            return;
        }
    }

    if (!QFile::rename(tempPath, zenzaiModelPath_)) {
        QMessageBox::critical(this, tr("Download Error"),
                              tr("Failed to rename model file."));
        QFile::remove(tempPath);
        return;
    }

    // Reload Zenzai model in server
    server_.reloadZenzaiModel();

    QMessageBox::information(
        this, tr("Download Complete"),
        tr("Zenzai model has been downloaded successfully.\n"
           "Please push 'Reset' to refresh the UI."));
}

void MainWindow::onDownloadError(QNetworkReply::NetworkError error) {
    // Close progress dialog first
    if (downloadProgressDialog_) {
        downloadProgressDialog_->deleteLater();
        downloadProgressDialog_ = nullptr;
    }

    if (!currentDownload_) {
        return;
    }

    QString errorString = currentDownload_->errorString();
    currentDownload_->deleteLater();
    currentDownload_ = nullptr;

    // Don't show error if user cancelled
    if (error != QNetworkReply::OperationCanceledError) {
        QMessageBox::critical(
            this, tr("Download Error"),
            tr("Failed to download Zenzai model: %1").arg(errorString));
    }
}

void MainWindow::onResetConfiguration() {
    QMessageBox::StandardButton reply = QMessageBox::question(
        this, tr("Reset Configuration"),
        tr("Resetting will discard any unsaved changes. Continue?"),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::No) {
        return;
    }

    server_.reloadZenzaiModel();

    if (!loadCurrentConfig()) {
        QMessageBox::critical(this, tr("Configuration Error"),
                              tr("Failed to load configuration from server."));
        return;
    }

    QTimer::singleShot(0, this, [this]() {
        QMessageBox::information(
            this, tr("Reset Complete"),
            tr("Configuration has been reset successfully."));
    });
}

QWidget* MainWindow::createWarningWidget(const QString& message,
                                         const QString& backgroundColor,
                                         const QString& buttonText,
                                         std::function<void()> buttonCallback) {
    QWidget* warningWidget = new QWidget();
    warningWidget->setStyleSheet(
        QString("background-color: %1; padding: 5px;").arg(backgroundColor));
    QHBoxLayout* warningLayout = new QHBoxLayout(warningWidget);

    QLabel* warningLabel = new QLabel(message);
    warningLabel->setWordWrap(true);
    warningLayout->addWidget(warningLabel);

    if (!buttonText.isEmpty() && buttonCallback) {
        QPushButton* button = new QPushButton(buttonText);
        connect(button, &QPushButton::clicked, this, buttonCallback);
        warningLayout->addWidget(button);
    }

    return warningWidget;
}

QString MainWindow::calculateFileSHA256(const QString& filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return QString();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) {
        file.close();
        return QString();
    }

    file.close();
    return QString(hash.result().toHex());
}
