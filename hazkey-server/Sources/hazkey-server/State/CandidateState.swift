import Foundation
import KanaKanjiConverterModule

struct HazkeyCandidate {
    let server: Candidate
    let client: Hazkey_Commands_ShowCandidates.Candidate
}

final class CandidateState {
    private(set) var list: [HazkeyCandidate] = []
    private(set) var pageSize: Int = 0
    private(set) var selectedIndex: Int? = nil

    internal func select(globalIndex: Int) {
        guard globalIndex >= 0, globalIndex < list.count else { return }
        self.selectedIndex = globalIndex
    }

    internal func unselect() {
        self.selectedIndex = nil
    }

    internal var isSelecting: Bool {
        return selectedIndex != nil
    }

    internal var selectedPage: Int {
        guard let selectedIndex = selectedIndex, pageSize > 0 else { return 0 }
        return selectedIndex / pageSize
    }

    internal func getCandidate(globalIndex: Int) -> HazkeyCandidate? {
        guard globalIndex >= 0, globalIndex < list.count else { return nil }
        return list[globalIndex]
    }

    internal func getCandidate() -> HazkeyCandidate? {
        guard let selectedIndex = selectedIndex else { return nil }
        return getCandidate(globalIndex: selectedIndex)
    }

    func updateList(
        list: [HazkeyCandidate],
        pageSize: Int,
    ) {
        self.list = list
        self.pageSize = pageSize
        self.selectedIndex = nil
    }

    internal var listCount: Int {
        return list.count
    }
}
