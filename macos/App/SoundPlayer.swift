// minedict - BSD 3-Clause License; see README.
import AVFoundation
import AppKit
import UniformTypeIdentifiers

/// Plays the clip a `sound://` link points at, one at a time.
@MainActor
final class SoundPlayer: NSObject, AVAudioPlayerDelegate {
    static let shared = SoundPlayer()
    private var player: AVAudioPlayer?
    /// Playback volume, 0 to 1.
    var volume: Float = 1

    var isPlaying: Bool { player?.isPlaying ?? false }
    /// Length of the clip playing now, in seconds.
    var duration: TimeInterval { player?.duration ?? 0 }

    func play(_ data: Data, name: String) {
        player?.stop()
        let hint = UTType(filenameExtension: (name as NSString).pathExtension)?.identifier
        do {
            let next = try AVAudioPlayer(data: data, fileTypeHint: hint)
            next.delegate = self
            next.volume = volume
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
