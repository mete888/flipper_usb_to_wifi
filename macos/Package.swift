// swift-tools-version: 5.9

import PackageDescription

let package = Package(
    name: "FlipperInternetBridge",
    platforms: [.macOS(.v13)],
    products: [
        .library(name: "BridgeCore", targets: ["BridgeCore"]),
        .executable(name: "FlipperInternetBridge", targets: ["FlipperInternetBridge"]),
    ],
    targets: [
        .target(name: "HostRadioDecoder"),
        .target(
            name: "BridgeCore",
            dependencies: ["HostRadioDecoder"],
            linkerSettings: [
                .linkedFramework("IOKit"),
                .linkedFramework("CoreBluetooth"),
                .linkedFramework("Security"),
            ]
        ),
        .executableTarget(
            name: "FlipperInternetBridge",
            dependencies: ["BridgeCore"],
            linkerSettings: [
                .linkedFramework("AppKit"),
                .linkedFramework("SwiftUI"),
            ]
        ),
        .testTarget(
            name: "BridgeCoreTests",
            dependencies: ["BridgeCore"]
        ),
    ]
)
