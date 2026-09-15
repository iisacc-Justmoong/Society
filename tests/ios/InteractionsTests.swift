import XCTest

/// Runs against the installed Release app. Never signs out, deletes library
/// assets, changes the container, or replaces the application's data.
final class InteractionsTests: XCTestCase {
    let society = XCUIApplication(bundleIdentifier: "com.iisacc.society")

    override func setUpWithError() throws {
        continueAfterFailure = false
        society.launch()
        XCTAssertTrue(society.wait(for: .runningForeground, timeout: 20))
        let access = society.buttons["Allow Full Access"].firstMatch
        if access.waitForExistence(timeout: 3) {
            access.tap()
        } else {
            let system = XCUIApplication(bundleIdentifier: "com.apple.springboard")
            let request = system.alerts.matching(NSPredicate(format: "label CONTAINS[c] %@", "Society")).firstMatch
            if request.exists && request.buttons["Allow Full Access"].exists {
                request.buttons["Allow Full Access"].tap()
            }
        }
    }

    func capture(_ name: String) {
        let image = XCTAttachment(screenshot: XCUIScreen.main.screenshot())
        image.name = name; image.lifetime = .keepAlways; add(image)
        let hierarchy = XCTAttachment(string: society.debugDescription)
        hierarchy.name = name + " hierarchy"; hierarchy.lifetime = .keepAlways; add(hierarchy)
    }

    func testInspectInstalledApp() {
        capture("installed Society")
    }

    func testLeftEdgeReturnsToTheParentStorageFolder() {
        society.buttons["Storage"].firstMatch.tap()
        let files = society.buttons["Files"].firstMatch
        XCTAssertTrue(files.waitForExistence(timeout: 15))
        files.tap()
        let up = society.buttons.matching(NSPredicate(format: "identifier ENDSWITH '.driveUp'")).firstMatch
        let ready = XCTNSPredicateExpectation(predicate: NSPredicate(format: "enabled == true"), object: up)
        XCTAssertEqual(XCTWaiter.wait(for: [ready], timeout: 10), .completed)
        capture("Files before edge back")
        let window = society.windows.firstMatch
        let start = window.coordinate(withNormalizedOffset: CGVector(dx: 0, dy: 0.45)).withOffset(CGVector(dx: 10, dy: 0))
        let end = start.withOffset(CGVector(dx: 180, dy: 6))
        start.press(forDuration: 0.05, thenDragTo: end)
        let returned = XCTNSPredicateExpectation(predicate: NSPredicate(format: "enabled == false"), object: up)
        XCTAssertEqual(XCTWaiter.wait(for: [returned], timeout: 5), .completed)
        XCTAssertTrue(society.buttons["Models"].firstMatch.exists)
        capture("parent restored by edge back")
    }

    func testPhotoGallerySurvivesBackgroundSynchronization() {
        society.buttons["Storage"].firstMatch.tap()
        let photos = society.buttons["Photos"].firstMatch
        XCTAssertTrue(photos.waitForExistence(timeout: 10)); photos.tap()
        let add = society.buttons.matching(NSPredicate(format: "identifier ENDSWITH '.addPhotos'")).firstMatch
        XCTAssertTrue(add.waitForExistence(timeout: 10))
        capture("native Photos gallery")
        XCUIDevice.shared.press(.home)
        XCTAssertTrue(society.wait(for: .runningBackground, timeout: 10))
        let delay = expectation(description: "Allow real background photo work")
        DispatchQueue.main.asyncAfter(deadline: .now() + 20) { delay.fulfill() }
        wait(for: [delay], timeout: 25)
        let home = XCTAttachment(screenshot: XCUIScreen.main.screenshot())
        home.name = "Society activity while backgrounded"; home.lifetime = .keepAlways; self.add(home)
        let system = XCUIApplication(bundleIdentifier: "com.apple.springboard")
        let hierarchy = XCTAttachment(string: system.debugDescription)
        hierarchy.name = "background Live Activity hierarchy"; hierarchy.lifetime = .keepAlways; self.add(hierarchy)
        society.activate()
        XCTAssertTrue(add.waitForExistence(timeout: 10)); capture("Photos after background work")
    }

    func testInspectDevicesAndPhotos() {
        let devices = society.buttons["Browse"].firstMatch
        XCTAssertTrue(devices.waitForExistence(timeout: 15)); devices.tap()
        let close = society.buttons["Close"].firstMatch
        XCTAssertTrue(close.waitForExistence(timeout: 10)); capture("devices and synchronization")
        close.tap()
        society.buttons["Storage"].firstMatch.tap()
        let files = society.buttons["Files"].firstMatch
        XCTAssertTrue(files.waitForExistence(timeout: 10)); files.tap()
        capture("Files folder")
    }

    func testDesktopViewsAdaptToMobileAndKeepPrompt() {
        let dashboard = society.buttons["Dashboard"].firstMatch
        XCTAssertTrue(dashboard.waitForExistence(timeout: 15))
        let prompt = society.textFields["Prompt"].firstMatch
        XCTAssertTrue(prompt.waitForExistence(timeout: 10))
        prompt.tap(); prompt.typeText("A quiet mobile landscape")
        capture("Dashboard with keyboard")
        society.buttons["Tools"].firstMatch.tap()
        XCTAssertTrue(society.staticTexts["Model merge"].firstMatch.waitForExistence(timeout: 10))
        capture("Tools on mobile")
        society.buttons["Storage"].firstMatch.tap()
        XCTAssertTrue(society.buttons["Photos"].firstMatch.waitForExistence(timeout: 10))
        dashboard.tap()
        XCTAssertEqual(prompt.value as? String, "A quiet mobile landscape")
        society.buttons["Environment"].firstMatch.tap()
        XCTAssertTrue(society.buttons["Done"].firstMatch.waitForExistence(timeout: 10))
        capture("Environment sheet")
        society.buttons["Done"].firstMatch.tap()
        XCUIDevice.shared.orientation = .landscapeLeft
        defer { XCUIDevice.shared.orientation = .portrait }
        XCTAssertTrue(society.buttons["Storage"].firstMatch.waitForExistence(timeout: 10))
        capture("Shared views in landscape")
    }
}
