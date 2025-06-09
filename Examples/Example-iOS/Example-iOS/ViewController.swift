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

    @IBOutlet weak var slider: UISlider!

    @IBOutlet weak var playButton: UIButton!

    @IBOutlet weak var urlField: UITextField!

    @IBOutlet weak var pV: UIView!

    @IBOutlet weak var currentTimeLabel: UILabel!

    @IBOutlet weak var durationLabel: UILabel!

    var isUserDraggingSlider = false

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

        player.progressHandler = { [weak self] current, duration in
            guard let self else {
                return
            }
            if (self.player.isLiveStream) {
                self.currentTimeLabel.text = "Live"
                self.durationLabel.text = formatMilliseconds(current)
                if !self.isUserDraggingSlider {
                    self.slider.value = 1
                }
            } else {
                self.currentTimeLabel.text = formatMilliseconds(current)
                self.durationLabel.text = formatMilliseconds(duration)
                if !self.isUserDraggingSlider {
                    self.slider.value = Float(current) / Float(duration)
                }
            }
        }

        slider.addTarget(self, action: #selector(sliderTouchDown), for: .touchDown)
        slider.addTarget(self, action: #selector(sliderTouchUp), for: [.touchUpInside, .touchUpOutside])
        slider.addTarget(self, action: #selector(sliderValueChanged), for: .valueChanged)
    }

    func formatMilliseconds(_ milliseconds: Int) -> String {
        let totalSeconds = milliseconds / 1000
        //        let ms = milliseconds % 1000
        let hours = totalSeconds / 3600
        let minutes = (totalSeconds % 3600) / 60
        let seconds = totalSeconds % 60

        if hours > 0 {
            // 显示 hh:mm:ss.SSS
            return String(format: "%02d:%02d:%02d", hours, minutes, seconds)
        } else {
            // 显示 mm:ss.SSS（省略小时）
            return String(format: "%02d:%02d", minutes, seconds)
        }
    }

    @IBAction func playURLAction(_ sender: Any) {
        urlField.resignFirstResponder()
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

    @IBAction func stopAct(_ sender: Any) {
        player.stop()
    }

    @objc func sliderTouchDown(_ sender: UISlider) {
        isUserDraggingSlider = true
    }

    @objc func sliderTouchUp(_ sender: UISlider) {
        isUserDraggingSlider = false
        let seekTime = sender.value
        seek(to: seekTime)
    }

    @objc func sliderValueChanged(_ sender: UISlider) {
        // 可选：更新 label 等
        print("拖动中 value: \(sender.value)")
    }

    func seek(to time: Float) {
        print("🎯 执行 seek 到: \(time)")
        player.seek(to: time)
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
