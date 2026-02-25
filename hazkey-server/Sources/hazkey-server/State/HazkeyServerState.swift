import Foundation
import KanaKanjiConverterModule
import SwiftUtils

// Bits: 0 isAlphabet, 1 isFullwidth, 2-3 variant (00: hiragana/lower, 01: normal, 10: capitalized, 11: katakana/upper)
enum DirectConversionCharacterType: UInt32 {
    case hiragana = 0b0010
    case katakanaFull = 0b1110
    case katakanaHalf = 0b1100
    case alphabetFullNormal = 0b0111
    case alphabetFullUpper = 0b1111
    case alphabetFullLower = 0b0011
    case alphabetFullCapitalized = 0b1011
    case alphabetHalfNormal = 0b0101
    case alphabetHalfUpper = 0b1101
    case alphabetHalfLower = 0b0001
    case alphabetHalfCapitalized = 0b1001
}

struct ClientStateBits: OptionSet {
    let rawValue: UInt32

    static let subInputMode = ClientStateBits(rawValue: 1 << 0)
    static let hasPreedit = ClientStateBits(rawValue: 1 << 1)
    static let hasList = ClientStateBits(rawValue: 1 << 2)
    static let hasSelectedCandidate = ClientStateBits(rawValue: 1 << 3)
    static let directConversionActive = ClientStateBits(rawValue: 1 << 4)
    static let learningDataDirty = ClientStateBits(rawValue: 1 << 9)

    private static let directConversionShift: UInt32 = 5
    private static let directConversionMask = ClientStateBits(
        rawValue: UInt32(0b1111) << directConversionShift)
    private static let directConversionFieldMask =
        directConversionMask.rawValue | directConversionActive.rawValue

    static func directConversion(_ type: DirectConversionCharacterType) -> ClientStateBits {
        let shifted = ClientStateBits(rawValue: type.rawValue << directConversionShift)
        return [.directConversionActive, shifted]
    }

    func directConversionType() -> DirectConversionCharacterType? {
        guard contains(.directConversionActive) else { return nil }
        let raw = (rawValue & ClientStateBits.directConversionMask.rawValue)
            >> ClientStateBits.directConversionShift
        return DirectConversionCharacterType(rawValue: raw)
    }

    func matches(mask: ClientStateBits, matcher: ClientStateBits) -> Bool {
        return intersection(mask) == matcher
    }

    mutating func set(_ flag: ClientStateBits, to isOn: Bool) {
        if isOn {
            insert(flag)
        } else {
            remove(flag)
        }
    }

    mutating func setDirectConversion(_ type: DirectConversionCharacterType?) {
        let cleared = ClientStateBits(
            rawValue: rawValue & ~ClientStateBits.directConversionFieldMask)
        self = cleared
        guard let type else { return }
        formUnion(ClientStateBits.directConversion(type))
    }
}

final class HazkeyServerState {
    // Server-side state now lives here too.
    internal let serverConfig: HazkeyServerConfig
    internal let converter: KanaKanjiConverter
    private(set) var composingText: ComposingTextBox = ComposingTextBox()

    internal var preedit: [Hazkey_Commands_ClientState.DecoratedTextPart] = []
    internal var preeditCandidate: Candidate? = nil

    internal var directConversionMode: DirectConversionCharacterType? = nil {
        didSet { clientStateBits.setDirectConversion(directConversionMode) }
    }

    private var keymap: Keymap
    internal var currentTableName: String
    internal var baseConvertRequestOptions: ConvertRequestOptions

    internal var isShiftPressedAlone = false

    private let actionManager: ActionManager
    internal var candidateState: CandidateState

    private var clientStateBits: ClientStateBits = []

    private var isSubInputMode: Bool {
        get { clientStateBits.contains(.subInputMode) }
        set { clientStateBits.set(.subInputMode, to: newValue) }
    }

    internal var learningDataNeedsCommit: Bool {
        get { clientStateBits.contains(.learningDataDirty) }
        set { clientStateBits.set(.learningDataDirty, to: newValue) }
    }

    // @discardableResult
    // internal func refreshDerivedClientState() -> ClientStateBits {
    //     var state = clientStateBits
    //     state.set(.hasPreedit, to: !composingText.value.toHiragana().isEmpty)
    //     state.set(.hasList, to: candidateState.listCount > 0)
    //     state.set(.hasSelectedCandidate, to: candidateState.isSelecting)
    //     state.setDirectConversion(directConversionMode)
    //     clientStateBits = state
    //     return state
    // }

    // does not gurantee to be consistent bit layout.
    private var clientState: ClientStateBits { clientStateBits }

    init() {
        self.serverConfig = HazkeyServerConfig()
        self.converter = KanaKanjiConverter.init(dictionaryURL: serverConfig.dictionaryPath)

        // Initialize keymap and table
        self.keymap = serverConfig.loadKeymap()
        self.currentTableName = UUID().uuidString
        serverConfig.loadInputTable(tableName: currentTableName)

        // Create user state directories (history data)
        do {
            let newPath = HazkeyServerConfig.getStateDirectory().appendingPathComponent(
                "memory", isDirectory: true)
            if !FileManager.default.fileExists(atPath: newPath.path) {
                let oldPath = HazkeyServerConfig.getDataDirectory().appendingPathComponent(
                    "memory", isDirectory: true)
                if FileManager.default.fileExists(atPath: oldPath.path) {
                    // v0.2.0の保存パスからの移動対応
                    try FileManager.default.createDirectory(
                        at: HazkeyServerConfig.getStateDirectory(),
                        withIntermediateDirectories: true)
                    try FileManager.default.moveItem(at: oldPath, to: newPath)
                } else {
                    try FileManager.default.createDirectory(
                        at: newPath, withIntermediateDirectories: true)
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

        self.baseConvertRequestOptions = serverConfig.genBaseConvertRequestOptions()
        self.actionManager = ActionManager()
        self.candidateState = CandidateState()
        resetSession()
    }

    func resetSession() {
        createComposingTextInstance()
        candidateState = CandidateState()
        preedit = []
        preeditCandidate = nil
        directConversionMode = nil
        // refreshDerivedClientState()
    }

    func handle(event: Hazkey_Commands_KeyEvent) -> Hazkey_Commands_ClientState {
        NSLog(try! event.jsonString())

        // refreshDerivedClientState()

        // update context
        setContext(
            surroundingText: event.context.context,
            anchorIndex: Int(event.context.anchor))

        var actions: [Hazkey_Commands_ClientAction] = []
        var filtered = false
        var accepted = false

        var continueProcessing = true

        let hazkeyActions = actionManager.match(
            event: event,
            clientState: clientState
        )
        NSLog(hazkeyActions.debugDescription)
        if !hazkeyActions.isEmpty {
            for hazkeyAction in hazkeyActions {
                actions.append(contentsOf: apply(stateActions: hazkeyAction.actions))
                filtered = hazkeyAction.consume
                if hazkeyAction.continueProcessing {
                    continueProcessing = false
                    break
                }
            }
            accepted = true
        }

        if continueProcessing,
            [.press, .down].contains(event.eventType),
            case .character(let charCode)? = event.input,
            let scalar = UnicodeScalar(charCode)
        {
            NSLog("input character: \(scalar)")
            if isSubInputMode {
                actions.append(contentsOf: apply(stateActions: [.insertDirect(Character(scalar))]))
            } else {
                actions.append(
                    contentsOf: apply(stateActions: [.insert(Character(scalar), nil)]))
            }
            filtered = true
            accepted = true
        }

        // refreshDerivedClientState()

        return buildState(filtered: filtered, accepted: accepted, actions: actions)
    }

    private func apply(stateActions: [StateAction]) -> [Hazkey_Commands_ClientAction] {
        return stateActions.flatMap { apply(stateAction: $0) }
    }

    private func apply(stateAction: StateAction) -> [Hazkey_Commands_ClientAction] {
        switch stateAction {
        case .showCandidateList:
            return updateCandidates(isSuggest: false)
        case .showSuggestionList:
            return updateCandidates(isSuggest: true)
        case .selectNext:
            return moveListSelection(delta: 1)
        case .selectPrev:
            return moveListSelection(delta: -1)
        case .selectList(let index):
            return selectCandidate(globalIndex: Int(index))
        case .completeList(let index):
            return completeCandidate(globalIndex: Int(index))
        case .selectNextPage:
            return moveListPage(delta: 1)
        case .selectPrevPage:
            return moveListPage(delta: -1)
        case .completeSelected:
            return completeCandidate()
        case .completeComposing:
            return completeComposing()
        case .clearComposing:
            return clearComposing()
        case .deleteFromCursor(let count):
            return deleteFromCursor(count: Int(count))
        case .moveCursor(let count):
            return moveCursorAction(offset: Int(count))
        case .toHiragana:
            return directConvert(charType: .hiragana)
        case .toFullKatakana:
            return directConvert(charType: .katakanaFull)
        case .toHalfKatakana:
            return directConvert(charType: .katakanaHalf)
        case .toFullAlphabetTypeAuto:
            return directConvertAutoRotateCase(fullwidth: true)
        case .toHalfAlphabetTypeAuto:
            return directConvertAutoRotateCase(fullwidth: false)
        case .insert(let char, let rawChar):
            return insert(char: char, rawChar: rawChar)
        case .insertDirect(let rawChar):
            return insertDirect(char: rawChar)
        default:
            return []
        }
    }

    private func insertDirect(char: Character) -> [Hazkey_Commands_ClientAction] {
        composingText.value.insertAtCursorPosition(String(char), inputStyle: .direct)
        return updateCandidates(isSuggest: true)
    }

    private func insert(char: Character, rawChar: Character?) -> [Hazkey_Commands_ClientAction] {
        let piece: InputPiece
        piece = .key(
            intention: char, input: rawChar ?? char, modifiers: [])

        composingText.value.insertAtCursorPosition([
            ComposingText.InputElement(
                piece: piece,
                inputStyle: .mapped(id: .tableName(currentTableName)))
        ])
        return updateCandidates(isSuggest: true)
    }

    private func deleteFromCursor(count: Int) -> [Hazkey_Commands_ClientAction] {
        composingText.value.deleteBackwardFromCursorPosition(count: count)
        return updateCandidates(isSuggest: true)
    }

    private func moveCursorAction(offset: Int) -> [Hazkey_Commands_ClientAction] {
        _ = composingText.value.moveCursorFromCursorPosition(count: offset)
        return updateCandidates(isSuggest: true)
    }

    private func buildState(
        filtered: Bool,
        accepted: Bool,
        actions: [Hazkey_Commands_ClientAction]
    ) -> Hazkey_Commands_ClientState {
        var state: Hazkey_Commands_ClientState = Hazkey_Commands_ClientState()
        state.filtered = filtered
        state.accepted = accepted
        state.currentInputMode = isSubInputMode ? .direct : .normal
        state.preeditText = self.preedit
        state.auxText = makeAuxText()
        state.actions = actions
        return state
    }
    func setContext(surroundingText: String, anchorIndex: Int) {
        let leftContext = String(surroundingText.prefix(anchorIndex))
        baseConvertRequestOptions.zenzaiMode = serverConfig.genZenzaiMode(
            leftContext: leftContext)
    }

    func createComposingTextInstance() {
        composingText = ComposingTextBox()
    }

    func makeAuxText() -> [Hazkey_Commands_ClientState.DecoratedTextPart] {
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
            return []
        }

        var result = [
            Hazkey_Commands_ClientState.DecoratedTextPart.with {
                $0.text = safeSubstring(hiragana, start: 0, end: cursorPos)
            }
        ]
        if cursorPos < hiragana.count {
            result.append(
                Hazkey_Commands_ClientState.DecoratedTextPart.with {
                    $0.text = safeSubstring(hiragana, start: cursorPos, end: cursorPos + 1)
                    $0.decoration = [.underline]
                })
        }
        if cursorPos + 1 < hiragana.count {
            result.append(
                Hazkey_Commands_ClientState.DecoratedTextPart.with {
                    $0.text = safeSubstring(hiragana, start: cursorPos + 1, end: hiragana.count)
                })
        }

        return result
    }

    func getComposingString(
        charType: DirectConversionCharacterType = .hiragana,
    ) -> String {
        return switch charType {
        case .hiragana: composingText.value.toHiragana()
        case .katakanaFull: composingText.value.toKatakana(true)
        case .katakanaHalf: composingText.value.toKatakana(false)
        case .alphabetFullNormal: composingText.value.toAlphabet(true)
        case .alphabetHalfNormal: composingText.value.toAlphabet(false)
        case .alphabetFullUpper: composingText.value.toAlphabet(true).uppercased()
        case .alphabetFullLower: composingText.value.toAlphabet(true).lowercased()
        case .alphabetFullCapitalized: composingText.value.toAlphabet(true).capitalized
        case .alphabetHalfUpper: composingText.value.toAlphabet(false).uppercased()
        case .alphabetHalfLower: composingText.value.toAlphabet(false).lowercased()
        case .alphabetHalfCapitalized: composingText.value.toAlphabet(false).capitalized
        }
    }

    // direct apis
    func saveLearningData() {
        if learningDataNeedsCommit {
            converter.commitUpdateLearningData()
            learningDataNeedsCommit = false
        }
    }

    func commitAllPreedit() {
        composingText.value.prefixComplete(composingCount: .inputCount(composingText.value.input.count))
    }

    func clearProfileLearningData(){
        converter.resetMemory()
    }

    func reinitializeConfiguration() {
        NSLog("Reinitializing state configuration...")

        keymap = serverConfig.loadKeymap()

        let newTableName = UUID().uuidString
        serverConfig.loadInputTable(tableName: newTableName)
        currentTableName = newTableName

        baseConvertRequestOptions = serverConfig.genBaseConvertRequestOptions()

        composingText = ComposingTextBox()
        candidateState = CandidateState()

        NSLog("State configuration reinitialized successfully")
    }
}
