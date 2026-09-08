import Foundation
import FileProvider

/// Opt-in device test. Uses the installed extension through the system's
/// replicated Files URL, and modifies only a freshly-created test directory.
@_cdecl("society_ios_files_integration_test")
public func societyIosFilesIntegrationTest() {
    DispatchQueue.global(qos: .utility).async {
        let fm = FileManager.default
        var checks: [String] = []
        var phase = "shared source"
        var report: [String: Any] = ["ok": false]
        func checked(_ condition: Bool, _ name: String) throws {
            guard condition else { throw NSError(domain: "SocietyFilesIntegration", code: 1,
                userInfo: [NSLocalizedDescriptionKey: name]) }
            checks.append(name)
        }
        func coordinate<T>(_ url: URL, write: Bool = false, _ work: (URL) throws -> T) throws -> T {
            var error: NSError?
            var result: Result<T, Error>?
            let action: (URL) -> Void = { location in result = Result { try work(location) } }
            let coordinator = NSFileCoordinator()
            if write { coordinator.coordinate(writingItemAt: url, options: .forMerging, error: &error, byAccessor: action) }
            else { coordinator.coordinate(readingItemAt: url, options: [], error: &error, byAccessor: action) }
            if let result = result { return try result.get() }
            throw error ?? CocoaError(.fileReadUnknown) as NSError
        }
        func waitFor(_ name: String, _ condition: () throws -> Bool) throws {
            let deadline = Date().addingTimeInterval(20)
            while Date() < deadline {
                if try condition() { try checked(true, name); return }
                Thread.sleep(forTimeInterval: 0.1)
            }
            try checked(false, name)
        }
        do {
            let location = try SharedDriveLocation.current()
            let root = try location.existingRoot()
            let catalog = try JSONDecoder().decode([DriveSection].self,
                from: Data(contentsOf: Bundle.main.url(forResource: "Sections", withExtension: "json")!))
            let source = try LocalDriveStore(root: root, catalog: catalog)
            let filesRoot = source.root.appendingPathComponent("Files", isDirectory: true)
            phase = "source root snapshot"
            try checked(try source.item("section:files").directory, "source root is Files directory")
            let semaphore = DispatchSemaphore(value: 0)
            var domains: [NSFileProviderDomain] = []
            var requestError: Error?
            NSFileProviderManager.getDomainsWithCompletionHandler { value, error in
                domains = value; requestError = error; semaphore.signal()
            }
            phase = "registered domain"
            guard semaphore.wait(timeout: .now() + 20) == .success else { throw CocoaError(.fileReadUnknown) }
            if let error = requestError { throw error }
            guard let domain = domains.first(where: { $0.identifier.rawValue == source.manifest.identifier }),
                  let manager = NSFileProviderManager(for: domain) else { throw NSFileProviderError(.providerNotFound) }
            try checked(domain.isReplicated, "registered domain is replicated")
            try checked(domain.userEnabled, "registered domain is enabled")
            var visible: URL?
            manager.getUserVisibleURL(for: .rootContainer) { url, error in
                visible = url; requestError = error; semaphore.signal()
            }
            phase = "system Files root URL"
            guard semaphore.wait(timeout: .now() + 20) == .success else { throw CocoaError(.fileReadUnknown) }
            if let error = requestError { throw error }
            guard let visible = visible else { throw NSFileProviderError(.noSuchItem) }
            guard visible.startAccessingSecurityScopedResource() else { throw CocoaError(.fileReadNoPermission) }
            defer { visible.stopAccessingSecurityScopedResource() }
            phase = "system Files root enumeration"
            let names = try coordinate(visible) { try fm.contentsOfDirectory(atPath: $0.path) }
            let expected = Set(try source.children("section:files").map { $0.name })
            try checked(Set(names.filter { !$0.hasPrefix(".") }) == Set(expected.filter { !$0.hasPrefix(".") }),
                        "system root exposes only Files children")

            // Explicit recovery after a disconnected device interrupted a run.
            // Never infer cleanup targets by scanning the user's directory.
            if let previous = ProcessInfo.processInfo.environment["SOCIETY_FILES_TEST_CLEANUP"] {
                let prefix = "Society Files Test "
                guard previous.hasPrefix(prefix), UUID(uuidString: String(previous.dropFirst(prefix.count))) != nil else {
                    throw CocoaError(.fileWriteInvalidFileName)
                }
                phase = "interrupted test cleanup"
                let leftover = visible.appendingPathComponent(previous, isDirectory: true)
                try coordinate(leftover, write: true) { url in
                    let values = try url.resourceValues(forKeys: [.isDirectoryKey, .isSymbolicLinkKey])
                    guard values.isDirectory == true, values.isSymbolicLink != true,
                          try fm.contentsOfDirectory(atPath: url.path).isEmpty else { throw CocoaError(.fileWriteNoPermission) }
                    try fm.removeItem(at: url)
                }
                try waitFor("interrupted empty test folder removed") { !fm.fileExists(atPath: filesRoot.appendingPathComponent(previous).path) }
            }

            let name = "Society Files Test " + UUID().uuidString
            let folder = visible.appendingPathComponent(name, isDirectory: true)
            let backing = filesRoot.appendingPathComponent(name, isDirectory: true)
            phase = "system folder creation"
            try coordinate(folder, write: true) { try fm.createDirectory(at: $0, withIntermediateDirectories: false) }
            try waitFor("system folder appears in source Files") { fm.fileExists(atPath: backing.path) }
            let file = folder.appendingPathComponent("검증.txt")
            let bytes = Data("Society Files verified\n".utf8)
            phase = "system file write"
            try coordinate(file, write: true) { try bytes.write(to: $0) }
            try waitFor("system file bytes reach source") { (try? Data(contentsOf: backing.appendingPathComponent("검증.txt"))) == bytes }
            try checked(try coordinate(file) { try Data(contentsOf: $0) } == bytes, "system file read")
            phase = "system file rename"
            let renamed = folder.appendingPathComponent("renamed.txt")
            try coordinate(folder, write: true) { _ in try fm.moveItem(at: file, to: renamed) }
            try waitFor("system rename reaches source") { fm.fileExists(atPath: backing.appendingPathComponent("renamed.txt").path) }
            phase = "system file edit"
            let edited = Data("Society Files edited\n".utf8)
            try coordinate(renamed, write: true) { try edited.write(to: $0) }
            try waitFor("system edits reach source") { (try? Data(contentsOf: backing.appendingPathComponent("renamed.txt"))) == edited }
            phase = "system file deletion"
            try coordinate(folder, write: true) { _ in try fm.removeItem(at: renamed) }
            try waitFor("system deletion reaches source") { !fm.fileExists(atPath: backing.appendingPathComponent("renamed.txt").path) }
            phase = "system folder deletion"
            try coordinate(folder, write: true) { try fm.removeItem(at: $0) }
            try waitFor("system folder deletion reaches source") { !fm.fileExists(atPath: backing.path) }
            try checked(source.manifest.sections.count == 8, "eight private source areas preserved")
            report["ok"] = true
            // Exercise the app's existing Open in Files action after the file
            // operations finish, so its public root can also be inspected.
            "path".withCString { action in
                root.path.withCString { path in
                    source.manifest.identifier.withCString { identifier in
                        societyIosDriveRequest(action, path, identifier, nil) { _, result in
                            if let result = result { print("SOCIETY_FILES_PRESENTATION " + String(cString: result)) }
                        }
                    }
                }
            }
        } catch {
            let error = error as NSError
            report["error"] = error.description
        }
        report["phase"] = phase
        report["checks"] = checks
        let data = try! JSONSerialization.data(withJSONObject: report, options: [.sortedKeys, .prettyPrinted])
        let output = fm.urls(for: .documentDirectory, in: .userDomainMask)[0]
        try? fm.createDirectory(at: output, withIntermediateDirectories: true)
        try? data.write(to: output.appendingPathComponent("ios-files-integration.json"), options: .atomic)
        print("SOCIETY_FILES_INTEGRATION " + String(decoding: data, as: UTF8.self))
    }
}
