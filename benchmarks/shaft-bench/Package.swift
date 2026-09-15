// swift-tools-version: 5.9
//
// The dependency is Shaft's own package, taken from its repository the way its
// CounterTemplate takes it. The Cxx interoperability mode and the C++17
// standard are not optional: Shaft's default renderer is Skia, and it does not
// build without them.
import PackageDescription

let package = Package(
    name: "ShaftBench",

    platforms: [
        .macOS(.v14)
    ],

    dependencies: [
        .package(url: "https://github.com/ShaftUI/Shaft", branch: "main")
    ],

    targets: [
        .executableTarget(
            name: "ShaftBench",
            dependencies: [
                .product(name: "Shaft", package: "Shaft"),
                .product(name: "ShaftSetup", package: "Shaft"),
            ],
            swiftSettings: [
                .interoperabilityMode(.Cxx)
            ],

            // SwiftSDL3 compiles its DirectInput haptic code on Windows but
            // does not link the library the GUIDs live in, so the executable
            // does it. Nothing on the other platforms needs this.
            linkerSettings: [
                .linkedLibrary("dxguid", .when(platforms: [.windows])),
                .linkedLibrary("dinput8", .when(platforms: [.windows])),
            ]
        )
    ],

    cxxLanguageStandard: .cxx17
)
