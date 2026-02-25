import Foundation
import KanaKanjiConverterModule
import SwiftUtils

extension HazkeyServerState {
    func updateCandidates(isSuggest: Bool) -> [Hazkey_Commands_ClientAction] {
        func canAppend(currentCount: Int, limit: Int) -> Bool {
            return !isSuggest || currentCount < limit
        }

        func appendCandidate(
            _ candidate: Candidate,
            hiraganaPreedit: String,
            hiraganaPreeditLen: Int,
            candidates: inout [HazkeyCandidate],
        ) {
            var clientCandidate = Hazkey_Commands_ShowCandidates.Candidate()
            clientCandidate.text = candidate.text
            let endIndex = min(candidate.rubyCount, hiraganaPreeditLen)
            clientCandidate.subHiragana = String(hiraganaPreedit.dropFirst(endIndex))
            candidates.append(HazkeyCandidate(server: candidate, client: clientCandidate))

        }

        var options = baseConvertRequestOptions
        let pageSizeSetting = candidatePageSize(isSuggest: isSuggest)
        let candidateLimit = candidateLimit(isSuggest: isSuggest)
        options.N_best = candidateLimit
        let predictionMode: ConvertRequestOptions.PredictionMode =
            shouldUsePrediction(isSuggest: isSuggest) ? .manualMix : .disabled
        options.requireJapanesePrediction = predictionMode

        var copiedComposingText = composingText.value
        if !isSuggest {
            let _ = copiedComposingText.moveCursorFromCursorPosition(
                count: copiedComposingText.toHiragana().count)
            copiedComposingText.insertAtCursorPosition(
                [
                    ComposingText.InputElement(
                        piece: .compositionSeparator,
                        inputStyle: .mapped(id: .tableName(currentTableName)))
                ])
        }

        let converted = converter.requestCandidates(copiedComposingText, options: options)
        let hiraganaPreedit = copiedComposingText.toHiragana()
        let hiraganaPreeditLen = hiraganaPreedit.count
        // var serverCandidates: [Candidate] = []
        // var clientCandidates: [Hazkey_Commands_ShowCandidates.Candidate] = []

        var candidates: [HazkeyCandidate] = []

        for candidate in converted.predictionResults {
            guard canAppend(currentCount: candidates.count, limit: candidateLimit) else {
                break
            }
            appendCandidate(
                candidate,
                hiraganaPreedit: hiraganaPreedit,
                hiraganaPreeditLen: hiraganaPreeditLen,
                candidates: &candidates
            )
        }

        var liveText = ""
        var liveCandidate: Candidate? = nil
        for candidate in converted.mainResults {
            let limitReached = !canAppend(
                currentCount: candidates.count, limit: candidateLimit)
            let isExactMatch = candidate.rubyCount == hiraganaPreeditLen

            if liveText.isEmpty && isExactMatch {
                liveText = candidate.text
                liveCandidate = candidate
                if isSuggest && candidates.count >= candidateLimit {
                    candidates.append(
                        HazkeyCandidate(
                            server: candidate, client: Hazkey_Commands_ShowCandidates.Candidate()))
                    break
                }
            }

            if limitReached && !liveText.isEmpty { break }

            appendCandidate(
                candidate,
                hiraganaPreedit: hiraganaPreedit,
                hiraganaPreeditLen: hiraganaPreeditLen,
                candidates: &candidates
            )
        }

        adjustAutoConversionState(
            hiraganaCount: hiraganaPreeditLen,
            liveText: &liveText,
            liveCandidate: &liveCandidate
        )

        self.preedit = [
            Hazkey_Commands_ClientState.DecoratedTextPart.with {
                $0.text = liveText.isEmpty ? hiraganaPreedit : liveText
            }
        ]
        self.preeditCandidate = liveCandidate

        let pageSize = max(pageSizeSetting, 0)

        self.candidateState.updateList(
            list: candidates,
            pageSize: pageSize,
        )

        var actions: [Hazkey_Commands_ClientAction] = []
        var show = Hazkey_Commands_ClientAction()
        show.showCandidates = Hazkey_Commands_ClientAction.ShowCandidates.with {
            $0.candidates = self.candidateState.list.map { candidate in
                Hazkey_Commands_ClientAction.ShowCandidates.Candidate.with {
                    $0.text = candidate.client.text
                    $0.subHiragana = candidate.client.subHiragana
                }
            }
            $0.pageSize = Int32(self.candidateState.pageSize)
        }
        actions.append(show)
        return actions
    }

    func moveListSelection(delta: Int) -> [Hazkey_Commands_ClientAction] {
        guard let selectedIndex = candidateState.selectedIndex else {
            return []
        }
        let count = candidateState.listCount
        candidateState.select(globalIndex: ((selectedIndex + delta) % count + count) % count)
        return []
    }

    func moveListPage(delta: Int) -> [Hazkey_Commands_ClientAction] {
        return moveListSelection(delta: delta * self.candidateState.pageSize)
    }

    func selectCandidate(globalIndex: Int) -> [Hazkey_Commands_ClientAction] {
        self.candidateState.select(globalIndex: globalIndex)
        return []
    }

    func completeCandidate() -> [Hazkey_Commands_ClientAction] {
        guard let candidate = self.candidateState.getCandidate()?.server ?? self.preeditCandidate
        else {
            resetSession()
            return []
        }
        return completeCandidate(candidate: candidate)
    }

    func completeCandidate(globalIndex: Int) -> [Hazkey_Commands_ClientAction] {
        guard let candidate = candidateState.getCandidate(globalIndex: globalIndex) else {
            resetSession()
            return []
        }
        return completeCandidate(candidate: candidate.server)
    }

    func completeCandidate(candidate: Candidate) -> [Hazkey_Commands_ClientAction] {
        composingText.value.prefixComplete(composingCount: candidate.composingCount)
        converter.setCompletedData(candidate)
        converter.updateLearningData(candidate)
        learningDataNeedsCommit = true

        var complete = Hazkey_Commands_ClientAction()
        complete.complete = candidate.text

        resetSession()
        return [complete]
    }

    private func candidateLimit(isSuggest: Bool) -> Int {
        if isSuggest
            && serverConfig.currentProfile.suggestionListMode
                == Hazkey_Config_Profile.SuggestionListMode.suggestionListDisabled
        {
            return 1
        } else if isSuggest {
            return Int(serverConfig.currentProfile.numSuggestions)
        } else {
            return Int(serverConfig.currentProfile.numCandidatesPerPage)
        }
    }

    private func candidatePageSize(isSuggest: Bool) -> Int {
        if isSuggest
            && serverConfig.currentProfile.suggestionListMode
                == Hazkey_Config_Profile.SuggestionListMode.suggestionListDisabled
        {
            return 0
        } else if isSuggest {
            return Int(serverConfig.currentProfile.numSuggestions)
        } else {
            return Int(serverConfig.currentProfile.numCandidatesPerPage)
        }
    }

    private func shouldUsePrediction(isSuggest: Bool) -> Bool {
        return isSuggest
            && serverConfig.currentProfile.suggestionListMode
                == Hazkey_Config_Profile.SuggestionListMode.suggestionListShowPredictiveResults
    }

    private func adjustAutoConversionState(
        hiraganaCount: Int,
        liveText: inout String,
        liveCandidate: inout Candidate?
    ) {
        let autoConvertMode = serverConfig.currentProfile.autoConvertMode
        let disableForSingleChar =
            autoConvertMode == .autoConvertForMultipleChars && hiraganaCount == 1
        let autoConvertDisabled =
            autoConvertMode == .autoConvertDisabled
        if disableForSingleChar || autoConvertDisabled {
            liveText = ""
            liveCandidate = nil
        }
    }
}
