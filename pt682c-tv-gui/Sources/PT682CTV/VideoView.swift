import SwiftUI

@MainActor
struct VideoContainerView: NSViewRepresentable {
    let state: AppState

    func makeNSView(context: Context) -> NSView {
        let view = NSView()
        view.wantsLayer = true
        view.layer?.backgroundColor = NSColor.black.cgColor
        state.videoNSView = view
        state.attachVideoView()
        return view
    }

    func updateNSView(_ nsView: NSView, context: Context) {}
}
