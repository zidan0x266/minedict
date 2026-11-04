// minedict - BSD 3-Clause License; see README.
import Foundation

/// The canvas the entry page is drawn on: white, as dictionaries expect, dark,
/// or whichever the system is using.
enum EntryAppearance: String, CaseIterable, Identifiable {
    case light, dark, system

    static let key = "entryAppearance"
    static let standard = EntryAppearance.light

    var id: String { rawValue }

    var title: String {
        switch self {
        case .light: "Light"
        case .dark: "Dark"
        case .system: "Follow System"
        }
    }
}

/// The web view's zoom factor, kept between launches.
enum PageZoom {
    static let key = "pageZoom"
    static let normal = 1.0

    static func bigger(_ zoom: Double) -> Double { min(zoom * 1.1, 3.0) }
    static func smaller(_ zoom: Double) -> Double { max(zoom / 1.1, 0.5) }
}
