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
//        let url = Bundle.main.url(forResource: "m_264", withExtension: "mp4")
//        let url = Bundle.main.url(forResource: "m_an", withExtension: "avi")
        let url = URL(string: "https://09367961bc62cea41dfb5071f34abce1.h2.smtcdns.net/pull-hs-f5-hot.flive.douyincdn.com/thirdgame/stream-405612924286534410.flv?arch_hrchy=w1&exp_hrchy=w1&expire=1749741634&major_anchor_level=common&sign=aa19ba228d9e84ca3a7c3a2c0d72bf23&t_id=037-20250605232033F9533FD5C397E920E453-d3lqt5&unique_id=stream-405612924286534410_778_flv&volcSecret=aa19ba228d9e84ca3a7c3a2c0d72bf23&volcTime=1749741634&abr_pts=-800&_session_id=037-20250605232033F9533FD5C397E920E453-d3lqt5.1749136835086.00023&rsi=1&TxLiveCode=cold_stream&svr_type=live_oc&tencent_test_client_ip=180.162.13.32&stream=&dispatch_from=OC_MGR61.170.89.86&utime=1749136835184&TxDispType=7&txTliveMsg=S5;WH_EIC1DX_1;WH_EIC1DX_1;")
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
