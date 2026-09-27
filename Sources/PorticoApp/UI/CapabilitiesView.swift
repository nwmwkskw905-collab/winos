import SwiftUI
import PorticoCore

/// Relatório de capacidades honesto (SUPPORTED / PARTIAL / NOT SUPPORTED).
struct CapabilitiesView: View {
    @EnvironmentObject var model: AppModel
    @State private var report: CapabilityReport?
    @State private var fullText = ""
    @State private var exportURL: URL?

    var body: some View {
        List {
            if let report {
                Section("Ambiente") {
                    LabeledContent("CPU", value: report.cpuBrand)
                    LabeledContent("RAM",
                        value: "\(report.totalRAM / (1024 * 1024)) MiB")
                    LabeledContent("JIT", value: report.jitAvailable ? "disponível" : "indisponível")
                    LabeledContent("W^X", value: report.writeXorExecute ? "sim" : "não")
                    Text(report.environmentNote)
                        .font(.caption)
                        .foregroundStyle(.secondary)
                }
                Section {
                    ForEach(Array(subsystems.enumerated()), id: \.offset) { _, item in
                        VStack(alignment: .leading, spacing: 2) {
                            HStack {
                                Text(statusLabel(item.level))
                                    .font(.caption2.bold())
                                    .padding(.horizontal, 6)
                                    .padding(.vertical, 2)
                                    .background(statusColor(item.level).opacity(0.2),
                                                in: Capsule())
                                Text(item.title).font(.subheadline)
                            }
                            Text(item.explanation)
                                .font(.caption2)
                                .foregroundStyle(.secondary)
                        }
                    }
                } header: {
                    Text("Subsistemas")
                } footer: {
                    Text("SUPPORTED = funcional · PARTIAL = subconjunto real · "
                         + "NÃO SUP. = não integrado (nada é simulado)")
                }
                Section("Recursos") {
                    ForEach(report.features) { f in
                        VStack(alignment: .leading, spacing: 2) {
                            HStack {
                                Text(statusLabel(f.level))
                                    .font(.caption2.bold())
                                    .padding(.horizontal, 6)
                                    .padding(.vertical, 2)
                                    .background(statusColor(f.level).opacity(0.2),
                                                in: Capsule())
                                Text(f.feature.rawValue).font(.subheadline)
                            }
                            if !f.detail.isEmpty {
                                Text(f.detail)
                                    .font(.caption2)
                                    .foregroundStyle(.secondary)
                            }
                        }
                    }
                }
            }
            Section {
                if let url = exportURL {
                    ShareLink(item: url) {
                        Label("Exportar relatório completo", systemImage: "square.and.arrow.up")
                    }
                } else {
                    Button("Gerar relatório completo") { generate() }
                }
            }
        }
        .navigationTitle("Diagnóstico")
        .onAppear {
            report = DiagnosticsReport.capabilities()
            subsystems = DiagnosticsReport.subsystemItems(
                storageNote: "Espaço livre: \(model.sandbox.availableSpaceBytes() / (1024 * 1024)) MiB")
        }
    }

    @State private var subsystems: [DiagnosticsReport.DiagnosticItem] = []

    private func generate() {
        fullText = DiagnosticsReport.fullReport(
            sandbox: model.sandbox,
            library: model.library,
            environments: model.environments,
            log: model.log)
        let url = model.sandbox.logsDir
            .appendingPathComponent("portico-relatorio.txt")
        try? fullText.data(using: .utf8)?.write(to: url)
        exportURL = url
    }

    private func statusLabel(_ level: SupportLevel) -> String {
        switch level {
        case .supported: return "OK"
        case .partial: return "PARCIAL"
        case .notSupported: return "NÃO SUP."
        }
    }

    private func statusColor(_ level: SupportLevel) -> Color {
        switch level {
        case .supported: return .green
        case .partial: return .orange
        case .notSupported: return .red
        }
    }
}
