import Foundation
import KanaKanjiConverterModule
import SwiftUtils

class HazkeyServerState {
    let serverConfig: HazkeyServerConfig
    let converter: KanaKanjiConverter
    var currentCandidateList: [Candidate]?
    var composingText: ComposingTextBox = ComposingTextBox()

    var isShiftPressedAlone = false
    var isSubInputMode = false
    var learningDataNeedsCommit = false

    var keymap: Keymap
    var currentTableName: String
    var baseConvertRequestOptions: ConvertRequestOptions

    init() {
        self.serverConfig = HazkeyServerConfig()

        self.converter = KanaKanjiConverter.init(dictionaryURL: serverConfig.dictionaryPath)

        // Initialize keymap and table
        self.keymap = serverConfig.loadKeymap()
        self.currentTableName = UUID().uuidString
        serverConfig.loadInputTable(tableName: currentTableName)

        // Create user state directories (history data)
        do {
            let newPath = HazkeyServerConfig.getStateDirectory().appendingPathComponent("memory", isDirectory: true)
            if !FileManager.default.fileExists(atPath: newPath.path) {
                let oldPath = HazkeyServerConfig.getDataDirectory().appendingPathComponent("memory", isDirectory: true)
                if FileManager.default.fileExists(atPath: oldPath.path) {
                    // v0.2.0の保存パスからの移動対応
                    try FileManager.default.createDirectory(at: HazkeyServerConfig.getStateDirectory(), withIntermediateDirectories: true)
                    try FileManager.default.moveItem(at: oldPath, to: newPath)
                    } else {
                    try FileManager.default.createDirectory(at: newPath, withIntermediateDirectories: true)
                }
            }
        } catch {
            NSLog("Failed to create user memory directory: \(error.localizedDescription)")
        }

        // Create user cache directories (user dictionary)
        do {
            try FileManager.default.createDirectory(
                at: HazkeyServerConfig.getCacheDirectory().appendingPathComponent(
                    "shared", isDirectory: true), withIntermediateDirectories: true)
        } catch {
            NSLog("Failed to create user cache directory: \(error.localizedDescription)")
        }

        // Initialize base convert options
        self.baseConvertRequestOptions = serverConfig.genBaseConvertRequestOptions()
    }

    func setContext(surroundingText: String, anchorIndex: Int) -> Hazkey_ResponseEnvelope {
        let leftContext = String(surroundingText.prefix(anchorIndex))
        baseConvertRequestOptions.zenzaiMode = serverConfig.genZenzaiMode(
            leftContext: leftContext)

        return Hazkey_ResponseEnvelope.with {
            $0.status = .success
        }
    }

    /// ComposingText

    func createComposingTextInstanse() -> Hazkey_ResponseEnvelope {
        composingText = ComposingTextBox()
        currentCandidateList = nil
        isSubInputMode = false
        isShiftPressedAlone = false
        return Hazkey_ResponseEnvelope.with {
            $0.status = .success
        }
    }

    func inputChar(inputString: String) -> Hazkey_ResponseEnvelope {
        guard let inputChar = inputString.first else {
            return Hazkey_ResponseEnvelope.with {
                $0.status = .failed
                $0.errorMessage = "failed to get first unicode character"
            }
        }
        isSubInputMode =
            isSubInputMode
            || (isShiftPressedAlone
                && serverConfig.getSubModeEntryPointChars().contains(inputChar))
        isShiftPressedAlone = false
        if isSubInputMode {
            composingText.value.insertAtCursorPosition(String(inputChar), inputStyle: .direct)
        } else {
            let piece: InputPiece
            if let (intentionChar, overrideInputChar) = keymap[inputChar] {
                piece = .key(
                    intention: intentionChar, input: overrideInputChar ?? inputChar, modifiers: [])
            } else {
                piece = .character(inputChar)
            }

            composingText.value.insertAtCursorPosition([
                ComposingText.InputElement(
                    piece: piece,
                    inputStyle: .mapped(id: .tableName(currentTableName)))
            ])
        }
        return Hazkey_ResponseEnvelope.with { $0.status = .success }
    }

    func processModifierEvent(
        modifier: Hazkey_Commands_ModifierEvent.ModifierType,
        event: Hazkey_Commands_ModifierEvent.EventType
    ) -> Hazkey_ResponseEnvelope {
        switch modifier {
        case .shift:
            switch event {
            case .press:
                isShiftPressedAlone = true
            case .release:
                if isShiftPressedAlone {
                    isSubInputMode.toggle()
                    isShiftPressedAlone = false
                }
            case .unspecified, .UNRECOGNIZED(_):
                NSLog("Unexpected event type")
                return Hazkey_ResponseEnvelope.with {
                    $0.status = .failed
                    $0.errorMessage = "Unexpected event type"
                }
            }
        case .unspecified, .UNRECOGNIZED(_):
            NSLog("Unexpected modifier type")
            return Hazkey_ResponseEnvelope.with {
                $0.status = .failed
                $0.errorMessage = "Unexpected modifier type"
            }
        }
        return Hazkey_ResponseEnvelope.with { $0.status = .success }
    }

    func getCurrentInputMode() -> Hazkey_ResponseEnvelope {
        return Hazkey_ResponseEnvelope.with {
            $0.status = .success
            $0.currentInputModeInfo = Hazkey_Commands_CurrentInputModeInfo.with {
                $0.inputMode = isSubInputMode ? .direct : .normal
            }
        }
    }

    func saveLearningData() -> Hazkey_ResponseEnvelope {
        if learningDataNeedsCommit {
            converter.commitUpdateLearningData()
            learningDataNeedsCommit = false
        }
        return Hazkey_ResponseEnvelope.with {
            $0.status = .success
        }
    }

    func deleteLeft() -> Hazkey_ResponseEnvelope {
        composingText.value.deleteBackwardFromCursorPosition(count: 1)
        return Hazkey_ResponseEnvelope.with {
            $0.status = .success
        }
    }

    func deleteRight() -> Hazkey_ResponseEnvelope {
        composingText.value.deleteForwardFromCursorPosition(count: 1)
        return Hazkey_ResponseEnvelope.with {
            $0.status = .success
        }
    }

    func completePrefix(candidateIndex: Int) -> Hazkey_ResponseEnvelope {
        if let completedCandidate = currentCandidateList?[candidateIndex] {
            composingText.value.prefixComplete(composingCount: completedCandidate.composingCount)
            converter.setCompletedData(completedCandidate)
            converter.updateLearningData(completedCandidate)
            learningDataNeedsCommit = true
        } else {
            return Hazkey_ResponseEnvelope.with {
                $0.status = .failed
                $0.errorMessage = "Candidate index \(candidateIndex) not found."
            }
        }
        return Hazkey_ResponseEnvelope.with {
            $0.status = .success
        }
    }

    func moveCursor(offset: Int) -> Hazkey_ResponseEnvelope {
        _ = composingText.value.moveCursorFromCursorPosition(count: offset)
        return Hazkey_ResponseEnvelope.with {
            $0.status = .success
        }
    }

    /// ComposingText -> Characters

    func getHiraganaWithCursor() -> Hazkey_ResponseEnvelope {
        func safeSubstring(_ text: String, start: Int, end: Int) -> String {
            guard start >= 0, end >= 0, start < text.count, end <= text.count, start < end else {
                return ""
            }

            let startIndex = text.index(text.startIndex, offsetBy: start)
            let endIndex = text.index(text.startIndex, offsetBy: end)

            return String(text[startIndex..<endIndex])
        }

        let hiragana = composingText.value.toHiragana()
        let cursorPos = composingText.value.convertTargetCursorPosition

        if (serverConfig.currentProfile.auxTextMode
            == Hazkey_Config_Profile.AuxTextMode.auxTextDisabled)
            || (serverConfig.currentProfile.auxTextMode
                == Hazkey_Config_Profile.AuxTextMode.auxTextShowWhenCursorNotAtEnd
                && hiragana.count == cursorPos)
        {
            return Hazkey_ResponseEnvelope.with {
                $0.status = .success
                $0.textWithCursor = Hazkey_Commands_TextWithCursor.with {
                    $0.beforeCursosr = ""
                    $0.onCursor = ""
                    $0.afterCursor = ""
                }
            }
        }

        return Hazkey_ResponseEnvelope.with {
            $0.status = .success
            $0.textWithCursor = Hazkey_Commands_TextWithCursor.with {
                $0.beforeCursosr = safeSubstring(hiragana, start: 0, end: cursorPos)
                $0.onCursor = safeSubstring(hiragana, start: cursorPos, end: cursorPos + 1)
                $0.afterCursor = safeSubstring(hiragana, start: cursorPos + 1, end: hiragana.count)
            }
        }
    }

    func getComposingString(
        charType: Hazkey_Commands_GetComposingString.CharType,
        currentPreedit: String
    ) -> Hazkey_ResponseEnvelope {
        let result: String
        switch charType {
        case .hiragana:
            result = composingText.value.toHiragana()
        case .katakanaFull:
            result = composingText.value.toKatakana(true)
        case .katakanaHalf:
            result = composingText.value.toKatakana(false)
        case .alphabetFull:
            result = cycleAlphabetCase(
                composingText.value.toAlphabet(true), preedit: currentPreedit)
        case .alphabetHalf:
            result = cycleAlphabetCase(
                composingText.value.toAlphabet(false), preedit: currentPreedit)
        case .UNRECOGNIZED:
            return Hazkey_ResponseEnvelope.with {
                $0.status = .failed
                $0.errorMessage = "unrecognized charType: \(charType.rawValue)"
            }
        }
        return Hazkey_ResponseEnvelope.with {
            $0.status = .success
            $0.text = result
        }
    }

    /// Candidates

    // TODO: return error message
    func getCandidates(is_suggest: Bool) -> Hazkey_ResponseEnvelope {
        var options = baseConvertRequestOptions
        options.N_best = {
            if is_suggest
                && serverConfig.currentProfile.suggestionListMode
                    == Hazkey_Config_Profile.SuggestionListMode.suggestionListDisabled
            {
                // for auto conversion
                return 1
            } else if is_suggest {
                return Int(serverConfig.currentProfile.numSuggestions)
            } else {
                return Int(serverConfig.currentProfile.numCandidatesPerPage)
            }
        }()

        options.requireJapanesePrediction =
            is_suggest
                && serverConfig.currentProfile.suggestionListMode
                    == Hazkey_Config_Profile.SuggestionListMode.suggestionListShowPredictiveResults
            ? .autoMix : .disabled
        options.requireEnglishPrediction = options.requireJapanesePrediction

        var copiedComposingText = composingText.value

        if !is_suggest {
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

        currentCandidateList = converted.mainResults

        let hiraganaPreedit = copiedComposingText.toHiragana()

        var candidatesResult = Hazkey_Commands_CandidatesResult()
        candidatesResult.liveTextIndex = -1
        candidatesResult.candidates = converted.mainResults.enumerated().map { index, c in
            var candidate = Hazkey_Commands_CandidatesResult.Candidate()
            candidate.text = c.text

            let endIndex = min(c.rubyCount, hiraganaPreedit.count)
            candidate.subHiragana = String(hiraganaPreedit.dropFirst(endIndex))

            // Set liveText if conditions are met
            if candidatesResult.liveText.isEmpty && c.rubyCount == hiraganaPreedit.count {
                candidatesResult.liveText = c.text
                candidatesResult.liveTextIndex = Int32(index)
            }

            return candidate
        }

        // Do not automatically convert if there is only one character
        if serverConfig.currentProfile.autoConvertMode
            == Hazkey_Config_Profile.AutoConvertMode.autoConvertForMultipleChars
            && hiraganaPreedit.count == 1
        {
            candidatesResult.liveText = ""
            candidatesResult.liveTextIndex = -1
        } else if serverConfig.currentProfile.autoConvertMode
            == Hazkey_Config_Profile.AutoConvertMode.autoConvertDisabled
        {
            candidatesResult.liveText = ""
            candidatesResult.liveTextIndex = -1
        }

        candidatesResult.pageSize = {
            if is_suggest
                && serverConfig.currentProfile.suggestionListMode
                    == Hazkey_Config_Profile.SuggestionListMode.suggestionListDisabled
            {
                return 0
            } else if is_suggest {
                return serverConfig.currentProfile.numSuggestions
            } else {
                return serverConfig.currentProfile.numCandidatesPerPage
            }
        }()

        return Hazkey_ResponseEnvelope.with {
            $0.status = .success
            $0.candidates = candidatesResult
        }
    }

    func clearProfileLearningData() -> Hazkey_ResponseEnvelope {
        converter.resetMemory()
        return Hazkey_ResponseEnvelope.with {
            $0.status = .success
        }
    }

    func reinitializeConfiguration() {
        NSLog("Reinitializing state configuration...")

        self.keymap = serverConfig.loadKeymap()

        let newTableName = UUID().uuidString
        serverConfig.loadInputTable(tableName: newTableName)
        self.currentTableName = newTableName

        self.baseConvertRequestOptions = serverConfig.genBaseConvertRequestOptions()

        self.composingText = ComposingTextBox()
        self.currentCandidateList = nil
        self.isSubInputMode = false
        self.isShiftPressedAlone = false

        NSLog("State configuration reinitialized successfully")
    }

}
