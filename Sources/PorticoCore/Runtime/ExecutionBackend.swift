import Foundation
import PorticoRuntime

/// Veredito de compatibilidade de um backend para uma imagem.
public struct BackendVerdict: Equatable, Sendable {
    public let canRun: Bool
    public let reason: String
    public init(canRun: Bool, reason: String) {
        self.canRun = canRun
        self.reason = reason
    }
}

/// Fase do ciclo de vida de um backend de execução.
public enum BackendPhase: String, Equatable, Sendable {
    case idle, loaded, initialized, running, paused, stopped, failed
}

/// Backend de execução (ABI Swift da camada de compatibilidade).
///
/// Ciclo de vida granular (v2):
///   load(executable:) → initialize(context:) → run() ⇄ pause()/resume()
///   → stop() → shutdown()
/// Implementações reais registram-se aqui; nada é simulado:
/// um backend sem implementação declara `canRun=false` com o motivo.
public protocol ExecutionBackend: AnyObject {
    var id: String { get }
    var displayName: String { get }
    var version: String { get }
    var phase: BackendPhase { get }
    func canExecute(_ image: SoftwareImage) -> BackendVerdict
    // Ciclo de vida
    func load(executable: URL, image: SoftwareImage) throws
    func initialize(context: RuntimeSessionContext) throws
    func run() throws
    func pause()
    func resume() throws
    func stop()
    func shutdown()
    // Ciclo composto (load+initialize+run) e E/S de sessão
    func start(context: RuntimeSessionContext) throws
    func stepFrame(input: InputState, dt: Float, timeMs: Double) -> FrameResult
    func consumeGraphicsFrame() -> GfxFrame
    func pullAudio(maxFrames: Int) -> [Float]
    func drainLogs(into log: LogCenter)
}

// MARK: - Backend nativo real: interpretador PXP (C host)

/// Backend REAL: executa payloads PXP (IA-32 interpretado) pelo host C.
/// Usado pelo Self-Test e por conteúdo nativo. NÃO executa software Windows.
public final class PXPInterpreterBackend: ExecutionBackend {
    public let id = "pxp-interpreter"
    public let displayName = "Interpretador nativo PXP"
    public let version = "1.1.0"

    private var host: OpaquePointer?
    private var logOffset: UInt64 = 0
    private var lastFrames: UInt32 = 0
    private let surfaceBuffer = GfxSurfaceBuffer()
    public private(set) var phase: BackendPhase = .idle
    private var pendingPayload: Data?

    public init() {
        pr_host_register_builtin_backends()
    }

    deinit { shutdown() }

    public func canExecute(_ image: SoftwareImage) -> BackendVerdict {
        switch image {
        case .pxpNative:
            return BackendVerdict(canRun: true, reason: "payload PXP nativo (IA-32 interpretado)")
        case .windowsPE(let pe):
            return BackendVerdict(canRun: false, reason:
                "PE Windows (\(pe.arch)) exige camada Win32 — não integrada neste build. "
                + "Veja docs/BACKEND_INTEGRATION.md para o ponto de encaixe (pr_host_backend_v1).")
        case .unknown:
            return BackendVerdict(canRun: false, reason: "formato de imagem desconhecido")
        }
    }

    public func load(executable: URL, image: SoftwareImage) throws {
        if phase != .idle && phase != .stopped { shutdown() }
        let data: Data
        do {
            data = try Data(contentsOf: executable)
        } catch {
            phase = .failed
            throw RuntimeFailure.io(reason: "leitura do payload: \(error)")
        }
        switch image {
        case .pxpNative:
            guard data.count >= 4,
                  data.prefix(4).elementsEqual([0x50, 0x58, 0x50, 0x30]) else {
                phase = .failed
                throw RuntimeFailure.payloadInvalid(reason: "payload PXP sem assinatura PXP0")
            }
        case .windowsPE:
            break // aceito p/ recusa honesta na inicialização (pr_host_start is_pe)
        case .unknown:
            phase = .failed
            throw RuntimeFailure.payloadInvalid(reason: "formato de imagem desconhecido")
        }
        pendingPayload = data
        phase = .loaded
    }

    public func initialize(context: RuntimeSessionContext) throws {
        guard phase == .loaded, let payload = pendingPayload else {
            throw RuntimeFailure.backendUnavailable(reason: "load() não executado")
        }
        pr_host_register_builtin_backends()
        let h = pr_host_create()
        host = h
        guard let h else {
            phase = .failed
            throw RuntimeFailure.backendUnavailable(reason: "pr_host_create")
        }

        let isPE: Int32 = (context.profile.tipo == .windowsPE) ? 1 : 0
        let w = UInt32(context.config.resolution.width)
        let height = UInt32(context.config.resolution.height)
        let fpsCap = Float(context.config.fps.value ?? 0)

        let st: pr_status = payload.withUnsafeBytes { raw in
            var info = pr_host_start_info()
            info.payload = raw.baseAddress
            info.payload_len = raw.count
            info.is_pe = isPE
            info.argv = nil
            info.env_keys = nil
            info.env_vals = nil
            info.env_count = 0
            info.target_w = w
            info.target_h = height
            info.fps_cap = fpsCap
            return pr_host_start(h, &info, nil)
        }
        guard st == PR_OK else {
            let detail = contextDetail(h)
            stop()
            phase = .failed
            switch st {
            case PR_ERR_UNSUPPORTED:
                throw RuntimeFailure.unsupported(reason: detail.isEmpty ? "sem backend compatível" : detail)
            case PR_ERR_FORMAT:
                throw RuntimeFailure.payloadInvalid(reason: detail.isEmpty ? "formato inválido" : detail)
            default:
                throw RuntimeFailure.backendUnavailable(reason: pr_status_str(st).map { String(cString: $0) } ?? "?")
            }
        }
        phase = .initialized
    }

    public func run() throws {
        switch phase {
        case .initialized, .paused:
            phase = .running
        default:
            throw RuntimeFailure.backendUnavailable(
                reason: "backend não inicializado (fase \(phase.rawValue))")
        }
    }

    public func pause() {
        if phase == .running { phase = .paused }
    }

    public func resume() throws {
        switch phase {
        case .paused: phase = .running
        case .running: break
        default:
            throw RuntimeFailure.backendUnavailable(
                reason: "retomada fora de estado (fase \(phase.rawValue))")
        }
    }

    /// Ciclo composto: load + initialize + run (compatível com o self-test).
    public func start(context: RuntimeSessionContext) throws {
        shutdown()
        let image: SoftwareImage = (context.profile.tipo == .windowsPE)
            ? .windowsPE(PEImage.mockForRefusal(context.profile.arquiteturaExe))
            : .pxpNative
        try load(executable: context.executableURL, image: image)
        try initialize(context: context)
        try run()
    }

    private func contextDetail(_ h: OpaquePointer?) -> String {
        guard let h, let log = pr_host_log(h) else { return "" }
        var buf = [pr_log_entry](repeating: pr_log_entry(), count: 8)
        let n = buf.withUnsafeMutableBufferPointer { p -> Int in
            pr_log_read(log, p.baseAddress, p.count, 0)
        }
        guard n > 0 else { return "" }
        var e = buf[n - 1]
        return withUnsafeBytes(of: &e.msg) { raw -> String in
            let p = raw.bindMemory(to: CChar.self).baseAddress!
            return String(cString: p)
        }
    }

    public func stepFrame(input: InputState, dt: Float, timeMs: Double) -> FrameResult {
        guard let h = host else {
            return FrameResult(state: .failed,
                               failure: .backendUnavailable(reason: "sessão não iniciada"))
        }
        var inC = pr_host_frame_in()
        inC.input.buttons = input.buttons
        inC.input.axes.0 = input.moveX
        inC.input.axes.1 = input.moveY
        inC.input.axes.2 = input.lookX
        inC.input.axes.3 = input.lookY
        inC.input.trigger_lt = input.triggerLT
        inC.input.trigger_rt = input.triggerRT
        inC.dt = dt
        inC.time_ms = timeMs

        var outC = pr_host_frame_out()
        let st = pr_host_frame(h, &inC, &outC)
        lastFrames = outC.frames_presented

        let state: RuntimeSessionState
        switch outC.state.rawValue {
        case PR_HOST_RUNNING.rawValue: state = .running
        case PR_HOST_STOPPED.rawValue: state = .stopped
        case PR_HOST_FAILED.rawValue: state = .failed
        case PR_HOST_IDLE.rawValue: state = .idle
        default: state = .failed
        }

        var failure: RuntimeFailure?
        if state == .failed {
            let msg = withUnsafeBytes(of: outC.message) { String(cString: $0.bindMemory(to: CChar.self).baseAddress!) }
            failure = .guestFault(detail: msg)
        }
        return FrameResult(state: state,
                           framesPresented: outC.frames_presented,
                           halted: outC.halted != 0,
                           message: "",
                           failure: failure)
    }

    public func consumeGraphicsFrame() -> GfxFrame {
        guard let h = host, let stream = pr_host_gfx(h) else { return GfxFrame() }
        var frame = GfxFrame()

        var cmdBuf = [pr_gfx_cmd](repeating: pr_gfx_cmd(), count: 256)
        let n = cmdBuf.withUnsafeMutableBufferPointer { p -> Int in
            pr_gfx_pull(stream, p.baseAddress, p.count)
        }
        frame.commands.reserveCapacity(n)
        for i in 0..<n {
            let c = cmdBuf[i]
            switch c.op {
            case UInt32(PR_GFX_CLEAR.rawValue):
                frame.commands.append(.clear(GfxColor(
                    r: c.a.clear.color.r, g: c.a.clear.color.g,
                    b: c.a.clear.color.b, a: c.a.clear.color.a)))
            case UInt32(PR_GFX_SET_VIEWPORT.rawValue):
                frame.commands.append(.setViewport(GfxRect(
                    x: c.a.rect.rect.x, y: c.a.rect.rect.y,
                    w: c.a.rect.rect.w, h: c.a.rect.rect.h)))
            case UInt32(PR_GFX_SET_SCISSOR.rawValue):
                frame.commands.append(.setScissor(GfxRect(
                    x: c.a.rect.rect.x, y: c.a.rect.rect.y,
                    w: c.a.rect.rect.w, h: c.a.rect.rect.h)))
            case UInt32(PR_GFX_DRAW_TRIANGLES.rawValue):
                frame.commands.append(.drawTriangles(
                    vertexCount: c.a.draw.vertex_count,
                    firstVertex: c.a.draw.first_vertex))
            case UInt32(PR_GFX_PRESENT.rawValue):
                frame.commands.append(.present(width: c.a.present.width,
                                               height: c.a.present.height))
            case UInt32(PR_GFX_SET_FILTER.rawValue):
                frame.commands.append(.setFilter(nearest: c.a.filter.filter == 1))
            default:
                break
            }
        }

        var vcount = 0
        if let verts = pr_gfx_vertices(stream, &vcount), vcount > 0 {
            frame.vertices = Array(UnsafeBufferPointer(start: verts, count: vcount)).map {
                GfxVertex(x: $0.x, y: $0.y, r: $0.r, g: $0.g, b: $0.b)
            }
        }

        // Superfície/textura do frame (apresentação por pixels) — buffer reutilizado.
        var sw: UInt32 = 0, sh: UInt32 = 0
        if let surf = pr_gfx_surface(stream, &sw, &sh), sw > 0, sh > 0 {
            if surfaceBuffer.update(width: sw, height: sh, copyFrom: surf) {
                frame.surface = surfaceBuffer
            }
        }
        return frame
    }

    public func pullAudio(maxFrames: Int) -> [Float] {
        guard let h = host, let ring = pr_host_audio(h) else { return [] }
        var out = [Float](repeating: 0, count: maxFrames * 2)
        let got = out.withUnsafeMutableBufferPointer { p -> Int in
            pr_audio_pull(ring, p.baseAddress, maxFrames)
        }
        if got < out.count { out.removeLast(out.count - got * 2) }
        return out
    }

    public func drainLogs(into log: LogCenter) {
        guard let h = host, let cLog = pr_host_log(h) else { return }
        var buf = [pr_log_entry](repeating: pr_log_entry(), count: 64)
        while true {
            let n = buf.withUnsafeMutableBufferPointer { p -> Int in
                pr_log_read(cLog, p.baseAddress, p.count, logOffset)
            }
            if n == 0 { break }
            for i in 0..<n {
                let e = buf[i]
                let level: LogLevel
                switch e.level.rawValue {
                case PR_LOG_DEBUG.rawValue: level = .debug
                case PR_LOG_INFO.rawValue: level = .info
                case PR_LOG_WARN.rawValue: level = .warning
                case PR_LOG_ERROR.rawValue: level = .error
                default: level = .info
                }
                let cat = withUnsafeBytes(of: e.cat) { String(cString: $0.bindMemory(to: CChar.self).baseAddress!) }
                let msg = withUnsafeBytes(of: e.msg) { String(cString: $0.bindMemory(to: CChar.self).baseAddress!) }
                log.log(level, cat, msg)
            }
            logOffset += UInt64(n)
            if n < buf.count { break }
        }
    }

    public func stop() {
        if let h = host {
            pr_host_stop(h)
            pr_host_destroy(h)
        }
        host = nil
        logOffset = 0
        if phase != .idle { phase = .stopped }
    }

    public func shutdown() {
        stop()
        pendingPayload = nil
        phase = .idle
    }

    public var framesPresented: UInt32 { lastFrames }
}

// MARK: - Backend Windows PE (execução REAL PE32 via pr_peproc)

/// Backend para executáveis Windows (PE).
///
/// ESTADO REAL:
///  - `load()` é REAL: valida e mapeia a imagem em memória (pr_pe_load) e gera
///    o relatório completo (imports/exports/diagnóstico);
///  - `initialize()` analisa a cobertura dos imports contra o catálogo Win32
///    (pr_win32) e recusa com o motivo preciso — NÃO executa código PE;
///  - a EXECUÇÃO requer o backend de CPU x86/x64 para PEs + tradução, que NÃO
///    está integrado neste build (ver docs/BACKEND_INTEGRATION.md).
public final class WindowsPEBackend: ExecutionBackend {
    public let id = "windows-pe"
    public let displayName = "Compatibilidade Windows (PE32 + Win32 parcial)"
    public let version = "1.1.0 (execução real PE32/PE32+: IA-32 + subconjunto x64 + Win32 parcial)"

    public private(set) var phase: BackendPhase = .idle
    public private(set) var loadedImage: PELoadedImage?
    public private(set) var lastCoverage: Win32Coverage?
    public private(set) var lastDiagnostic: String = ""
    public private(set) var exitCode: UInt32?

    private var proc: OpaquePointer?      // pr_peproc*
    private var prLog: OpaquePointer?     // pr_log* (execution log do processo)
    private var logOffset: UInt64 = 0
    private var budgetPerFrame: UInt64 = 200_000
    private let surfaceBuffer = GfxSurfaceBuffer()
    private var lastFrames: UInt32 = 0

    public init() {}

    deinit { shutdown() }

    public func canExecute(_ image: SoftwareImage) -> BackendVerdict {
        switch image {
        case .windowsPE(let pe):
            let a = pe.arch.lowercased()
            let isX64 = pe.machine == 0x8664
                || a.contains("x64") || a.contains("x86-64") || a.contains("amd64")
            let isX86 = pe.machine == 0x014C
                || (!isX64 && !pe.isPE32Plus
                    && (a.contains("x86") || a.contains("i386")))
            if isX86 {
                return BackendVerdict(canRun: true, reason:
                    "PE Windows 32-bit (\(pe.arch)): execução real (PE → memória virtual R/W/X → "
                    + "imports → Win32 → CPU IA-32 interpretada → processo/exit). "
                    + "Win32 parcial: APIs não implementadas param com EXECUTION STOPPED.")
            }
            if isX64 {
                return BackendVerdict(canRun: true, reason:
                    "PE Windows x64 (\(pe.arch)): execução real pelo interpretador x64 MÍNIMO "
                    + "(subconjunto straight-line: movs/ALU/call [rip]/ret/INT/hlt). "
                    + "Instrução fora do subconjunto → EXECUTION STOPPED com opcode/RIP/endereço. "
                    + "Win32 parcial como no x86. NÃO é compatibilidade x64 completa.")
            }
            return BackendVerdict(canRun: false, reason:
                "PE Windows (\(pe.arch)): a execução Win32 nesta arquitetura NÃO está "
                + "integrada neste build (somente x86 e o subconjunto x64 do interpretador). "
                + "Recusa honesta: nada é executado.")
        default:
            return BackendVerdict(canRun: false, reason: "imagem não é PE Windows")
        }
    }

    public func load(executable: URL, image: SoftwareImage) throws {
        shutdown()
        let data: Data
        do {
            data = try Data(contentsOf: executable)
        } catch {
            phase = .failed
            throw RuntimeFailure.io(reason: "leitura do PE: \(error)")
        }
        // metadados/relatório via PELoader (análise PE existente)
        do {
            loadedImage = try PELoader.loadImage(
                data, moduleName: executable.lastPathComponent)
        } catch {
            phase = .failed
            throw RuntimeFailure.payloadInvalid(reason: "PE inválido: \(error)")
        }
        // processo real: memória virtual + CPU + Win32 (pr_peproc)
        if prLog == nil { prLog = pr_log_create(0) }
        let cLog = prLog
        var p: OpaquePointer?
        let st = data.withUnsafeBytes { raw -> pr_status in
            pr_peproc_create(raw.baseAddress, raw.count, cLog, &p)
        }
        guard let p else {
            phase = .failed
            throw RuntimeFailure.payloadInvalid(reason: "falha interna de carga PE (pr_peproc_create)")
        }
        if st != PR_OK {
            let diag = String(cString: pr_peproc_diagnostic(p))
            pr_peproc_destroy(p)
            phase = .failed
            throw RuntimeFailure.payloadInvalid(reason:
                "PE não executável neste build.\n\n\(diag)")
        }
        proc = p
        phase = .loaded
    }

    public func initialize(context: RuntimeSessionContext) throws {
        guard phase == .loaded, let p = proc else {
            throw RuntimeFailure.backendUnavailable(reason: "load() não executado")
        }
        if let img = loadedImage {
            lastCoverage = Win32Catalog.coverage(for: img.report)
        }
        budgetPerFrame = UInt64(max(context.config.maxInstructionsPerFrame, 1))
        // o processo TEM ambiente, diretório de trabalho e linha de comando
        // reais (FASE 2), vindos do contexto de sessão.
        if let w = pr_peproc_win32(p) {
            for (k, v) in context.environmentVariables {
                k.withCString { kn in v.withCString { vn in
                    _ = pr_win32_env_set(w, kn, vn)
                } }
            }
            if !context.profile.caminho.isEmpty {
                context.profile.caminho.withCString { _ = pr_win32_set_cwd(w, $0) }
            }
            // FASE 7: filesystem virtual — raiz ÚNICA = diretório do sandbox
            // da sessão. Caminhos Windows normalizam para dentro dele; ".."
            // e fugas são NEGADOS com log (nunca acessa o filesystem do iOS).
            if !context.profile.caminho.isEmpty {
                context.profile.caminho.withCString { _ = pr_peproc_set_fs_root(p, $0) }
            }
            var cmd = context.profile.executavel
            if !context.profile.argumentos.isEmpty { cmd += " " + context.profile.argumentos }
            if cmd.isEmpty { cmd = "portico" }
            cmd.withCString { _ = pr_win32_set_cmdline(w, $0) }
        }
        // resolução de imports (IAT → thunks stdcall) + proteções finais
        let st = pr_peproc_prepare(p)
        if st != PR_OK {
            let diag = String(cString: pr_peproc_diagnostic(p))
            lastDiagnostic = diag
            var summary = ""
            if let cov = lastCoverage {
                summary = "imports Win32: \(cov.resolved.count) resolvida(s), "
                    + "\(cov.unresolved.count) não resolvida(s) — a camada Win32 é "
                    + "parcial e a funcionalidade ausente NÃO está integrada neste build.\n\n"
            }
            phase = .failed
            throw RuntimeFailure.unsupported(reason:
                "EXECUÇÃO DE WINDOWS INTERROMPIDA NA INICIALIZAÇÃO.\n\n\(summary)\(diag)")
        }
        phase = .initialized
    }

    public func run() throws {
        guard phase == .initialized || phase == .paused || phase == .stopped else {
            throw RuntimeFailure.backendUnavailable(reason:
                "run() inválido na fase \(phase.rawValue) — execute load()/initialize() antes")
        }
        phase = .running
    }

    public func pause() {
        if phase == .running { phase = .paused }
    }

    public func resume() throws {
        guard phase == .paused else {
            throw RuntimeFailure.backendUnavailable(reason:
                "resume() inválido na fase \(phase.rawValue)")
        }
        phase = .running
    }

    /// Ciclo composto real: load + initialize + run.
    public func start(context: RuntimeSessionContext) throws {
        shutdown()
        let arch = context.profile.arquiteturaExe
        let a = arch.lowercased()
        let known = a.contains("x86") || a.contains("x64") || a.contains("i386")
            || a.contains("amd64")
        if !known {
            phase = .failed
            throw RuntimeFailure.unsupported(reason:
                "PE Windows (\(arch)): a execução Win32 nesta arquitetura NÃO está "
                + "integrada neste build (somente x86 e o subconjunto x64 do interpretador).")
        }
        try load(executable: context.executableURL,
                 image: .windowsPE(PEImage.mockForRefusal(arch)))
        try initialize(context: context)
        try run()
    }

    public func stepFrame(input: InputState, dt: Float, timeMs: Double) -> FrameResult {
        guard let p = proc else {
            return FrameResult(state: .failed,
                               failure: .backendUnavailable(reason: "sessão não iniciada"))
        }
        if phase == .paused {
            return FrameResult(state: .running, message: "pausado")
        }
        guard phase == .running else {
            return FrameResult(state: (phase == .stopped) ? .stopped : .failed,
                               framesPresented: lastFrames,
                               halted: phase == .stopped,
                               message: lastDiagnostic, failure: nil)
        }
        var executed: UInt64 = 0
        let st = pr_peproc_step(p, budgetPerFrame, &executed)
        let stop = pr_peproc_state(p)
        if stop.rawValue == PR_PEPROC_STOP_EXIT.rawValue {
            phase = .stopped
            exitCode = pr_peproc_exit_code(p)
            lastDiagnostic = String(cString: pr_peproc_diagnostic(p))
            return FrameResult(state: .stopped, framesPresented: lastFrames,
                               halted: true,
                               message: "PROCESS EXIT (código \(pr_peproc_exit_code(p)))",
                               failure: nil)
        }
        if stop.rawValue != PR_PEPROC_STOP_NONE.rawValue || st != PR_OK {
            phase = .failed
            lastDiagnostic = String(cString: pr_peproc_diagnostic(p))
            return FrameResult(state: .failed, framesPresented: lastFrames,
                               halted: true, message: lastDiagnostic,
                               failure: .guestFault(detail: lastDiagnostic))
        }
        return FrameResult(state: .running, framesPresented: lastFrames,
                           halted: false, message: "", failure: nil)
    }

    public func consumeGraphicsFrame() -> GfxFrame {
        guard let p = proc, let surf = pr_peproc_surface(p) else { return GfxFrame() }
        var frame = GfxFrame()
        let w = pr_surf_width(surf)
        let h = pr_surf_height(surf)
        if surfaceBuffer.update(width: w, height: h, copyFrom: pr_surf_pixels(surf)) {
            frame.surface = surfaceBuffer
            frame.commands = [.present(width: w, height: h)]
            lastFrames += 1
        }
        return frame
    }

    public func pullAudio(maxFrames: Int) -> [Float] {
        // Win32/waveOut ainda não implementado — sem áudio (honesto).
        []
    }

    public func drainLogs(into log: LogCenter) {
        guard let p = proc, let cLog = pr_peproc_log(p) else { return }
        var buf = [pr_log_entry](repeating: pr_log_entry(), count: 64)
        while true {
            let n = buf.withUnsafeMutableBufferPointer { pbuf -> Int in
                pr_log_read(cLog, pbuf.baseAddress, pbuf.count, logOffset)
            }
            if n == 0 { break }
            for i in 0..<n {
                let e = buf[i]
                let level: LogLevel
                switch e.level.rawValue {
                case PR_LOG_DEBUG.rawValue: level = .debug
                case PR_LOG_INFO.rawValue: level = .info
                case PR_LOG_WARN.rawValue: level = .warning
                case PR_LOG_ERROR.rawValue: level = .error
                default: level = .info
                }
                let cat = withUnsafeBytes(of: e.cat) {
                    String(cString: $0.bindMemory(to: CChar.self).baseAddress!)
                }
                let msg = withUnsafeBytes(of: e.msg) {
                    String(cString: $0.bindMemory(to: CChar.self).baseAddress!)
                }
                log.log(level, cat, msg)
            }
            logOffset += UInt64(n)
            if n < buf.count { break }
        }
    }

    public func stop() {
        // mantém o processo p/ inspeção (diagnóstico/exit code); libera no shutdown
        if phase != .idle { phase = .stopped }
    }

    public func shutdown() {
        if let p = proc { pr_peproc_destroy(p) }
        if let l = prLog { pr_log_destroy(l) }
        prLog = nil
        proc = nil
        loadedImage = nil
        lastCoverage = nil
        lastDiagnostic = ""
        exitCode = nil
        logOffset = 0
        lastFrames = 0
        phase = .idle
    }
}

extension PEImage {
    /// Somente para mensagens de recusa (sem arquivo real).
    static func mockForRefusal(_ arch: String) -> PEImage {
        PEImage(isPE32Plus: arch.contains("64"), isDLL: false, machine: 0,
                subsystem: 2, timestamp: 0, imageBase: 0, sizeOfImage: 0,
                entryPointRVA: 0, sectionCount: 0, importCount: 0,
                arch: arch, imports: [], sections: [])
    }
}

/// Registro de backends consultado pelo CompatibilityLayer/RuntimeManager.
public final class BackendRegistry {
    public private(set) var backends: [ExecutionBackend] = []

    public init() {}

    public func register(_ backend: ExecutionBackend) {
        backends.append(backend)
    }

    public func backend(id: String) -> ExecutionBackend? {
        backends.first(where: { $0.id == id })
    }

    /// Escolhe o primeiro backend que aceite a imagem (com veredito/motivo).
    public func select(for image: SoftwareImage) -> (ExecutionBackend, BackendVerdict)? {
        var lastReason = ""
        for b in backends {
            let v = b.canExecute(image)
            if v.canRun { return (b, v) }
            if lastReason.isEmpty { lastReason = v.reason }
        }
        return nil
    }

    public static func standard(log: LogCenter) -> BackendRegistry {
        let r = BackendRegistry()
        r.register(PXPInterpreterBackend())
        r.register(WindowsPEBackend())
        log.info("backends", "registrados: \(r.backends.map(\.id).joined(separator: ", "))")
        return r
    }
}
