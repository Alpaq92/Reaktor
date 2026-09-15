// swift-tools-version: 5.9
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

            linkerSettings: [
                .linkedLibrary("dxguid", .when(platforms: [.windows])),
                .linkedLibrary("dinput8", .when(platforms: [.windows])),
            ]
        )
    ],

    cxxLanguageStandard: .cxx17
)
