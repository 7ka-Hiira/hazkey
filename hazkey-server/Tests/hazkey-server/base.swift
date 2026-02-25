import Foundation
import XCTest

@testable import hazkeyServer

class BaseHazkeyServerTestCase: XCTestCase {
  var client: HazkeyServerClient!

  override func setUpWithError() throws {
    try super.setUpWithError()

    // Ensure server is running
    XCTAssertTrue(
      TestUtilities.waitForServer(),
      "Server socket did not appear within timeout. Make sure the hazkey-server is running."
    )

    // Create and connect client
    client = HazkeyServerClient()
    try client.connect()

    // Initialize server state
    try initializeServerState()
  }

  override func tearDownWithError() throws {
    client?.disconnect()
    client = nil
    try super.tearDownWithError()
  }

  private func initializeServerState() throws {
    // Set default configuration
    let configQuery = QueryDataBuilder.setConfig()
    let configResponse = try client.sendQuery(configQuery)
    XCTAssertEqual(configResponse.status, .success, "Failed to set initial configuration")

    // Create composing text instance
    let instanceQuery = QueryDataBuilder.createComposingTextInstance()
    let instanceResponse = try client.sendQuery(instanceQuery)
    XCTAssertEqual(instanceResponse.status, .success, "Failed to create composing text instance")
  }

  // Helper method for sending queries with better error reporting
  func sendQuery(
    _ query: Hazkey_Commands_QueryData,
    file: StaticString = #file,
    line: UInt = #line
  ) throws -> Hazkey_Commands_ResultData {
    do {
      return try client.sendQuery(query)
    } catch {
      XCTFail("Failed to send query: \(error)", file: file, line: line)
      throw error
    }
  }
}
