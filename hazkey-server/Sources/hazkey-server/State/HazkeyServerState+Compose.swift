import Foundation
import KanaKanjiConverterModule
import SwiftUtils

extension HazkeyServerState {
    func completeComposing() -> [Hazkey_Commands_ClientAction] {
        let textResponse = getComposingString()
        var complete = Hazkey_Commands_ClientAction()
        complete.complete = textResponse
        resetSession()
        return [complete]
    }

    func clearComposing() -> [Hazkey_Commands_ClientAction] {
        resetSession()
        return []
    }

    // apply upper/lower/capitalized case state if possible
    private func getDirectAlphabetNormalMode(fullwidth: Bool) -> DirectConversionCharacterType {
        let text = self.composingText.value.toAlphabet(fullwidth)
        let mode: DirectConversionCharacterType = switch text {
        case text.lowercased(): .alphabetHalfLower
        case text.uppercased(): .alphabetHalfUpper
        case text.capitalized: .alphabetHalfCapitalized
        default: .alphabetHalfNormal
        }
        self.directConversionMode = mode
        return mode
    }

    func directConvertAutoRotateCase(fullwidth: Bool)
        -> [Hazkey_Commands_ClientAction]
    {
        let nextCase: DirectConversionCharacterType = if fullwidth {
            switch self.directConversionMode {
            case .alphabetFullNormal: .alphabetFullLower
            case .alphabetFullLower: .alphabetFullUpper
            case .alphabetFullUpper: .alphabetFullCapitalized
            case .alphabetFullCapitalized: getDirectAlphabetNormalMode(fullwidth: true)
            default: getDirectAlphabetNormalMode(fullwidth: true)
            }
        } else {
            switch self.directConversionMode {
            case .alphabetHalfNormal: getDirectAlphabetNormalMode(fullwidth: false)
            case .alphabetHalfLower: .alphabetHalfUpper
            case .alphabetHalfUpper: .alphabetHalfCapitalized
            case .alphabetHalfCapitalized: getDirectAlphabetNormalMode(fullwidth: false)
            default: getDirectAlphabetNormalMode(fullwidth: false)
            }
        }
        self.preedit = [
            Hazkey_Commands_ClientState.DecoratedTextPart.with {
                $0.text = getComposingString(charType: nextCase)
            }
        ]
        self.directConversionMode = nextCase
        return []
    }

    func directConvert(charType: DirectConversionCharacterType = .hiragana)
        -> [Hazkey_Commands_ClientAction]
    {
        self.preedit = [
            Hazkey_Commands_ClientState.DecoratedTextPart.with {
                $0.text = getComposingString(charType: charType)
            }
        ]
        self.directConversionMode = charType
        return []
    }
}
