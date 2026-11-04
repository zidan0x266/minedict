// minedict - BSD 3-Clause License; see README.
import SwiftUI

@main
struct MinedictApp: App {
    @StateObject private var library = Library()
    @AppStorage(EntryAppearance.key) private var appearance = EntryAppearance.standard
    @AppStorage(PageZoom.key) private var zoom = PageZoom.normal

    var body: some Scene {
        WindowGroup("Minedict") {
            ContentView()
                .environmentObject(library)
                .frame(minWidth: 720, minHeight: 440)
        }
        .defaultSize(width: 1080, height: 720)
        .commands {
            CommandGroup(replacing: .newItem) {
                Button("Open Dictionary…") { library.presentOpenPanel() }
                    .keyboardShortcut("o")
            }
            CommandGroup(after: .pasteboard) {
                Divider()
                Button("Find Headword") { library.requestSearchFocus() }
                    .keyboardShortcut("f")
            }
            CommandMenu("Go") {
                Button("Back") { library.goBack() }
                    .keyboardShortcut("[")
                    .disabled(!library.canGoBack)
                Button("Forward") { library.goForward() }
                    .keyboardShortcut("]")
                    .disabled(!library.canGoForward)
            }
            CommandGroup(after: .toolbar) {
                Picker("Entry Appearance", selection: $appearance) {
                    ForEach(EntryAppearance.allCases) { Text($0.title).tag($0) }
                }
                Toggle("Match Anywhere in Headwords", isOn: $library.matchAnywhere)
                Divider()
                Button("Bigger Text") { zoom = PageZoom.bigger(zoom) }
                    .keyboardShortcut("=")
                Button("Smaller Text") { zoom = PageZoom.smaller(zoom) }
                    .keyboardShortcut("-")
                Button("Actual Size") { zoom = PageZoom.normal }
                    .keyboardShortcut("0")
            }
        }
    }
}
