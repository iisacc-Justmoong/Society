import XCTest

/// Runs against the installed Release app. Never signs out, deletes library
/// assets, changes the container, or replaces the application's data.
final class InteractionsTests: XCTestCase {
    let society = XCUIApplication(bundleIdentifier: "com.iisacc.society")

    override func setUpWithError() throws {
        continueAfterFailure = false
        if name.contains("Dreamscapes") { return }
        if name.contains("Models") && society.state != .notRunning && society.state != .unknown {
            society.activate() // Keep an attached device console and pending SDK transfer alive.
        } else {
            society.launch()
        }
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

    // The host-side fixtures are valid tiny Safetensors files. The surrounding
    // device audit checks that neither payload arrives until its card is tapped.
    private let selectedModel = "000-society-ui-final-20260917.safetensors"
    private func tapTab(_ label: String) {
        let tab = society.descendants(matching: .any).matching(NSPredicate(format: "label == %@", label)).firstMatch
        XCTAssertTrue(tab.waitForExistence(timeout: 5)); tab.tap()
    }
    private func showModels() {
        let started = Date()
        tapTab("Storage")
        let models = society.buttons["Models"].firstMatch
        let card = society.descendants(matching: .any).matching(
            NSPredicate(format: "label == %@", selectedModel)).firstMatch
        if !card.exists {
            XCTAssertTrue(models.waitForExistence(timeout: 5)); models.tap()
        }
        XCTAssertTrue(card.waitForExistence(timeout: 5), "Cached model identity must be available promptly")
        let elapsed = Date().timeIntervalSince(started)
        let timing = XCTAttachment(string: "Storage/Models navigation to accessible model card: \(elapsed) seconds (includes XCTest synchronization)")
        timing.name = "Models navigation timing"; timing.lifetime = .keepAlways; add(timing)
        XCTAssertLessThan(elapsed, 8, "Models navigation must not stall the main thread")
    }

    func testModelsShowsMetadataAndRemainsResponsive() {
        showModels()
        capture("Models metadata before any download")
        tapTab("Dashboard")
        XCTAssertTrue(society.buttons["Generate"].firstMatch.waitForExistence(timeout: 5))
        showModels()
        capture("Models metadata after repeated navigation")
    }

    func testModelsBrowseInstalledLibrary() {
        tapTab("Storage")
        let models = society.buttons["Models"].firstMatch
        XCTAssertTrue(models.waitForExistence(timeout: 5)); models.tap()
        let card = society.buttons.matching(NSPredicate(
            format: "identifier CONTAINS '.modelCardimage' AND NOT (identifier ENDSWITH '.card_menu')")).firstMatch
        XCTAssertTrue(card.waitForExistence(timeout: 5))
        capture("Installed Models library")
    }

    func testModelsDownloadsOnlyTheTappedCard() {
        showModels()
        let card = society.descendants(matching: .any).matching(
            NSPredicate(format: "label == %@", selectedModel)).firstMatch
        if !card.isHittable {
            let cards = society.descendants(matching: .any).matching(
                NSPredicate(format: "identifier ENDSWITH '.modelCardsimage'")).firstMatch
            cards.swipeLeft()
        }
        XCTAssertTrue(card.isHittable); card.tap()
        capture("Models selected download")
        // Qt exposes the card as one AX element; its footnote is visual text.
        // The device audit waits for the durable SDK request and verifies bytes.
        tapTab("Dashboard")
        XCTAssertTrue(society.buttons["Generate"].firstMatch.waitForExistence(timeout: 5))
        showModels()
        capture("Models remains responsive after selected download request")
    }

    // Run only against a populated device: the real sync/generation must have
    // started. A retained card is presentation evidence, not background compute.
    func testSocietyLiveActivitySurvivesHostTermination() {
        society.descendants(matching: .any).matching(NSPredicate(format: "label == 'Storage'")).firstMatch.tap()
        let photos = society.buttons["Photos"].firstMatch
        XCTAssertTrue(photos.waitForExistence(timeout: 10)); photos.tap()
        verifyPersistentActivity(society, title: "Society")
    }

    func testDreamscapesLiveActivitySurvivesHostTermination() {
        let app = XCUIApplication(bundleIdentifier: "com.iisacc.dreamscapes")
        app.launch()
        XCTAssertTrue(app.wait(for: .runningForeground, timeout: 20))
        let model = app.buttons.matching(NSPredicate(format: "identifier ENDSWITH '.societyModelSelector'")).firstMatch
        XCTAssertTrue(model.waitForExistence(timeout: 15)); model.tap()
        let menu = XCTAttachment(string: app.debugDescription)
        menu.name = "Dreamscapes model menu"; menu.lifetime = .keepAlways; add(menu)
        let menuImage = XCTAttachment(screenshot: XCUIScreen.main.screenshot())
        menuImage.name = "Dreamscapes model choices"; menuImage.lifetime = .keepAlways; add(menuImage)
        let checkpoint = app.descendants(matching: .any).matching(NSPredicate(format: "label CONTAINS %@", "redLilyIllu_v10")).firstMatch
        if checkpoint.exists { checkpoint.tap() }
        else {
            // The installed Qt ContextMenu omits its rows from AX. The verified
            // device catalog has two Anima entries, then redLily (third row).
            model.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 1))
                .withOffset(CGVector(dx: 0, dy: 2 + 8 + 2 * (18 + 2) + 9)).tap()
        }
        XCTAssertTrue(model.label.contains("redLilyIllu_v10"))
        let prompt = app.descendants(matching: .any).matching(NSPredicate(format: "identifier ENDSWITH '.promptField'")).firstMatch
        XCTAssertTrue(prompt.waitForExistence(timeout: 15))
        prompt.tap()
        // Qt exposes this focused editor as Other, so XCTest's typeText focus
        // check fails. Exercise the actual visible keyboard instead.
        for key in ["l", "a", "k", "e"] { app.keys[key].tap() }
        app.buttons["Generate"].firstMatch.tap()
        verifyPersistentActivity(app, title: "Dreamscapes")
    }

    private func verifyPersistentActivity(_ app: XCUIApplication, title: String) {
        XCUIDevice.shared.press(.home)
        let system = XCUIApplication(bundleIdentifier: "com.apple.springboard")
        let screen = system.windows.firstMatch
        screen.coordinate(withNormalizedOffset: CGVector(dx: 0.15, dy: 0))
            .press(forDuration: 0.05, thenDragTo: screen.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 0.8)))
        let updated = system.staticTexts.matching(NSPredicate(format: "label CONTAINS %@", "Last update")).firstMatch
        let appeared = updated.waitForExistence(timeout: 20)
        let before = XCTAttachment(screenshot: XCUIScreen.main.screenshot())
        before.name = title + " ActivityKit before host termination"; before.lifetime = .keepAlways; add(before)
        let beforeTree = XCTAttachment(string: system.debugDescription)
        beforeTree.name = title + " before termination hierarchy"; beforeTree.lifetime = .keepAlways; add(beforeTree)
        XCTAssertTrue(appeared, "Expected the independent ActivityKit card")
        XCTAssertTrue(system.staticTexts[title].firstMatch.exists)
        // An already synchronized library can finish before leaving Society.
        // Its completed card must also outlive the containing process.
        let completedSync = title == "Society" && system.staticTexts["Completed"].firstMatch.exists
        let permission = system.staticTexts["Allow Live Activities from \(title)?"].firstMatch
        if permission.exists {
            let allow = system.buttons.matching(identifier: "Allow").allElementsBoundByIndex
                .filter { $0.frame.minY > permission.frame.maxY }
                .min { $0.frame.minY < $1.frame.minY }
            XCTAssertNotNil(allow); allow?.tap()
        }
        app.terminate()
        XCTAssertTrue(app.wait(for: .notRunning, timeout: 15))
        let delay = expectation(description: "Retain ActivityKit after host termination")
        DispatchQueue.main.asyncAfter(deadline: .now() + 100) { delay.fulfill() }
        wait(for: [delay], timeout: 105)
        XCTAssertEqual(app.state, .notRunning)
        XCTAssertTrue(updated.exists)
        // The OS schedules the stale presentation; its deadline is not an
        // exact UI refresh timer, especially while the host is terminated.
        if completedSync {
            XCTAssertTrue(system.staticTexts["Completed"].firstMatch.exists)
        } else {
            XCTAssertTrue(system.staticTexts["Open the app to check progress"].firstMatch.waitForExistence(timeout: 45))
        }
        XCTAssertTrue(system.staticTexts[title].firstMatch.exists)
        let after = XCTAttachment(screenshot: XCUIScreen.main.screenshot())
        after.name = title + " ActivityKit 100 seconds after host termination"; after.lifetime = .keepAlways; add(after)
        let tree = XCTAttachment(string: system.debugDescription)
        tree.name = title + " independent ActivityKit hierarchy"; tree.lifetime = .keepAlways; add(tree)
        app.activate()
    }

    func testDashboardUsesLogicalControlSizesInBothOrientations() {
        defer { XCUIDevice.shared.orientation = .portrait }
        for orientation in [UIDeviceOrientation.portrait, .landscapeRight] {
            XCUIDevice.shared.orientation = orientation
            let landscape = orientation.isLandscape
            let rotated = XCTNSPredicateExpectation(predicate: NSPredicate { _, _ in
                let frame = self.society.windows.firstMatch.frame
                return (frame.width > frame.height) == landscape
            }, object: society)
            XCTAssertEqual(XCTWaiter.wait(for: [rotated], timeout: 10), .completed)
            capture(landscape ? "Dashboard landscape size" : "Dashboard portrait size")
            let navigation = society.descendants(matching: .any).matching(
                NSPredicate(format: "identifier ENDSWITH %@", ".mobileNavigationToggle")).firstMatch
            let compact = navigation.exists && navigation.isHittable
            let toolbarIcons = compact
                ? ["mobileNavigationToggle", "mobileSearchToggle", "mobileAccount"]
                : ["dashboardAccount"]
            let toolbarControls = compact ? toolbarIcons
                : ["dashboardTab", "toolsTab", "storageTab", "browseTab", "environmentTab",
                   "dashboardSearch"] + toolbarIcons
            for name in ["promptField", "mediaTypeButton", "aspectRatioButton",
                         "generationCountButton", "generateButton"] + toolbarControls {
                let control = society.descendants(matching: .any).matching(
                    NSPredicate(format: "identifier ENDSWITH %@", "." + name)).firstMatch
                XCTAssertTrue(control.waitForExistence(timeout: 10), name)
                XCTAssertEqual(control.frame.height, 22, accuracy: 0.5, name)
                if toolbarIcons.contains(name) {
                    XCTAssertEqual(control.frame.width, 22, accuracy: 0.5, name)
                }
                XCTAssertGreaterThanOrEqual(control.frame.minX, society.windows.firstMatch.frame.minX, name)
                XCTAssertLessThanOrEqual(control.frame.maxX, society.windows.firstMatch.frame.maxX, name)
                XCTAssertTrue(control.isHittable, name)
            }
        }
    }

    func testDashboardMenusKeepTheirSelectionAtLogicalSize() {
        XCUIDevice.shared.orientation = .portrait
        let count = society.buttons["Image count: 1"].firstMatch
        XCTAssertTrue(count.waitForExistence(timeout: 10)); count.tap()
        let two = society.buttons["2 images"].firstMatch
        XCTAssertTrue(two.waitForExistence(timeout: 5)); two.tap()
        let countClosed = XCTNSPredicateExpectation(predicate: NSPredicate(format: "exists == false"), object: two)
        XCTAssertEqual(XCTWaiter.wait(for: [countClosed], timeout: 5), .completed)
        let aspect = society.buttons["Aspect ratio: 1:1"].firstMatch
        aspect.tap()
        capture("Dashboard aspect ratio menu")
        // Qt's dynamic ContextMenu rows are visible but absent from the iOS AX tree.
        // Tap row 2 using the LVRS gap (2), padding (8), row (18), and spacing (2).
        aspect.coordinate(withNormalizedOffset: CGVector(dx: 0.5, dy: 1))
            .withOffset(CGVector(dx: 0, dy: 2 + 8 + 18 + 2 + 9)).tap()
        XCTAssertTrue(society.buttons["Aspect ratio: 4:3"].firstMatch.waitForExistence(timeout: 5))
        society.buttons["Tools"].firstMatch.tap()
        society.buttons["Dashboard"].firstMatch.tap()
        XCTAssertTrue(society.buttons["Image count: 2"].firstMatch.isHittable)
        XCTAssertTrue(society.buttons["Aspect ratio: 4:3"].firstMatch.isHittable)
        capture("Dashboard selections after touch and tab navigation")
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
        let mergeTool = society.buttons["Model merge"].firstMatch
        XCTAssertTrue(mergeTool.waitForExistence(timeout: 10))
        capture("Tools on mobile")
        mergeTool.tap()
        XCTAssertTrue(society.buttons["Weighted sum"].firstMatch.waitForExistence(timeout: 10))
        society.buttons["All tools"].firstMatch.tap()
        XCTAssertTrue(mergeTool.isHittable)
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
