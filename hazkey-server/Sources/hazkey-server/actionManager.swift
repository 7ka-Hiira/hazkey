import Foundation

extension Hazkey_Commands_KeyEvent {
    struct EventTypes: OptionSet {
        let rawValue: Int

        static let down = EventTypes(rawValue: 1 << 0)
        static let press = EventTypes(rawValue: 1 << 1)
        static let up = EventTypes(rawValue: 1 << 2)

        init(rawValue: Int) {
            self.rawValue = rawValue
        }

        init(protoEventTypes: [Hazkey_Commands_KeyEvent.EventType]) {
            let raw = protoEventTypes.reduce(0) { $0 | (1 << $1.rawValue) }
            self.init(rawValue: raw)
        }

        func contains(_ protoEventType: Hazkey_Commands_KeyEvent.EventType) -> Bool {
            return self.contains(EventTypes(rawValue: 1 << protoEventType.rawValue))
        }

    }
}

struct ModifierState: OptionSet {
    let rawValue: UInt32

    static let shift = ModifierState(rawValue: 1 << 0)
    static let ctrl = ModifierState(rawValue: 1 << 1)
    static let alt = ModifierState(rawValue: 1 << 2)
    static let allNonLock = ModifierState(rawValue: 0b111 << 0)

    func matches(mask: ModifierState, matcher: ModifierState) -> Bool {
        return intersection(mask) == matcher
    }
}

enum StateAction {
    case showCandidateList
    case showSuggestionList
    case selectNext
    case selectPrev
    case selectList(Int32)
    case completeList(Int32)
    case selectNextPage
    case selectPrevPage
    case completeSelected
    case extendSegment(Int32)
    case shrinkSegment(Int32)
    case setSegmentLength(Int32)
    case toHiragana
    case toFullKatakana
    case toHalfKatakana
    case toFullAlphabetTypeAuto
    case toHalfAlphabetTypeAuto
    case moveCursor(Int32)
    case setInputMode(Int32)
    case completeSegment
    case completeComposing
    case clearComposing
    case setUserState(Int32)
    case unsetUserState(Int32)
    case toggleUserState(Int32)
    case deleteFromCursor(Int32)
    case insert(Character, Character?)
    case insertDirect(Character)
}

struct HazkeyAction {
    let eventTypes: Hazkey_Commands_KeyEvent.EventTypes
    let continueProcessing: Bool
    let consume: Bool
    let actions: [StateAction]

    let clientStateMask: ClientStateBits
    let clientStateMatcher: ClientStateBits
    let modifierMask: ModifierState
    let modifierMatcher: ModifierState

    let userStateMask: UInt32
    let userStateMatcher: UInt32
}

final class ActionManager {
    private var actionsByKeyCode: [Hazkey_Commands_KeyCode: [HazkeyAction]] = [:]
    private var actionsBySpecialKey: [Hazkey_Commands_SpecialKey: [HazkeyAction]] = [:]
    private var actionsByCharCode: [UInt32: [HazkeyAction]] = [:]

    init() {
        registerDefaults()
    }

    private func register(
        keyCode: Hazkey_Commands_KeyCode,
        eventTypes: Hazkey_Commands_KeyEvent.EventTypes = [.down, .press],
        actions: [StateAction],
        continueProcessing: Bool = false,
        consume: Bool = true,
        clientStateMask: ClientStateBits = [],
        clientStateMatcher: ClientStateBits = [],
        modifierMask: ModifierState = .allNonLock,
        modifierMatcher: ModifierState = [],
        userStateMask: UInt32 = 0,
        userStateMatcher: UInt32 = 0
    ) {
        let action = HazkeyAction(
            eventTypes: eventTypes,
            continueProcessing: continueProcessing,
            consume: consume,
            actions: actions,
            clientStateMask: clientStateMask,
            clientStateMatcher: clientStateMatcher,
            modifierMask: modifierMask,
            modifierMatcher: modifierMatcher,
            userStateMask: userStateMask,
            userStateMatcher: userStateMatcher
        )
        actionsByKeyCode[keyCode, default: []].append(action)
    }

    private func register(
        specialKey: Hazkey_Commands_SpecialKey,
        eventTypes: Hazkey_Commands_KeyEvent.EventTypes = [.down, .press],
        actions: [StateAction],
        continueProcessing: Bool = false,
        consume: Bool = true,
        clientStateMask: ClientStateBits = [],
        clientStateMatcher: ClientStateBits = [],
        modifierMask: ModifierState = .allNonLock,
        modifierMatcher: ModifierState = [],
        userStateMask: UInt32 = 0,
        userStateMatcher: UInt32 = 0
    ) {
        let action = HazkeyAction(
            eventTypes: eventTypes,
            continueProcessing: continueProcessing,

            consume: consume,
            actions: actions,
            clientStateMask: clientStateMask,
            clientStateMatcher: clientStateMatcher,
            modifierMask: modifierMask,
            modifierMatcher: modifierMatcher,
            userStateMask: userStateMask,
            userStateMatcher: userStateMatcher
        )
        actionsBySpecialKey[specialKey, default: []].append(action)
    }

    private func register(
        character: UInt32,
        eventTypes: Hazkey_Commands_KeyEvent.EventTypes = [.down, .press],
        actions: [StateAction],
        continueProcessing: Bool = false,
        consume: Bool = true,
        clientStateMask: ClientStateBits = [],
        clientStateMatcher: ClientStateBits = [],
        modifierMask: ModifierState = .allNonLock,
        modifierMatcher: ModifierState = [],
        userStateMask: UInt32 = 0,
        userStateMatcher: UInt32 = 0
    ) {
        let action = HazkeyAction(
            eventTypes: eventTypes,
            continueProcessing: continueProcessing,
            consume: consume,
            actions: actions,
            clientStateMask: clientStateMask,
            clientStateMatcher: clientStateMatcher,
            modifierMask: modifierMask,
            modifierMatcher: modifierMatcher,
            userStateMask: userStateMask,
            userStateMatcher: userStateMatcher
        )
        actionsByCharCode[character, default: []].append(action)
    }

    private func registerDefaults() {
        // State masks for readability
        let noPreeditMask: ClientStateBits = [.hasPreedit]
        let noPreeditMatcher: ClientStateBits = []

        let hasPreeditMask: ClientStateBits = [.hasPreedit]
        let hasPreeditMatcher: ClientStateBits = [.hasPreedit]

        let preeditNoListMask: ClientStateBits = [.hasPreedit, .hasList]
        let preeditNoListMatcher: ClientStateBits = [.hasPreedit]

        let selectingListMask: ClientStateBits = [.hasList]
        let selectingListMatcher: ClientStateBits = [.hasList]

        // --- noPreeditKeyEvent ---

        // shift + space should commit " " directly.
        // only space without modifier should be sent to KKC.
        register(
            character: 32, actions: [.insert(" ", nil), .completeComposing],
            clientStateMask: noPreeditMask, clientStateMatcher: noPreeditMatcher)

        // --- preeditKeyEvent ---
        register(
            specialKey: .enter, actions: [.completeComposing],
            clientStateMask: hasPreeditMask, clientStateMatcher: hasPreeditMatcher)
        register(
            specialKey: .backspace, actions: [.deleteFromCursor(-1)],
            clientStateMask: hasPreeditMask, clientStateMatcher: hasPreeditMatcher)
        register(
            specialKey: .delete, actions: [.deleteFromCursor(1)],
            clientStateMask: hasPreeditMask, clientStateMatcher: hasPreeditMatcher)
        register(
            specialKey: .f6, actions: [.toHiragana],
            clientStateMask: hasPreeditMask, clientStateMatcher: hasPreeditMatcher)
        register(
            specialKey: .f7, actions: [.toFullKatakana],
            clientStateMask: hasPreeditMask, clientStateMatcher: hasPreeditMatcher)
        register(
            specialKey: .f8, actions: [.toHalfKatakana],
            clientStateMask: hasPreeditMask, clientStateMatcher: hasPreeditMatcher)
        register(
            specialKey: .f9, actions: [.toFullAlphabetTypeAuto],
            clientStateMask: hasPreeditMask, clientStateMatcher: hasPreeditMatcher)
        register(
            specialKey: .f10, actions: [.toHalfAlphabetTypeAuto],
            clientStateMask: hasPreeditMask, clientStateMatcher: hasPreeditMatcher)
        register(
            specialKey: .escape, actions: [.clearComposing],
            clientStateMask: hasPreeditMask, clientStateMatcher: hasPreeditMatcher)
        register(
            character: 32, actions: [.showCandidateList],
            clientStateMask: hasPreeditMask, clientStateMatcher: hasPreeditMatcher)  // space
        // when !hasList, space or arrow down should show candidate list first,
        // then select list items
        register(
            specialKey: .arrowDown, actions: [.showSuggestionList], continueProcessing: true,
            clientStateMask: preeditNoListMask, clientStateMatcher: preeditNoListMatcher)
        register(
            specialKey: .tab, actions: [.showSuggestionList], continueProcessing: true,
            clientStateMask: preeditNoListMask, clientStateMatcher: preeditNoListMatcher)
        register(
            specialKey: .arrowDown, actions: [.selectNext],
            clientStateMask: selectingListMask, clientStateMatcher: selectingListMatcher)
        register(
            specialKey: .tab, actions: [.selectNext],
            clientStateMask: selectingListMask, clientStateMatcher: selectingListMatcher)
        // TODO: implement ctrl+u, i, o, p, t

        // --- ListSelectingKeyEvent ---
        register(
            specialKey: .enter, actions: [.completeSelected],
            clientStateMask: selectingListMask, clientStateMatcher: selectingListMatcher)
        register(
            specialKey: .backspace, actions: [.selectList(-1)],
            clientStateMask: selectingListMask, clientStateMatcher: selectingListMatcher)
        register(
            specialKey: .escape, actions: [.selectList(-1)],
            clientStateMask: selectingListMask, clientStateMatcher: selectingListMatcher)
        register(
            character: 32, actions: [.selectNext],
            clientStateMask: selectingListMask, clientStateMatcher: selectingListMatcher)
        register(
            specialKey: .tab, actions: [.selectNext],
            clientStateMask: selectingListMask, clientStateMatcher: selectingListMatcher)
        register(
            specialKey: .arrowDown, actions: [.selectNext],
            clientStateMask: selectingListMask, clientStateMatcher: selectingListMatcher)
        register(
            specialKey: .arrowUp, actions: [.selectPrev],
            clientStateMask: selectingListMask, clientStateMatcher: selectingListMatcher)
        register(
            specialKey: .pageDown, actions: [.selectNextPage],
            clientStateMask: selectingListMask, clientStateMatcher: selectingListMatcher)
        register(
            specialKey: .pageUp, actions: [.selectPrevPage],
            clientStateMask: selectingListMask, clientStateMatcher: selectingListMatcher)
    }

    func match(
        event: Hazkey_Commands_KeyEvent,
        clientState: ClientStateBits,
        userState: UInt32 = 0
    ) -> [HazkeyAction] {

        NSLog(String(clientState.rawValue, radix: 2))

        let modifier = ModifierState(rawValue: event.modifier)
        let eventType = Hazkey_Commands_KeyEvent.EventTypes(protoEventTypes: [event.eventType])

        var availableActions: [HazkeyAction] = []

        if case .specialKey(let special)? = event.input,
            let actions = actionsBySpecialKey[special]
        {
            NSLog(actions.debugDescription)
            availableActions.append(contentsOf: actions.filter {
                matches(
                    $0,
                    eventTypes: eventType,
                    clientState: clientState,
                    modifier: modifier,
                    userState: userState
                )
            })
        }

        if case .character(let charCode)? = event.input,
            let actions = actionsByCharCode[charCode]
        {
            availableActions.append(contentsOf: actions.filter {
                matches(
                    $0,
                    eventTypes: eventType,
                    clientState: clientState,
                    modifier: modifier,
                    userState: userState
                )
            })
        }

        if let actions = actionsByKeyCode[event.code] {
            availableActions.append(contentsOf: actions.filter {
                matches(
                    $0,
                    eventTypes: eventType,
                    clientState: clientState,
                    modifier: modifier,
                    userState: userState
                )
            })
        }

        return availableActions
    }

    private func matches(
        _ action: HazkeyAction,
        eventTypes: Hazkey_Commands_KeyEvent.EventTypes,
        clientState: ClientStateBits,
        modifier: ModifierState,
        userState: UInt32
    ) -> Bool {
        if !clientState.matches(mask: action.clientStateMask, matcher: action.clientStateMatcher) {
            return false
        }
        if !modifier.matches(mask: action.modifierMask, matcher: action.modifierMatcher) {
            return false
        }
        if !matchesMaskedValue(
            value: userState,
            mask: action.userStateMask,
            matcher: action.userStateMatcher
        ) {
            return false
        }
        if !action.eventTypes.contains(eventTypes) {
            return false
        }
        return true
    }

    private func matchesMaskedValue(value: UInt32, mask: UInt32, matcher: UInt32) -> Bool {
        return (value & mask) == matcher
    }
}
