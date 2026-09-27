// swift-tools-version: 5.9
// Portico — plataforma de execução/compatibilidade para jogos de PC em iOS.
// Este SPM package cobre os módulos portáveis (PorticoRuntime C + PorticoCore Swift)
// para permitir build/test no Linux e no macOS. O app iOS (Sources/PorticoApp)
// é compilado pelo Xcode (Portico.xcodeproj) e não faz parte dos targets SPM.
import PackageDescription

let package = Package(
    name: "Portico",
    platforms: [
        .iOS(.v17),
        .macOS(.v14),
    ],
    products: [
        .library(name: "PorticoRuntime", targets: ["PorticoRuntime"]),
        .library(name: "PorticoCore", targets: ["PorticoCore"]),
    ],
    targets: [
        // Núcleo C11: parser PE, ZIP (zlib), interpretador x86-32 (subconjunto),
        // stream de comandos gráficos, ring de áudio, sonda de capacidades, host de runtime.
        .target(
            name: "PorticoRuntime",
            path: "Sources/PorticoRuntime",
            publicHeadersPath: "include",
            cSettings: [
                .define("PR_ENABLE_ZLIB", to: "1"),
            ],
            linkerSettings: [
                .linkedLibrary("z"),
                .linkedLibrary("m"),
            ]
        ),
        // Núcleo Swift portável: modelos, biblioteca, configuração, importação,
        // gerenciadores (runtime/processo/ambiente), camadas gráfica/áudio/entrada,
        // compatibilidade e diagnóstico.
        .target(
            name: "PorticoCore",
            dependencies: ["PorticoRuntime"],
            path: "Sources/PorticoCore"
        ),
        .testTarget(
            name: "PorticoCoreTests",
            dependencies: ["PorticoCore"],
            path: "Tests/PorticoCoreTests"
        ),
    ],
    cLanguageStandard: .c11
)
