// minedict - BSD 3-Clause License; see README.
import SwiftUI

/// The search field and the headwords it matches.
struct SidebarView: View {
    @EnvironmentObject private var library: Library
    var searchFocused: FocusState<Bool>.Binding

    var body: some View {
        VStack(spacing: 0) {
            searchField
                .padding(.horizontal, 10)
                .padding(.vertical, 8)
            List(library.matches, id: \.self, selection: selection) { word in
                Text(word).lineLimit(1)
            }
            .listStyle(.sidebar)
            .overlay {
                if library.matches.isEmpty && !library.query.trimmingCharacters(in: .whitespaces).isEmpty {
                    Text("No headwords match").foregroundStyle(.tertiary)
                }
            }
            Divider()
            footer
        }
    }

    private var searchField: some View {
        HStack(spacing: 6) {
            Image(systemName: "magnifyingglass").foregroundStyle(.secondary)
            TextField("Search headwords", text: $library.query)
                .textFieldStyle(.plain)
                .focused(searchFocused)
                .onSubmit { library.submitQuery() }
            if !library.query.isEmpty {
                Button { library.query = "" } label: {
                    Image(systemName: "xmark.circle.fill").foregroundStyle(.secondary)
                }
                .buttonStyle(.plain)
            }
        }
        .padding(.horizontal, 8)
        .padding(.vertical, 6)
        .background(.quaternary.opacity(0.6), in: RoundedRectangle(cornerRadius: 7, style: .continuous))
    }

    private var footer: some View {
        HStack(spacing: 6) {
            if library.isLoading {
                ProgressView().controlSize(.small)
                Text("Opening dictionaries…")
            } else {
                Text(summary)
            }
            Spacer()
        }
        .font(.caption)
        .foregroundStyle(.secondary)
        .padding(.horizontal, 12)
        .padding(.vertical, 6)
    }

    private var summary: String {
        let words = library.dictionaries.filter { !$0.isResourceFile }.count
        switch words {
        case 0: return "No dictionaries"
        case 1: return "1 dictionary"
        default: return "\(words) dictionaries"
        }
    }

    /// Picking a headword shows it; the list follows the word on display.
    private var selection: Binding<String?> {
        Binding(get: { library.current }, set: { word in
            if let word { library.show(word) }
        })
    }
}
