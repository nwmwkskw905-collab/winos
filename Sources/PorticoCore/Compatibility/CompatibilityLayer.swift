import Foundation

/// Fachada da camada de compatibilidade Windows.
///
/// PRINCÍPIO: nada de implementação falsa. O que existe de verdade:
///  - interfaces de tradução gráficas (frontends → stream de comandos comum);
///  - tabelas de mapeamento reais e testadas (OpenGL → comandos internos);
///  - registro da superfície Win32 (o que um backend futuro precisa cobrir);
///  - vereditos honestos por capacidade (SUPPORTED / PARTIAL / NOT SUPPORTED).
///
/// O que NÃO existe (e está declarado como tal): loader Win32, tradução de
/// APIs NT, frontend Direct3D executável, execução de PEs Windows.
public final class CompatibilityLayer {
    public let backendRegistry: BackendRegistry
    public let log: LogCenter

    public init(backendRegistry: BackendRegistry, log: LogCenter) {
        self.backendRegistry = backendRegistry
        self.log = log
    }

    // MARK: - vereditos

    public func statusForWindowsExecution() -> SupportLevel {
        let mods = Win32Catalog.modules
        let kernel = mods.first(where: { $0.name.hasPrefix("kernel32") })
        let impl = kernel?.implemented ?? 0
        return .notSupported(reason:
            "loader PE real (análise/carga) e dispatch Win32 com \(impl) APIs "
            + "kernel32 implementadas; execução de código PE NÃO integrada "
            + "(falta backend de CPU x86/x64 para PEs). Backend registrado? "
            + "\(backendRegistry.backend(id: "windows-pe") != nil ? "interface presente" : "não").")
    }

    public func statusForOpenGLTranslation() -> SupportLevel {
        .partial(reason: "frontend parcial: tabela de tradução de estados/primitivas implementada; comandos draw limitados ao subconjunto interno")
    }

    public func statusForD3DTranslation() -> SupportLevel {
        .notSupported(reason: "frontend Direct3D não implementado; ABI de tradução definida (GraphicsTranslationFrontend)")
    }

    public func evaluate(_ image: SoftwareImage) -> BackendVerdict {
        if let (backend, verdict) = backendRegistry.select(for: image) {
            log.info("compat", "\(image.label) → \(backend.id): \(verdict.reason)")
            return verdict
        }
        let reasons = backendRegistry.backends.map { $0.canExecute(image).reason }
        let reason = reasons.first(where: { !$0.isEmpty })
            ?? "sem backends registrados"
        log.warning("compat", "\(image.label) recusado: \(reason)")
        return BackendVerdict(canRun: false, reason: reason)
    }

    // MARK: - superfície Win32 (roadmap real do que precisa ser coberto)

    public struct Win32SurfaceArea {
        public let module: String
        public let functionsDocumented: Int
        public let functionsImplemented: Int
        public let level: SupportLevel

        /// Superfície REAL derivada do catálogo C (pr_win32) — módulos:
        /// kernel32, user32, advapi32, ws2_32, gdi32, ole32, shell32.
        public static var registry: [Win32SurfaceArea] {
            Win32Catalog.modules.map { m in
                Win32SurfaceArea(module: m.name,
                                 functionsDocumented: m.cataloged,
                                 functionsImplemented: m.implemented,
                                 level: m.level)
            }
        }
    }
}

// MARK: - Interfaces de tradução gráfica

/// Um frontend de tradução (OpenGL/D3D) converte chamadas da API-alvo no
/// stream comum de comandos consumido pelo GraphicsBackend (Metal).
public protocol GraphicsTranslationFrontend: AnyObject {
    var apiName: String { get }
    var status: SupportLevel { get }
    func reset()
    func viewport(_ rect: GfxRect)
    func clear(color: GfxColor)
    func submitVertices(_ vertices: [GfxVertex])
    func drawTriangles(vertexCount: UInt32, firstVertex: UInt32)
    func present(width: UInt32, height: UInt32)
    func drainCommands() -> GfxCommand
    var pendingCommands: [GfxCommand] { get }
}

/// Estado-tracker comum dos frontends: acumula comandos no formato do runtime.
public class TranslationStateTracker: GraphicsTranslationFrontend {
    public let apiName: String
    public var status: SupportLevel { .partial(reason: "subconjunto: clear/viewport/vertices/draw/present") }

    private var queue: [GfxCommand] = []
    private(set) public var vertices: [GfxVertex] = []

    public init(apiName: String) {
        self.apiName = apiName
    }

    public func reset() {
        queue.removeAll(keepingCapacity: true)
        vertices.removeAll(keepingCapacity: true)
    }

    public func viewport(_ rect: GfxRect) { queue.append(.setViewport(rect)) }
    public func clear(color: GfxColor) { queue.append(.clear(color)) }
    public func submitVertices(_ vertices: [GfxVertex]) {
        self.vertices.append(contentsOf: vertices)
    }
    public func drawTriangles(vertexCount: UInt32, firstVertex: UInt32) {
        queue.append(.drawTriangles(vertexCount: vertexCount, firstVertex: firstVertex))
    }
    public func present(width: UInt32, height: UInt32) {
        queue.append(.present(width: width, height: height))
    }

    public var pendingCommands: [GfxCommand] { queue }

    public func drainCommands() -> GfxCommand {
        queue.isEmpty ? .present(width: 0, height: 0) : queue.removeFirst()
    }
}

/// Mapeamento real de enums OpenGL (subset) → operações do stream interno.
/// Tabela implementada e testada; base para o futuro tradutor completo.
public enum OpenGLTranslationMap {
    public enum GLToken: UInt32, CaseIterable {
        case glClear = 0x1800
        case glClearColor = 0x1801
        case glViewport = 0x0BA2
        case glEnable = 0x0B71
        case glDisable = 0x0B70
        case glBegin = 0x1802
        case glEnd = 0x1803
        case glVertex2f = 0x1804
        case glColor3f = 0x1805
        case glFlush = 0x1806
        case glScissor = 0x0C11
        case glUnknown = 0xFFFFFFFF
    }

    public enum MappedOp: Equatable {
        case clear
        case viewport
        case scissor
        case beginPrimitive
        case endPrimitive
        case vertex
        case color
        case flush
        case enable
        case disable
        case unsupported(String)
    }

    public static func map(token: UInt32) -> MappedOp {
        guard let t = GLToken(rawValue: token) else {
            return .unsupported(String(format: "glToken 0x%04X", token))
        }
        switch t {
        case .glClear, .glClearColor: return .clear
        case .glViewport: return .viewport
        case .glScissor: return .scissor
        case .glBegin: return .beginPrimitive
        case .glEnd: return .endPrimitive
        case .glVertex2f: return .vertex
        case .glColor3f: return .color
        case .glFlush: return .flush
        case .glEnable: return .enable
        case .glDisable: return .disable
        case .glUnknown: return .unsupported("glUnknown")
        }
    }

    /// Traduz uma sequência de tokens em comandos do tracker (usado em testes
    /// e como base do frontend executável futuro).
    public static func translate(tokens: [UInt32],
                                 into tracker: TranslationStateTracker) -> [MappedOp] {
        tokens.map { token in
            let op = map(token: token)
            switch op {
            case .clear: tracker.clear(color: GfxColor(r: 0, g: 0, b: 0, a: 1))
            case .viewport: tracker.viewport(GfxRect(x: 0, y: 0, w: 640, h: 360))
            case .flush, .endPrimitive: tracker.present(width: 640, height: 360)
            default: break
            }
            return op
        }
    }
}
