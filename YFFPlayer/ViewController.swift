//
//  ViewController.swift
//  YFFPlayer
//
//  Created by Xueyuan Xiao on 2025/5/8.
//

import UIKit

class ViewController: UIViewController {

    lazy var player: TestPlayer = {
        let player = TestPlayer(videoRenderView: pV)!
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
        let url = Bundle.main.url(forResource: "m", withExtension: "avi")
        player.playVideo(with: url)
    }
    
    @IBAction func pauseAct(_ sender: Any) {
    }
    
    @IBAction func resumeAct(_ sender: Any) {
    }
}

