// minedict - BSD 3-Clause License; see README.
import Foundation

/// The HTML document the web view shows for one dictionary's entries. Its
/// base URL names the dictionary, so relative resources resolve through the
/// `minedict://` scheme handler.
struct EntryPage: Equatable {
    let dictionaryID: String
    let html: String
    let baseURL: URL

    init(hit: Hit, appearance: EntryAppearance) {
        dictionaryID = hit.dictionary.identifier
        baseURL = URL(string: "minedict://dict/\(hit.dictionary.identifier)/")!
        var page = "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
        page += "<style>\(Self.baseStyle(for: appearance))</style>"
        for name in hit.dictionary.stylesheetNames {
            let href = name.addingPercentEncoding(withAllowedCharacters: .urlPathAllowed) ?? name
            page += "<link rel=\"stylesheet\" href=\"\(href)\">"
        }
        page += "</head><body>"
        for entry in hit.entries {
            page += "<article class=\"minedict-entry\">"
            if !entry.via.isEmpty {
                let chain = (entry.via + [entry.headword]).map(Self.escape).joined(separator: " → ")
                page += "<p class=\"minedict-via\">↳ \(chain)</p>"
            }
            page += entry.html
            page += "</article>"
        }
        page += "</body></html>"
        html = page
    }

    static func escape(_ text: String) -> String {
        text.replacingOccurrences(of: "&", with: "&amp;")
            .replacingOccurrences(of: "<", with: "&lt;")
            .replacingOccurrences(of: ">", with: "&gt;")
    }

    /// Typography and canvas; the dictionary's own stylesheet comes after and wins.
    static func baseStyle(for appearance: EntryAppearance) -> String {
        let light = "body { background: #ffffff; color: #1d1d1f; }"
        let dark = "body { background: #282a36; color: #f8f8f2; }"
        let scheme: String
        let canvas: String
        switch appearance {
        case .light:
            scheme = "light"
            canvas = light
        case .dark:
            scheme = "dark"
            canvas = dark
        case .system:
            scheme = "light dark"
            canvas = "\(light) @media (prefers-color-scheme: dark) { \(dark) }"
        }
        return """
        :root { color-scheme: \(scheme); }
        html { font: 15px/1.5 -apple-system, system-ui, "PingFang SC", "Hiragino Sans GB", sans-serif; }
        body { margin: 0; padding: 14px 20px 32px; overflow-wrap: break-word; }
        \(canvas)
        img { max-width: 100%; height: auto; }
        .minedict-entry + .minedict-entry { margin-top: 1.4em; padding-top: 1.2em; border-top: 1px solid rgba(128, 128, 128, 0.35); }
        .minedict-via { margin: 0 0 0.6em; font-size: 0.85em; opacity: 0.65; }
        """
    }
}
