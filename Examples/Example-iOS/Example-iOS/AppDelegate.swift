//
//  AppDelegate.swift
//  Example-iOS
//
//  Created by Xueyuan Xiao on 2025/5/30.
//

import UIKit

@main
class AppDelegate: UIResponder, UIApplicationDelegate {



    func application(_ application: UIApplication, didFinishLaunchingWithOptions launchOptions: [UIApplication.LaunchOptionsKey: Any]?) -> Bool {
        clearTmpDirectory()
        return true
    }

    func clearTmpDirectory() {
        let tmpDir = FileManager.default.temporaryDirectory

        do {
            let contents = try FileManager.default.contentsOfDirectory(at: tmpDir, includingPropertiesForKeys: nil, options: [])
            for file in contents {
                try FileManager.default.removeItem(at: file)
            }
            print("🧹 清空 tmp 成功")
        } catch {
            print("❌ 清空 tmp 失败: \(error)")
        }
    }

    // MARK: UISceneSession Lifecycle

    func application(_ application: UIApplication, configurationForConnecting connectingSceneSession: UISceneSession, options: UIScene.ConnectionOptions) -> UISceneConfiguration {
        // Called when a new scene session is being created.
        // Use this method to select a configuration to create the new scene with.
        return UISceneConfiguration(name: "Default Configuration", sessionRole: connectingSceneSession.role)
    }

    func application(_ application: UIApplication, didDiscardSceneSessions sceneSessions: Set<UISceneSession>) {
        // Called when the user discards a scene session.
        // If any sessions were discarded while the application was not running, this will be called shortly after application:didFinishLaunchingWithOptions.
        // Use this method to release any resources that were specific to the discarded scenes, as they will not return.
    }


}

