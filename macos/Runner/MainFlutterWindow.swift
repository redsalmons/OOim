import Cocoa
import FlutterMacOS
import desktop_multi_window

class MainFlutterWindow: NSWindow {
  override func awakeFromNib() {
    let flutterViewController = FlutterViewController()
    let windowFrame = self.frame
    self.contentViewController = flutterViewController
    self.setFrame(windowFrame, display: true)
    let windowTitle = (Locale.preferredLanguages.first?.hasPrefix("zh") == true) ? "零海" : "OceanTalk"
    self.title = windowTitle
    // The nib's APP_NAME placeholder and the window controller can reset the
    // title during/after nib loading, so re-assert it once the window is up.
    DispatchQueue.main.async { [weak self] in
      self?.title = windowTitle
    }

    RegisterGeneratedPlugins(registry: flutterViewController)

    // The in-app language switch lives in Dart; let it retitle this window.
    let titleChannel = FlutterMethodChannel(
      name: "oim/window_title",
      binaryMessenger: flutterViewController.engine.binaryMessenger)
    titleChannel.setMethodCallHandler { [weak self] (call, result) in
      if call.method == "setTitle", let code = call.arguments as? String {
        self?.title = (code == "zh") ? "零海" : "OceanTalk"
        result(nil)
      } else {
        result(FlutterMethodNotImplemented)
      }
    }

    FlutterMultiWindowPlugin.setOnWindowCreatedCallback { controller in
      RegisterGeneratedPlugins(registry: controller)
    }

    super.awakeFromNib()
  }
}
