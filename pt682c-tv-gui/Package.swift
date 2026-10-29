// swift-tools-version:5.9
import PackageDescription

let package = Package(
    name: "PT682CTV",
    platforms: [.macOS(.v13)],
    targets: [
        .target(name: "SessionLifecycle"),
        .target(name: "TSRecorder"),
        .target(
            name: "VLCShim",
            path: "Sources/VLCShim"
        ),
        .executableTarget(
            name: "PT682CTV",
            dependencies: ["VLCShim", "SessionLifecycle", "TSRecorder"],
            path: "Sources/PT682CTV"
        ),
        .testTarget(name: "SessionLifecycleTests", dependencies: ["SessionLifecycle"])
    ]
)
