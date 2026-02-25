#ifndef _FCITX5_HAZKEY_HAZKEY_STATE_H_
#define _FCITX5_HAZKEY_HAZKEY_STATE_H_

#include <fcitx/inputcontext.h>
#include <fcitx/inputpanel.h>
#include <fcitx/surroundingtext.h>

#include "commands.pb.h"
#include "hazkey_preedit.h"

namespace fcitx {

class HazkeyEngine;

class HazkeyState : public InputContextProperty {
   public:
    HazkeyState(HazkeyEngine* engine, InputContext* ic);

    void commitPreedit();
    void keyEvent(KeyEvent& keyEvent);
    void resetClient();

   private:
    void setAuxDownText(std::optional<std::string> optText);
    hazkey::commands::KeyEvent buildKeyEvent(KeyEvent& keyEvent);
    void applyClientState(const hazkey::commands::ClientState& state,
                          KeyEvent& keyEvent);
    void applyPreedit(
        const google::protobuf::RepeatedPtrField<hazkey::commands::ClientState_DecoratedTextPart> &preedit);
    void applyAction(const hazkey::commands::ClientAction& action);

    HazkeyEngine* engine_;
    InputContext* ic_;
    HazkeyPreedit preedit_;
};

}  // namespace fcitx

#endif  // _FCITX5_HAZKEY_HAZKEY_STATE_H_
