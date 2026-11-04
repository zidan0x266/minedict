// minedict - BSD 3-Clause License; see README.
import AppKit
import SwiftUI
import WebKit

/// A web view showing an `EntryPage`. Links to other words stay inside the
/// app, `sound://` links play, and everything else opens in the browser.
struct EntryWebView: NSViewRepresentable {
    let library: Library
    let page: EntryPage
    let zoom: Double

    func makeCoordinator() -> Coordinator { Coordinator(library: library) }

    func makeNSView(context: Context) -> WKWebView {
        let configuration = WKWebViewConfiguration()
        let resources = ResourceSchemeHandler(library: library)
        configuration.setURLSchemeHandler(resources, forURLScheme: ResourceSchemeHandler.scheme)
        configuration.setURLSchemeHandler(resources, forURLScheme: ResourceSchemeHandler.soundScheme)
        configuration.preferences.setValue(true, forKey: "developerExtrasEnabled")
        let view = WKWebView(frame: .zero, configuration: configuration)
        view.navigationDelegate = context.coordinator
        view.uiDelegate = context.coordinator
        view.allowsMagnification = true
        view.setValue(false, forKey: "drawsBackground")
        return view
    }

    func updateNSView(_ view: WKWebView, context: Context) {
        let coordinator = context.coordinator
        if coordinator.page != page {
            coordinator.page = page
            view.loadHTMLString(page.html, baseURL: page.baseURL)
        }
        if abs(view.pageZoom - zoom) > 0.001 { view.pageZoom = zoom }
    }

    final class Coordinator: NSObject, WKNavigationDelegate, WKUIDelegate {
        let library: Library
        var page: EntryPage?

        init(library: Library) { self.library = library }

        func webView(_ webView: WKWebView, decidePolicyFor navigationAction: WKNavigationAction,
                     decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
            decisionHandler(handle(navigationAction.request.url) ? .cancel : .allow)
        }

        func webView(_ webView: WKWebView, createWebViewWith configuration: WKWebViewConfiguration,
                     for navigationAction: WKNavigationAction, windowFeatures: WKWindowFeatures) -> WKWebView? {
            _ = handle(navigationAction.request.url)  // links that ask for a new window
            return nil
        }

        /// Acts on a link; returns true when the web view must not follow it.
        private func handle(_ url: URL?) -> Bool {
            guard let url, let scheme = url.scheme?.lowercased() else { return false }
            switch scheme {
            case "about":
                return false
            case ResourceSchemeHandler.scheme:
                guard let page else { return false }
                let base = page.baseURL.absoluteString
                if url.absoluteString == base { return false }  // the page itself
                if url.fragment != nil, url.absoluteString.hasPrefix(base + "#") { return false }  // an anchor in it
                show(url.lastPathComponent.removingPercentEncoding ?? url.lastPathComponent)
                return true
            case "entry", "bword", "d", "x", "xi", "xid", "xp":  // cross-references to other words
                show(Self.target(of: url, scheme: scheme))
                return true
            case ResourceSchemeHandler.soundScheme:
                play(Self.target(of: url, scheme: scheme))
                return true
            case "http", "https", "mailto":
                NSWorkspace.shared.open(url)
                return true
            default:
                return true
            }
        }

        /// `entry://ice%20cream#2` gives "ice cream".
        private static func target(of url: URL, scheme: String) -> String {
            var text = String(url.absoluteString.dropFirst(scheme.count + 1))
            if text.hasPrefix("//") { text.removeFirst(2) }
            if let hash = text.firstIndex(of: "#") { text = String(text[..<hash]) }
            while text.hasSuffix("/") { text.removeLast() }
            return text.removingPercentEncoding ?? text
        }

        private func show(_ word: String) {
            let trimmed = word.trimmingCharacters(in: .whitespacesAndNewlines)
            guard !trimmed.isEmpty else { return }
            let library = self.library
            Task { @MainActor in library.show(trimmed) }
        }

        private func play(_ name: String) {
            guard !name.isEmpty, let page else { return }
            let library = self.library
            Task { @MainActor in
                guard let shelf = library.shelf,
                      let dictionary = shelf.dictionary(withIdentifier: page.dictionaryID) else { return }
                let data = await Task.detached { () -> Data? in
                    var mime: NSString?
                    return shelf.resourceNamed(name, for: dictionary, mimeType: &mime)
                }.value
                if let data { SoundPlayer.shared.play(data, name: name) }
            }
        }
    }
}

/// Serves `minedict://dict/<id>/<path>` and `sound://<path>` from the
/// dictionary's resource files and the files beside it.
final class ResourceSchemeHandler: NSObject, WKURLSchemeHandler {
    static let scheme = "minedict"
    static let soundScheme = "sound"

    private struct Found {
        let data: Data
        let mimeType: String
    }

    private let library: Library
    private let queue = DispatchQueue(label: "minedict.resources", qos: .userInitiated, attributes: .concurrent)
    private let lock = NSLock()
    private var active = Set<ObjectIdentifier>()

    init(library: Library) { self.library = library }

    func webView(_ webView: WKWebView, start task: WKURLSchemeTask) {
        let id = ObjectIdentifier(task)
        lock.withLock { _ = active.insert(id) }
        let shelf = MainActor.assumeIsolated { library.shelf }
        let request = task.request
        queue.async { [weak self] in
            let found = Self.resolve(request, in: shelf)
            DispatchQueue.main.async {
                // Only answer tasks the web view has not stopped meanwhile.
                guard let self, self.lock.withLock({ self.active.remove(id) != nil }) else { return }
                guard let url = request.url, let found else {
                    task.didFailWithError(URLError(.fileDoesNotExist))
                    return
                }
                let textual = found.mimeType.hasPrefix("text/") || found.mimeType.contains("javascript")
                let response = URLResponse(url: url, mimeType: found.mimeType, expectedContentLength: found.data.count,
                                           textEncodingName: textual ? "utf-8" : nil)
                task.didReceive(response)
                task.didReceive(found.data)
                task.didFinish()
            }
        }
    }

    func webView(_ webView: WKWebView, stop task: WKURLSchemeTask) {
        lock.withLock { _ = active.remove(ObjectIdentifier(task)) }
    }

    private static func resolve(_ request: URLRequest, in shelf: MDShelf?) -> Found? {
        guard let shelf, let url = request.url, let scheme = url.scheme?.lowercased() else { return nil }
        let identifier: String?
        let name: String
        if scheme == Self.scheme {
            // minedict://dict/<identifier>/<resource path>
            guard url.host == "dict" else { return nil }
            let components = url.pathComponents.dropFirst()
            guard components.count >= 2 else { return nil }
            identifier = components.first
            name = components.dropFirst().joined(separator: "/")
        } else {
            // sound://<resource path>, relative to the page that asked for it
            guard let main = request.mainDocumentURL, main.scheme == Self.scheme else { return nil }
            identifier = main.pathComponents.dropFirst().first
            name = (url.host(percentEncoded: false) ?? "") + url.path(percentEncoded: false)
        }
        guard let identifier, let dictionary = shelf.dictionary(withIdentifier: identifier) else { return nil }
        var mime: NSString?
        guard let data = shelf.resourceNamed(name, for: dictionary, mimeType: &mime) else { return nil }
        return Found(data: data, mimeType: (mime as String?) ?? "application/octet-stream")
    }
}
