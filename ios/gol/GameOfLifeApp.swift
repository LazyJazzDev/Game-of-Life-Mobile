import SwiftUI

// The Game of Life app: one page hosting the desktop game.
@main
struct GameOfLifeApp: App {
  @UIApplicationDelegateAdaptor(GameAppDelegate.self) private var appDelegate
  var body: some Scene {
    WindowGroup {
      GamesView()
        .preferredColorScheme(.dark)
        .statusBarHidden()
        .persistentSystemOverlays(.hidden)
    }
  }
}
