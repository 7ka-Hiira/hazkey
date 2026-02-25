#include "hazkey_state.h"

#include <fcitx-utils/key.h>
#include <fcitx-utils/log.h>
#include <fcitx-utils/textformatflags.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/candidatelist.h>
#include <fcitx/text.h>
#include <google/protobuf/repeated_field.h>

#include <memory>
#include <optional>
#include <string>

#include "commands.pb.h"
#include "hazkey_candidate.h"
#include "hazkey_engine.h"
#include "hazkey_server_connector.h"

namespace fcitx {
namespace {

uint32_t utf8ToCodepoint(const std::string& text) {
    auto utf32 = fcitx::utf8::MakeUTF8CharRange(text);
    if (utf32.begin() != utf32.end()) {
        return static_cast<uint32_t>(*(utf32.begin()));
    }
    return 0;
}

std::optional<hazkey::commands::SpecialKey> mapSpecialKey(KeySym sym) {
    switch (sym) {
        case FcitxKey_Return:
            return hazkey::commands::SPECIAL_KEY_ENTER;
        case FcitxKey_Escape:
            return hazkey::commands::SPECIAL_KEY_ESCAPE;
        case FcitxKey_BackSpace:
            return hazkey::commands::SPECIAL_KEY_BACKSPACE;
        case FcitxKey_Tab:
            return hazkey::commands::SPECIAL_KEY_TAB;
        case FcitxKey_Home:
            return hazkey::commands::SPECIAL_KEY_HOME;
        case FcitxKey_Page_Up:
            return hazkey::commands::SPECIAL_KEY_PAGE_UP;
        case FcitxKey_Delete:
            return hazkey::commands::SPECIAL_KEY_DELETE;
        case FcitxKey_End:
            return hazkey::commands::SPECIAL_KEY_END;
        case FcitxKey_Page_Down:
            return hazkey::commands::SPECIAL_KEY_PAGE_DOWN;
        case FcitxKey_Right:
            return hazkey::commands::SPECIAL_KEY_ARROW_RIGHT;
        case FcitxKey_Left:
            return hazkey::commands::SPECIAL_KEY_ARROW_LEFT;
        case FcitxKey_Down:
            return hazkey::commands::SPECIAL_KEY_ARROW_DOWN;
        case FcitxKey_Up:
            return hazkey::commands::SPECIAL_KEY_ARROW_UP;
        case FcitxKey_Num_Lock:
            return hazkey::commands::SPECIAL_KEY_NUM_LOCK;
        case FcitxKey_F1:
            return hazkey::commands::SPECIAL_KEY_F1;
        case FcitxKey_F2:
            return hazkey::commands::SPECIAL_KEY_F2;
        case FcitxKey_F3:
            return hazkey::commands::SPECIAL_KEY_F3;
        case FcitxKey_F4:
            return hazkey::commands::SPECIAL_KEY_F4;
        case FcitxKey_F5:
            return hazkey::commands::SPECIAL_KEY_F5;
        case FcitxKey_F6:
            return hazkey::commands::SPECIAL_KEY_F6;
        case FcitxKey_F7:
            return hazkey::commands::SPECIAL_KEY_F7;
        case FcitxKey_F8:
            return hazkey::commands::SPECIAL_KEY_F8;
        case FcitxKey_F9:
            return hazkey::commands::SPECIAL_KEY_F9;
        case FcitxKey_F10:
            return hazkey::commands::SPECIAL_KEY_F10;
        case FcitxKey_F11:
            return hazkey::commands::SPECIAL_KEY_F11;
        case FcitxKey_F12:
            return hazkey::commands::SPECIAL_KEY_F12;
        default:
            return std::nullopt;
    }
}

}  // namespace

HazkeyState::HazkeyState(HazkeyEngine* engine, InputContext* ic)
    : engine_(engine), ic_(ic), preedit_(HazkeyPreedit(ic)) {}

void HazkeyState::commitPreedit() { preedit_.commitPreedit(); }

void HazkeyState::resetClient() {
    ic_->inputPanel().reset();
    preedit_.setPreedit(Text());
}

void HazkeyState::setAuxDownText(std::optional<std::string> optText) {
    auto aux = Text();
    // if (engine_->server().currentInputModeIsDirect()) {
    //     // appending fcitx::Text is supported only >= 5.1.9
    //     aux.append(std::string(_("[Direct Input]")));
    // } else if (optText != std::nullopt) {
    aux.append(optText.value());
    // }
    ic_->inputPanel().setAuxDown(aux);
}

hazkey::commands::KeyEvent HazkeyState::buildKeyEvent(KeyEvent& keyEvent) {
    hazkey::commands::KeyEvent request;

    if (keyEvent.isRelease()) {
        request.set_event_type(
            hazkey::commands::KeyEvent_EventType_EVENT_TYPE_RELEASE);
    } else if (keyEvent.key().states().test(KeyState::Repeat)) {
        request.set_event_type(
            hazkey::commands::KeyEvent_EventType_EVENT_TYPE_PRESS);
    } else {
        request.set_event_type(
            hazkey::commands::KeyEvent_EventType_EVENT_TYPE_DOWN);
    }

    auto keysym = keyEvent.key().sym();
    if (auto special = mapSpecialKey(keysym)) {
        request.set_special_key(*special);
    }

    auto states = keyEvent.key().states();
    uint32_t modifierMask = 0;
    if (states.test(KeyState::Shift)) {
        modifierMask |= 1U << 0;
    }
    if (states.test(KeyState::Ctrl)) {
        modifierMask |= 1U << 1;
    }
    if (states.test(KeyState::Alt)) {
        modifierMask |= 1U << 2;
    }
    request.set_modifier(modifierMask);

    // Populate character if no special mapping exists
    if (!request.has_special_key()) {
        auto utf8 = Key::keySymToUTF8(keysym);
        if (!utf8.empty()) {
            request.set_character(utf8ToCodepoint(utf8));
        }
    }

    // Surrounding text context
    if (ic_->capabilityFlags().test(CapabilityFlag::SurroundingText) &&
        ic_->surroundingText().isValid()) {
        auto& surroundingText = ic_->surroundingText();
        auto ctx = request.mutable_context();
        ctx->set_context(surroundingText.text());
        ctx->set_anchor(surroundingText.anchor());
    }

    return request;
}

void HazkeyState::applyPreedit(
    const google::protobuf::RepeatedPtrField<
        hazkey::commands::ClientState_DecoratedTextPart>& preedit) {
    Text text;
    // text.append(preedit.beforecursor(), TextFormatFlag::NoFlag);
    // text.setCursor(text.textLength());
    // text.append(preedit.oncursor(), TextFormatFlag::HighLight);
    // text.append(preedit.aftercursor(), TextFormatFlag::Underline);
    for (const auto& preeditPart : preedit) {
        TextFormatFlags format = TextFormatFlag::NoFlag;
        for (auto& deco : preeditPart.decoration()) {
            switch (deco) {
                case hazkey::commands::
                    ClientState_TextDecoration_TEXT_DECORATION_UNDERLINE:
                    format |= TextFormatFlag::Underline;
                    break;
            }
        }
        text.append(preeditPart.text(), format);
    }
    preedit_.setPreedit(text);
}

void HazkeyState::applyAction(const hazkey::commands::ClientAction& action) {
    switch (action.action_case()) {
        case hazkey::commands::ClientAction::kShowCandidates: {
            google::protobuf::RepeatedPtrField<
                hazkey::commands::ShowCandidates::Candidate>
                candidates;
            for (const auto& candidate :
                 action.show_candidates().candidates()) {
                auto* added = candidates.Add();
                added->set_text(candidate.text());
                added->set_sub_hiragana(candidate.sub_hiragana());
            }
            auto candidateList =
                std::make_unique<HazkeyCandidateList>(candidates);
            candidateList->setPageSize(action.show_candidates().page_size());
            ic_->inputPanel().setCandidateList(std::move(candidateList));
            auto list = std::dynamic_pointer_cast<HazkeyCandidateList>(
                ic_->inputPanel().candidateList());
            if (list) {
                list->focus();
                setAuxDownText(std::string(_("[Press Tab to Select]")));
            }
            break;
        }
        case hazkey::commands::ClientAction::kCloseCandidates: {
            ic_->inputPanel().setCandidateList(nullptr);
            break;
        }
        case hazkey::commands::ClientAction::kComplete: {
            ic_->commitString(action.complete());
            // ic_->inputPanel().reset();
            break;
        }
        case hazkey::commands::ClientAction::ACTION_NOT_SET: {
            break;
        }
    }
}

void HazkeyState::applyClientState(const hazkey::commands::ClientState& state,
                                   KeyEvent& keyEvent) {
    applyPreedit(state.preedit_text());

    for (const auto& action : state.actions()) {
        applyAction(action);
    }

    if (state.filtered() || state.accepted()) {
        keyEvent.filterAndAccept();
    }
}

void HazkeyState::keyEvent(KeyEvent& keyEvent) {
    auto requestEvent = buildKeyEvent(keyEvent);
    auto response = engine_->server().processKeyEvent(requestEvent);
    if (!response.has_value()) {
        return;
    }
    applyClientState(response.value(), keyEvent);
}

}  // namespace fcitx
