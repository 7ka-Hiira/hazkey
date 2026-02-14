#include <QApplication>
#include <QNetworkAccessManager>
#include <QtTest/QtTest>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

#include "config.pb.h"
#include "controllers/ai_tab_controller.h"
#include "controllers/conversion_tab_controller.h"
#include "controllers/input_style_tab_controller.h"
#include "controllers/tab_context.h"
#include "ui_mainwindow.h"

namespace {

hazkey::config::CurrentConfig buildBaseConfig() {
    hazkey::config::CurrentConfig config;
    config.set_xdg_config_home_path("/test/home/.config");

    auto* romajiTable = config.add_available_tables();
    romajiTable->set_name("Romaji");
    romajiTable->set_is_built_in(true);
    romajiTable->set_filename("romaji.json");

    auto* kanaTable = config.add_available_tables();
    kanaTable->set_name("Kana");
    kanaTable->set_is_built_in(true);
    kanaTable->set_filename("kana.json");

    const std::vector<std::string> keymaps = {
        "Japanese Symbol", "Fullwidth Period", "Fullwidth Comma",
        "Fullwidth Number",  "Fullwidth Symbol", "Fullwidth Space",
        "JIS Kana"};

    for (const auto& name : keymaps) {
        auto* keymap = config.add_available_keymaps();
        keymap->set_name(name);
        keymap->set_is_built_in(true);
        keymap->set_filename(name + std::string(".json"));
    }

    auto* cpuDevice = config.add_available_zenzai_backend_devices();
    cpuDevice->set_name("CPU");
    cpuDevice->set_desc("TestCPU");

    auto* gpuDevice = config.add_available_zenzai_backend_devices();
    gpuDevice->set_name("Vulkan0");
    gpuDevice->set_desc("TestVulkanGPU");

    config.set_zenzai_model_available(true);
    config.set_zenzai_model_path("/tmp/zenzai.gguf");

    auto* profile = config.add_profiles();
    profile->set_profile_id("test-profile");

    return config;
}

void setRomajiProfile(hazkey::config::Profile* profile) {
    profile->clear_enabled_tables();
    auto* table = profile->add_enabled_tables();
    table->set_name("Romaji");
    table->set_is_built_in(true);
    table->set_filename("romaji.json");

    profile->clear_enabled_keymaps();
    const std::vector<std::string> keymaps = {
        "Japanese Symbol", "Fullwidth Period", "Fullwidth Comma",
        "Fullwidth Number", "Fullwidth Symbol", "Fullwidth Space"};
    for (const auto& name : keymaps) {
        auto* keymap = profile->add_enabled_keymaps();
        keymap->set_name(name);
        keymap->set_is_built_in(true);
        keymap->set_filename(name + std::string(".json"));
    }

    profile->set_submode_entry_point_chars("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
}

std::unordered_set<std::string> keymapNames(
    const hazkey::config::Profile& profile) {
    std::unordered_set<std::string> names;
    for (int i = 0; i < profile.enabled_keymaps_size(); ++i) {
        names.insert(profile.enabled_keymaps(i).name());
    }
    return names;
}

std::unordered_set<std::string> tableNames(
    const hazkey::config::Profile& profile) {
    std::unordered_set<std::string> names;
    for (int i = 0; i < profile.enabled_tables_size(); ++i) {
        names.insert(profile.enabled_tables(i).name());
    }
    return names;
}

struct UiFixture {
    UiFixture() { ui.setupUi(&window); }
    QWidget window;
    Ui::MainWindow ui;
};

}  // namespace

class InputStyleTabControllerTest : public QObject {
    Q_OBJECT

   private slots:
    void init() {
        config_ = buildBaseConfig();
        profile_ = config_.mutable_profiles(0);
        setRomajiProfile(profile_);

        fixture_ = std::make_unique<UiFixture>();
        controller_ = std::make_unique<hazkey::settings::InputStyleTabController>(
            &fixture_->ui, &fixture_->window);

        controller_->connectSignals();
    }

    void cleanup() {
        controller_.reset();
        fixture_.reset();
        profile_ = nullptr;
    }

    void loadFromConfig_setsBasicStateFromRomajiProfile() {
        hazkey::settings::TabContext ctx{&config_, profile_, nullptr};
        controller_->setContext(ctx);

        controller_->loadFromConfig();

        QCOMPARE(fixture_->ui.mainInputStyle->currentIndex(), 0);
        // Current compatibility rules disable punctuation presets when both
        // Japanese Symbol and fullwidth punctuation maps coexist.
        QCOMPARE(fixture_->ui.punctuationStyle->currentIndex(), 0);
        QCOMPARE(fixture_->ui.numberStyle->currentIndex(), 0);
        QCOMPARE(fixture_->ui.commonSymbolStyle->currentIndex(), 0);
        QCOMPARE(fixture_->ui.spaceStyleLabel->currentIndex(), 0);

        QCOMPARE(fixture_->ui.enabledTableList->count(), 1);
        QCOMPARE(fixture_->ui.availableTableList->count(), 1);
        QCOMPARE(fixture_->ui.enabledKeymapList->count(), 6);

        auto tables = tableNames(*profile_);
        QVERIFY(tables.count("Romaji") == 1);
    }

    void onBasicInputStyleChanged_updatesProfileForKanaMode() {
        hazkey::settings::TabContext ctx{&config_, profile_, nullptr};
        controller_->setContext(ctx);
        controller_->loadFromConfig();

        fixture_->ui.mainInputStyle->setCurrentIndex(1);
        QMetaObject::invokeMethod(controller_.get(), "onBasicInputStyleChanged");

        auto tables = tableNames(*profile_);
        QVERIFY(tables.count("Kana") == 1);

        auto keymaps = keymapNames(*profile_);
        QVERIFY(keymaps.count("JIS Kana") == 1);
        QVERIFY(keymaps.count("Fullwidth Space") == 1);

        QCOMPARE(profile_->submode_entry_point_chars(), std::string(""));
        QVERIFY(!fixture_->ui.punctuationStyle->isEnabled());
        QVERIFY(!fixture_->ui.numberStyle->isEnabled());
        QVERIFY(!fixture_->ui.commonSymbolStyle->isEnabled());
    }

   private:
    hazkey::config::CurrentConfig config_;
    hazkey::config::Profile* profile_{nullptr};
    std::unique_ptr<UiFixture> fixture_;
    std::unique_ptr<hazkey::settings::InputStyleTabController> controller_;
};

class ConversionTabControllerTest : public QObject {
    Q_OBJECT

   private slots:
    void init() {
        config_ = buildBaseConfig();
        profile_ = config_.mutable_profiles(0);

        auto* special = profile_->mutable_special_conversion_mode();
        special->set_halfwidth_katakana(true);
        special->set_extended_emoji(false);
        special->set_comma_separated_number(true);
        special->set_calendar(false);
        special->set_time(true);
        special->set_mail_domain(false);
        special->set_unicode_codepoint(true);
        special->set_roman_typography(false);
        special->set_hazkey_version(true);

        profile_->set_use_input_history(true);
        profile_->set_stop_store_new_history(false);

        fixture_ = std::make_unique<UiFixture>();
        controller_ = std::make_unique<hazkey::settings::ConversionTabController>(
            &fixture_->ui, &fixture_->window, nullptr, &fixture_->window);

        controller_->connectSignals();
    }

    void cleanup() {
        controller_.reset();
        fixture_.reset();
        profile_ = nullptr;
    }

    void loadAndSave_roundTripsConversionSettings() {
        hazkey::settings::TabContext ctx{&config_, profile_, nullptr};
        controller_->setContext(ctx);

        controller_->loadFromConfig();

        QVERIFY(fixture_->ui.useHistory->isChecked());
        QVERIFY(fixture_->ui.stopStoreNewHistory->isEnabled());
        QCOMPARE(fixture_->ui.halfwidthKatakanaConversion->isChecked(), true);
        QCOMPARE(fixture_->ui.extendedEmojiConversion->isChecked(), false);
        QCOMPARE(fixture_->ui.calendarConversion->isChecked(), false);
        QCOMPARE(fixture_->ui.hazkeyVersionConversion->isChecked(), true);

        fixture_->ui.useHistory->setChecked(false);
        fixture_->ui.extendedEmojiConversion->setChecked(true);
        fixture_->ui.calendarConversion->setChecked(true);
        fixture_->ui.timeConversion->setChecked(false);

        controller_->saveToConfig();

        QCOMPARE(profile_->use_input_history(), false);
        QCOMPARE(profile_->stop_store_new_history(), false);

        const auto& special = profile_->special_conversion_mode();
        QCOMPARE(special.extended_emoji(), true);
        QCOMPARE(special.calendar(), true);
        QCOMPARE(special.time(), false);
    }

   private:
    hazkey::config::CurrentConfig config_;
    hazkey::config::Profile* profile_{nullptr};
    std::unique_ptr<UiFixture> fixture_;
    std::unique_ptr<hazkey::settings::ConversionTabController> controller_;
};

class AiTabControllerTest : public QObject {
    Q_OBJECT

   private slots:
    void init() {
        config_ = buildBaseConfig();
        profile_ = config_.mutable_profiles(0);

        profile_->set_zenzai_infer_limit(7);
        profile_->set_zenzai_enable(true);
        profile_->set_zenzai_contextual_mode(true);
        profile_->set_zenzai_profile("default");
        profile_->set_zenzai_backend_device_name("CPU");

        fixture_ = std::make_unique<UiFixture>();
        networkManager_ = std::make_unique<QNetworkAccessManager>(&fixture_->window);
        controller_ = std::make_unique<hazkey::settings::AiTabController>(
            &fixture_->ui, &fixture_->window, networkManager_.get(),
            &fixture_->window);

        controller_->connectSignals();
    }

    void cleanup() {
        controller_.reset();
        networkManager_.reset();
        fixture_.reset();
        profile_ = nullptr;
    }

    void loadAndSave_updatesProfileAndUi() {
        hazkey::settings::TabContext ctx{&config_, profile_, nullptr};
        controller_->setContext(ctx);

        controller_->loadFromConfig();

        QCOMPARE(fixture_->ui.zenzaiInferenceLimit->value(), 7);
        QCOMPARE(fixture_->ui.enableZenzai->isChecked(), true);
        QCOMPARE(fixture_->ui.zenzaiContextualConversion->isChecked(), true);
        QCOMPARE(fixture_->ui.zenzaiBackendDevice->currentData().toString(),
             QString("CPU"));
        QVERIFY(fixture_->ui.enableZenzai->isEnabled());

        fixture_->ui.zenzaiInferenceLimit->setValue(13);
        fixture_->ui.enableZenzai->setChecked(false);
        fixture_->ui.zenzaiContextualConversion->setChecked(false);
        fixture_->ui.zenzaiUserPlofile->setText("new-user");
        fixture_->ui.zenzaiBackendDevice->setCurrentIndex(1);

        controller_->saveToConfig();

        QCOMPARE(profile_->zenzai_infer_limit(), 13);
        QCOMPARE(profile_->zenzai_enable(), false);
        QCOMPARE(profile_->zenzai_contextual_mode(), false);
        QCOMPARE(profile_->zenzai_profile(), std::string("new-user"));
        QCOMPARE(profile_->zenzai_backend_device_name(), std::string("Vulkan0"));
    }

   private:
    hazkey::config::CurrentConfig config_;
    hazkey::config::Profile* profile_{nullptr};
    std::unique_ptr<UiFixture> fixture_;
    std::unique_ptr<QNetworkAccessManager> networkManager_;
    std::unique_ptr<hazkey::settings::AiTabController> controller_;
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);

    int status = 0;
    {
        InputStyleTabControllerTest test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        ConversionTabControllerTest test;
        status |= QTest::qExec(&test, argc, argv);
    }
    {
        AiTabControllerTest test;
        status |= QTest::qExec(&test, argc, argv);
    }

    return status;
}

#include "settings_tests.moc"
