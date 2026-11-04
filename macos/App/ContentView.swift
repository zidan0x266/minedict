// minedict - BSD 3-Clause License; see README.
import SwiftUI
import UniformTypeIdentifiers

/// Per-view state lives in small observable objects: the Swift shipped with
/// the Command Line Tools cannot expand SwiftUI's `@State` macro.
final class WindowState: ObservableObject {
    @Published var showingDictionaries = false
}

struct ContentView: View {
    @EnvironmentObject private var library: Library
    @StateObject private var window = WindowState()
    @FocusState private var searchFocused: Bool

    var body: some View {
        NavigationSplitView {
            SidebarView(searchFocused: $searchFocused)
                .navigationSplitViewColumnWidth(min: 200, ideal: 260, max: 420)
        } detail: {
            EntryView()
        }
        .navigationTitle(library.current ?? "Minedict")
        .toolbar {
            ToolbarItemGroup(placement: .navigation) {
                Button { library.goBack() } label: { Label("Back", systemImage: "chevron.left") }
                    .disabled(!library.canGoBack)
                    .help("Back")
                Button { library.goForward() } label: { Label("Forward", systemImage: "chevron.right") }
                    .disabled(!library.canGoForward)
                    .help("Forward")
            }
            ToolbarItem(placement: .primaryAction) {
                Button { window.showingDictionaries = true } label: { Label("Dictionaries", systemImage: "books.vertical") }
                    .help("The open dictionaries")
            }
        }
        .sheet(isPresented: $window.showingDictionaries) {
            DictionariesView().environmentObject(library)
        }
        .onChange(of: library.searchFocusRequest) { searchFocused = true }
        .onAppear { searchFocused = true }
        .onDrop(of: [.fileURL], isTargeted: nil, perform: handleDrop)
    }

    /// Files and folders dropped on the window join the shelf.
    private func handleDrop(_ providers: [NSItemProvider]) -> Bool {
        let files = providers.filter { $0.hasItemConformingToTypeIdentifier(UTType.fileURL.identifier) }
        guard !files.isEmpty else { return false }
        for provider in files {
            _ = provider.loadObject(ofClass: URL.self) { url, _ in
                guard let url else { return }
                Task { @MainActor in library.add(paths: [url.path(percentEncoded: false)]) }
            }
        }
        return true
    }
}
