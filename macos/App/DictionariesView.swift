// minedict - BSD 3-Clause License; see README.
import SwiftUI

/// The open dictionaries, what each file says about itself, and the sources they came from.
final class DictionarySelection: ObservableObject {
    @Published var identifier: String?
}

struct DictionariesView: View {
    @EnvironmentObject private var library: Library
    @Environment(\.dismiss) private var dismiss
    @StateObject private var selection = DictionarySelection()

    var body: some View {
        VStack(spacing: 0) {
            HSplitView {
                sidebar.frame(minWidth: 260, idealWidth: 300)
                details.frame(minWidth: 320, maxWidth: .infinity, maxHeight: .infinity)
            }
            Divider()
            footer
        }
        .frame(minWidth: 720, minHeight: 440)
        .onAppear { if selection.identifier == nil { selection.identifier = library.dictionaries.first?.identifier } }
    }

    private var sidebar: some View {
        List(selection: $selection.identifier) {
            Section("Dictionaries") {
                ForEach(library.dictionaries) { dictionary in
                    VStack(alignment: .leading, spacing: 2) {
                        Text(dictionary.title)
                        Text(dictionary.path)
                            .font(.caption)
                            .foregroundStyle(.secondary)
                            .lineLimit(1)
                            .truncationMode(.middle)
                    }
                    .tag(dictionary.identifier)
                }
            }
            Section("Sources") {
                ForEach(library.sources, id: \.self) { source in
                    HStack {
                        Text(source).lineLimit(1).truncationMode(.middle)
                        Spacer()
                        Button { library.remove(source: source) } label: {
                            Image(systemName: "minus.circle")
                        }
                        .buttonStyle(.plain)
                        .help("Remove this source")
                    }
                }
            }
        }
    }

    @ViewBuilder private var details: some View {
        if let dictionary = library.dictionaries.first(where: { $0.identifier == selection.identifier }) {
            ScrollView {
                Grid(alignment: .leadingFirstTextBaseline, horizontalSpacing: 14, verticalSpacing: 8) {
                    ForEach(Array(dictionary.infoRows.enumerated()), id: \.offset) { _, row in
                        GridRow {
                            Text(row[0])
                                .foregroundStyle(.secondary)
                                .gridColumnAlignment(.trailing)
                            Text(row[1])
                                .textSelection(.enabled)
                                .frame(maxWidth: .infinity, alignment: .leading)
                        }
                    }
                }
                .padding(20)
            }
        } else {
            ContentUnavailableView("No dictionary selected", systemImage: "book.closed")
        }
    }

    private var footer: some View {
        HStack {
            if !library.problems.isEmpty {
                Label("\(library.problems.count) file(s) could not be opened", systemImage: "exclamationmark.triangle")
                    .foregroundStyle(.orange)
                    .help(library.problems.joined(separator: "\n"))
            }
            Spacer()
            Button("Add…") { library.presentOpenPanel() }
            Button("Done") { dismiss() }.keyboardShortcut(.defaultAction)
        }
        .padding(12)
    }
}
