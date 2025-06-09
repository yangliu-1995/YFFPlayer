//
//  ViewController.swift
//  YFFPlayer
//
//  Created by Xueyuan Xiao on 2025/5/8.
//

import UIKit
import YFFPlayer
import UniformTypeIdentifiers

class ViewController: UIViewController {

    lazy var player: YFFPlayer = {
        let player = YFFPlayer(videoRenderView: pV)!
        return player
    }()

    @IBOutlet weak var playButton: UIButton!

    @IBOutlet weak var urlField: UITextField!

    @IBOutlet weak var pV: UIView!

    override func viewDidLoad() {
        super.viewDidLoad()
        urlField.clearButtonMode = .whileEditing

        let url0 = Bundle.main.url(forResource: "m", withExtension: "avi")!
        let url1 = Bundle.main.url(forResource: "m_264", withExtension: "mp4")!
        let url2 = Bundle.main.url(forResource: "m_an", withExtension: "avi")!
        let urls = [("mepg4 in avi", url0), ("h264 in mp4", url1), ("h264 in mp4 no audio", url2)]
        var actions: [UIAction] = urls.map({ item in
            UIAction(title: item.0) { [weak self] _ in
                guard let self else {
                    return
                }
                self.player.playVideo(with: item.1)
            }
        })

        actions.append(UIAction(title: "Select from Files...") { [weak self] _ in
            guard let self else {
                return
            }
            self.showFilePicker()
        })

        playButton.menu = UIMenu(children: actions)
        playButton.showsMenuAsPrimaryAction = true
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        super.touchesBegan(touches, with: event)
    }

    @IBAction func playURLAction(_ sender: Any) {
        view.endEditing(true)
        guard let urlString = urlField.text,
              let url = URL(string: urlString) else {
            return
        }
        player.playVideo(with: url)
    }

    @IBAction func pauseAct(_ sender: Any) {
        player.pause()
    }

    @IBAction func resumeAct(_ sender: Any) {
        player.resume()
    }

    @IBAction func rate_05(_ sender: Any) {
        player.setPlaybackRate(0.5)
    }

    @IBAction func rate_10(_ sender: Any) {
        player.setPlaybackRate(1)
    }

    @IBAction func rate_15(_ sender: Any) {
        player.setPlaybackRate(1.5)
    }

    @IBAction func rate_20(_ sender: Any) {
        player.setPlaybackRate(2.0)
    }

    private func showFilePicker() {
        let supportedTypes: [UTType] = [
            .movie,        // 包括 .mp4, .mov
            .video,        // 泛视频
            .audio,        // 泛音频
            .mp3,
            .mpeg4Audio    // .m4a, .aac
        ]

        let picker = UIDocumentPickerViewController(forOpeningContentTypes: supportedTypes, asCopy: false)
        picker.delegate = self
        picker.allowsMultipleSelection = false
        present(picker, animated: true, completion: nil)
    }
}

extension ViewController: UIDocumentPickerDelegate {
    func documentPicker(_ controller: UIDocumentPickerViewController, didPickDocumentsAt urls: [URL]) {
        guard let fileURL = urls.first else { return }

        let accessGranted = fileURL.startAccessingSecurityScopedResource()
        defer {
            if accessGranted {
                fileURL.stopAccessingSecurityScopedResource()
            }
        }

        let tmpDir = FileManager.default.temporaryDirectory
        let destURL = tmpDir.appendingPathComponent(fileURL.lastPathComponent)

        do {
            // 如有重复则先删除
            if FileManager.default.fileExists(atPath: destURL.path) {
                try FileManager.default.removeItem(at: destURL)
            }

            try FileManager.default.copyItem(at: fileURL, to: destURL)
            print("✅ 文件已复制到 tmp: \(destURL.path)")
            player.playVideo(with: destURL)
            // 👉 现在你可以用 FFmpeg 加载这个路径
        } catch {
            print("❌ 拷贝失败: \(error)")
        }
    }
}
