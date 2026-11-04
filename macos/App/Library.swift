// minedict - BSD 3-Clause License; see README.
//
// The app's state: the open dictionaries, the search, and the word on display.
import AppKit
import Foundation
import UniformTypeIdentifiers

/// The entries one dictionary has for a word.
struct Hit: Identifiable {
    let dictionary: MDDictionary
    let entries: [MDEntry]
    var id: String { dictionary.identifier }
}

extension MDDictionary: Identifiable {
    public var id: String { identifier }
}

@MainActor
final class Library: ObservableObject {
    @Published private(set) var shelf: MDShelf?
    @Published private(set) var dictionaries: [MDDictionary] = []
    @Published private(set) var problems: [String] = []
    @Published private(set) var isLoading = false
    /// The files and folders the shelf is built from, as the user chose them.
    @Published private(set) var sources: [String] = []

    @Published var query = "" {
        didSet { if query != oldValue { scheduleSearch() } }
    }
    @Published private(set) var matches: [String] = []
    /// The word whose entries are on display.
    @Published private(set) var current: String?
    @Published private(set) var history: [String] = []
    @Published private(set) var historyIndex = -1
    /// Bumped whenever a menu command asks the search field to take focus.
    @Published private(set) var searchFocusRequest = 0
    @Published var matchAnywhere = false {
        didSet {
            UserDefaults.standard.set(matchAnywhere, forKey: Keys.matchAnywhere)
            scheduleSearch(immediately: true)
        }
    }

    private enum Keys {
        static let sources = "dictionarySources"
        static let matchAnywhere = "matchAnywhere"
    }
    private var searchTask: Task<Void, Never>?
    private var loadGeneration = 0

    init() {
        matchAnywhere = UserDefaults.standard.bool(forKey: Keys.matchAnywhere)
        sources = UserDefaults.standard.stringArray(forKey: Keys.sources) ?? MDShelf.defaultSources()
        let arguments = CommandLine.arguments.dropFirst()
        // `Minedict --dictionary <path>` opens a file or folder for this launch only;
        // `--lookup <word>` shows a word right away.
        var extra: [String] = []
        var iterator = arguments.makeIterator()
        while let argument = iterator.next() {
            switch argument {
            case "--dictionary":
                if let path = iterator.next() { extra.append(path) }
            case "--lookup":
                if let word = iterator.next() { show(word) }
            default:
                break
            }
        }
        if !extra.isEmpty { sources += extra.filter { !sources.contains($0) } }
        reload()
    }

    // MARK: Sources

    func add(paths: [String]) {
        var updated = sources
        for path in paths where !updated.contains(path) { updated.append(path) }
        guard updated != sources else { return }
        setSources(updated)
    }

    func remove(source: String) {
        setSources(sources.filter { $0 != source })
    }

    private func setSources(_ updated: [String]) {
        sources = updated
        UserDefaults.standard.set(updated, forKey: Keys.sources)
        reload()
    }

    /// Open every source again off the main thread and swap the shelf in when done.
    func reload() {
        loadGeneration += 1
        let generation = loadGeneration
        let paths = sources
        isLoading = true
        Task.detached(priority: .userInitiated) {
            let shelf = MDShelf()
            var problems: [String] = []
            for path in paths {
                do {
                    try shelf.addPath(path)
                } catch {
                    problems.append(error.localizedDescription)
                }
            }
            problems.append(contentsOf: shelf.problems)
            let collected = problems
            await MainActor.run { [weak self] in
                guard let self, generation == self.loadGeneration else { return }
                self.shelf = shelf
                self.dictionaries = shelf.dictionaries
                self.problems = collected
                self.isLoading = false
                self.scheduleSearch(immediately: true)
            }
        }
    }

    // MARK: Search

    private func scheduleSearch(immediately: Bool = false) {
        searchTask?.cancel()
        let text = query.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let shelf, !text.isEmpty else {
            matches = []
            return
        }
        let anywhere = matchAnywhere
        searchTask = Task { [weak self] in
            if !immediately {
                try? await Task.sleep(for: .milliseconds(50))
                if Task.isCancelled { return }
            }
            let found = await Task.detached(priority: .userInitiated) { () -> [String] in
                anywhere ? shelf.headwordsContaining(text, limit: 300) : shelf.headwords(withPrefix: text, limit: 300)
            }.value
            if Task.isCancelled { return }
            self?.matches = found
        }
    }

    // MARK: Navigation

    /// Show a word's entries and remember it in the history.
    func show(_ word: String) {
        let trimmed = word.trimmingCharacters(in: .whitespacesAndNewlines)
        guard !trimmed.isEmpty else { return }
        if historyIndex < history.count - 1 { history.removeSubrange((historyIndex + 1)...) }
        if history.last != trimmed { history.append(trimmed) }
        historyIndex = history.count - 1
        current = trimmed
    }

    func submitQuery() { show(query) }

    var canGoBack: Bool { historyIndex > 0 }
    var canGoForward: Bool { historyIndex >= 0 && historyIndex < history.count - 1 }

    func goBack() {
        guard canGoBack else { return }
        historyIndex -= 1
        current = history[historyIndex]
    }

    func goForward() {
        guard canGoForward else { return }
        historyIndex += 1
        current = history[historyIndex]
    }

    func requestSearchFocus() { searchFocusRequest += 1 }

    // MARK: Opening files

    func presentOpenPanel() {
        let panel = NSOpenPanel()
        panel.canChooseFiles = true
        panel.canChooseDirectories = true
        panel.allowsMultipleSelection = true
        panel.allowedContentTypes = [UTType.folder] + ["mdx", "mdd"].compactMap { UTType(filenameExtension: $0) }
        panel.message = "Choose .mdx dictionaries, their .mdd resource files, or a folder holding them"
        panel.prompt = "Open"
        if panel.runModal() == .OK {
            add(paths: panel.urls.map { $0.path(percentEncoded: false) })
        }
    }
}
