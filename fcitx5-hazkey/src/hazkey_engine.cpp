#include "hazkey_engine.h"

#include <fcitx-utils/macros.h>

#include "hazkey_server_connector.h"
#include "hazkey_state.h"
#include "hazkey_constants.h"

namespace fcitx {

HazkeyEngine::HazkeyEngine(Instance *instance)
    : instance_(instance), factory_([this](InputContext &ic) {
          return new HazkeyState(this, &ic);
      }) {
    server_ = HazkeyServerConnector();

    instance->inputContextManager().registerProperty("hazkeyState", &factory_);
    reloadConfig();
}

void HazkeyEngine::keyEvent([[maybe_unused]] const InputMethodEntry &entry,
                            KeyEvent &keyEvent) {
    FCITX_DEBUG() << "keyEvent: " << keyEvent.key().toString();

    auto inputContext = keyEvent.inputContext();
    inputContext->propertyFor(&factory_)->keyEvent(keyEvent);
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void HazkeyEngine::activate([[maybe_unused]] const InputMethodEntry &entry,
                            InputContextEvent &event) {
    FCITX_DEBUG() << &entry;
    FCITX_DEBUG() << "HazkeyEngine activate";
    auto inputContext = event.inputContext();
    auto state = inputContext->propertyFor(&factory_);
    state->reset();
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void HazkeyEngine::deactivate([[maybe_unused]] const InputMethodEntry &entry,
                              InputContextEvent &event) {
    FCITX_DEBUG() << "HazkeyEngine deactivate";
    auto inputContext = event.inputContext();
    auto state = inputContext->propertyFor(&factory_);
    state->commitPreedit();
    state->reset();
    inputContext->updatePreedit();
    inputContext->updateUserInterface(UserInterfaceComponent::InputPanel);
}

void HazkeyEngine::setConfig(const RawConfig &config) {
    config_.load(config, true);
    safeSaveAsIni(config_, "conf/hazkey.conf");
    reloadConfig();
}

void HazkeyEngine::reloadConfig() {
    readAsIni(config_, "conf/hazkey.conf");

    std::string lastVersion = config_.lastVersion.value();

    if (lastVersion != HAZKEY_VERSION) {
        FCITX_DEBUG() << "Update detected. restarting server..";
        // server automatically restarts if current server is newer than running server.
        server_.start_hazkey_server();

        config_.lastVersion.setValue(HAZKEY_VERSION);
        safeSaveAsIni(config_, "conf/hazkey.conf");
    }
}

void HazkeyEngine::save() {
    server_.saveLearningData();
}

FCITX_ADDON_FACTORY(HazkeyEngineFactory);

}  // namespace fcitx
