//
//  ViewController.swift
//  YFFPlayer
//
//  Created by Xueyuan Xiao on 2025/5/8.
//

import UIKit
import YFFPlayer

class ViewController: UIViewController {

    lazy var player: YFFPlayer = {
        let player = YFFPlayer(videoRenderView: pV)!
        return player
    }()

    @IBOutlet weak var pV: UIView!

    override func viewDidLoad() {
        super.viewDidLoad()
        // Do any additional setup after loading the view.
    }

    override func touchesBegan(_ touches: Set<UITouch>, with event: UIEvent?) {
        super.touchesBegan(touches, with: event)
    }

    @IBAction func playAct(_ sender: Any) {
//        let url = Bundle.main.url(forResource: "m", withExtension: "avi")
        let url = Bundle.main.url(forResource: "m_264", withExtension: "mp4")
//        let url = Bundle.main.url(forResource: "m_an", withExtension: "avi")
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
}
