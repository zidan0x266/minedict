// minedict - BSD 3-Clause License; see README.
import AVFoundation
import AppKit
import UniformTypeIdentifiers

/// Plays the clip a `sound://` link points at, one at a time.
@MainActor
final class SoundPlayer: NSObject, AVAudioPlayerDelegate {
    static let shared = SoundPlayer()
    private var player: AVAudioPlayer?

    func play(_ data: Data, name: String) {
        player?.stop()
        let hint = UTType(filenameExtension: (name as NSString).pathExtension)?.identifier
        do {
            let next = try AVAudioPlayer(data: data, fileTypeHint: hint)
            next.delegate = self
            next.play()
            player = next
        } catch {
            NSSound.beep()
        }
    }

    nonisolated func audioPlayerDidFinishPlaying(_ player: AVAudioPlayer, successfully flag: Bool) {
        Task { @MainActor in
            if self.player === player { self.player = nil }
        }
    }
}
