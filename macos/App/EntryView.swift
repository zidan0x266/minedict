// minedict - BSD 3-Clause License; see README.
import SwiftUI

/// What the shelf has for one word, and which dictionary's entries are showing.
@MainActor
final class EntryModel: ObservableObject {
    @Published private(set) var hits: [Hit] = []
    @Published private(set) var suggestions: [String] = []
    @Published var chosen: String?
    @Published private(set) var looking = false

    var selectedHit: Hit? {
        hits.first { $0.id == chosen } ?? hits.first
    }

    func refresh(word: String?, shelf: MDShelf?) async {
        guard let word, let shelf else {
            hits = []
            suggestions = []
            return
        }
        looking = true
        let (found, near) = await Task.detached(priority: .userInitiated) { () -> ([Hit], [String]) in
            let found = shelf.lookup(word).map { Hit(dictionary: $0.dictionary, entries: $0.entries) }
            let near = found.isEmpty ? shelf.suggestions(forWord: word, limit: 12) : []
            return (found, near)
        }.value
        hits = found
        suggestions = near
        looking = false
        if !found.contains(where: { $0.id == chosen }) { chosen = found.first?.id }
    }
}

/// The entries for the current word: one dictionary at a time, in a web view.
struct EntryView: View {
    @EnvironmentObject private var library: Library
    @StateObject private var model = EntryModel()
    @AppStorage(EntryAppearance.key) private var appearance = EntryAppearance.standard
    @AppStorage(PageZoom.key) private var zoom = PageZoom.normal

    private struct Request: Equatable {
        var word: String?
        var shelf: ObjectIdentifier?
    }

    var body: some View {
        content
            .task(id: Request(word: library.current, shelf: library.shelf.map(ObjectIdentifier.init))) {
                await model.refresh(word: library.current, shelf: library.shelf)
            }
    }

    @ViewBuilder private var content: some View {
        if library.dictionaries.isEmpty && !library.isLoading {
            NoDictionariesView()
        } else if let word = library.current {
            if let hit = model.selectedHit {
                entryBody(for: hit)
            } else if model.looking {
                ProgressView()
            } else {
                NotFoundView(word: word, suggestions: model.suggestions)
            }
        } else {
            ContentUnavailableView(
                "Look up a word",
                systemImage: "character.book.closed",
                description: Text("Type in the search field, then pick a headword or press Return."))
        }
    }

    private func entryBody(for hit: Hit) -> some View {
        VStack(spacing: 0) {
            if model.hits.count > 1 {
                dictionaryPicker
                Divider()
            }
            EntryWebView(library: library, page: EntryPage(hit: hit, appearance: appearance), zoom: zoom)
        }
    }

    @ViewBuilder private var dictionaryPicker: some View {
        let binding = Binding(get: { model.selectedHit?.id ?? "" }, set: { model.chosen = $0 })
        if model.hits.count <= 4 {
            Picker("Dictionary", selection: binding) {
                ForEach(model.hits) { Text($0.dictionary.title).tag($0.id) }
            }
            .pickerStyle(.segmented)
            .labelsHidden()
            .padding(8)
        } else {
            Picker("Dictionary", selection: binding) {
                ForEach(model.hits) { Text($0.dictionary.title).tag($0.id) }
            }
            .pickerStyle(.menu)
            .frame(maxWidth: .infinity, alignment: .leading)
            .padding(8)
        }
    }
}

struct NoDictionariesView: View {
    @EnvironmentObject private var library: Library

    var body: some View {
        ContentUnavailableView {
            Label("No dictionaries open", systemImage: "books.vertical")
        } description: {
            Text("Open .mdx dictionaries with their .mdd resource files, or a folder holding them. Files dropped on the window are opened too.")
        } actions: {
            Button("Open Dictionary…") { library.presentOpenPanel() }
            if !library.problems.isEmpty {
                Text(library.problems.joined(separator: "\n"))
                    .font(.caption)
                    .foregroundStyle(.red)
                    .multilineTextAlignment(.center)
            }
        }
    }
}

struct NotFoundView: View {
    @EnvironmentObject private var library: Library
    let word: String
    let suggestions: [String]

    var body: some View {
        ContentUnavailableView {
            Label("No entry for “\(word)”", systemImage: "magnifyingglass")
        } description: {
            Text(suggestions.isEmpty ? "None of the open dictionaries has this headword." : "Did you mean:")
        } actions: {
            if !suggestions.isEmpty {
                LazyVGrid(columns: [GridItem(.adaptive(minimum: 140))], spacing: 6) {
                    ForEach(suggestions, id: \.self) { suggestion in
                        Button(suggestion) { library.show(suggestion) }.buttonStyle(.link)
                    }
                }
                .frame(maxWidth: 460)
            }
        }
    }
}
