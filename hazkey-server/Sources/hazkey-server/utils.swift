import Foundation

// log for debugging
public func debugLog(
    _ items: Any?, function: String = #function
) {
    #if DEBUG
        if let items = items {
            NSLog("\(function) : \(items)")
        } else {
            NSLog("\(function)")
        }
    #endif
}

extension Int {
    func positiveMod(_ m: Int) -> Int {
        let r = self % m
        return r < 0 ? r + m : r
    }
}