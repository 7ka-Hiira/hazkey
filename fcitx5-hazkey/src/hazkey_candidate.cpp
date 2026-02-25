#include "hazkey_candidate.h"

#include <vector>

#include "commands.pb.h"

namespace fcitx {

/// CandidateWord

std::vector<std::string> HazkeyCandidateWord::getPreedit() const {
    if (hiragana_.empty()) return {candidate_};
    return {candidate_, hiragana_};
}

void HazkeyCandidateWord::select(InputContext* ic) const {
    FCITX_UNUSED(ic);
    // TODO: reinplement in cleaner way
    // KeyEvent keyEvent(ic, Key(FcitxKey_Return));
    // keyEvent = KeyEvent(
    //     ic, Key(KeySym(FcitxKey_1 + (index_ % ic->inputPanel() )),
    //             KeyState::Alt));
    // ic->keyEvent(keyEvent);
}

/// CandidateList

HazkeyCandidateList::HazkeyCandidateList(
    const google::protobuf::RepeatedPtrField<
        ::hazkey::commands::ShowCandidates::Candidate>
        candidates)
    : CommonCandidateList() {
    // CandidateWord needs to know their own index
    int i = 0;
    for (const auto& candidate : candidates) {
        append(std::make_unique<HazkeyCandidateWord>(i, candidate));
        i++;
    }
}

CandidateLayoutHint HazkeyCandidateList::layoutHint() const {
    return CandidateLayoutHint::Vertical;
}

void HazkeyCandidateList::focus() { setGlobalCursorIndex(0); }

const HazkeyCandidateWord& HazkeyCandidateList::getCandidate(
    int localIndex) const {
    return static_cast<const HazkeyCandidateWord&>(candidate(localIndex));
}

void HazkeyCandidateList::setCursorIndex(int localIndex) {
    if (localIndex < 0 || localIndex >= size()) {
        return;
    }
    int globalIndex = pageSize() * currentPage() + localIndex;
    setGlobalCursorIndex(globalIndex);
}

void HazkeyCandidateList::nextPage() {
    next();
    setCursorIndex(0);
}

void HazkeyCandidateList::prevPage() {
    prev();
    setCursorIndex(0);
}

bool HazkeyCandidateList::focused() const { return (globalCursorIndex() >= 0); }

}  // namespace fcitx
