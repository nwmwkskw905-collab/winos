import SwiftUI
import PorticoCore

/// Visualização de logs: filtro por nível, limpar, exportar (quando permitido).
struct LogsView: View {
    @EnvironmentObject var model: AppModel
    @Environment(\.dismiss) private var dismiss
    @State private var minLevel: LogLevel = .debug
    @State private var entries: [LogEntry] = []
    @State private var exportURL: URL?

    var body: some View {
        List {
            Section {
                Picker("Nível mínimo", selection: $minLevel) {
                    ForEach(LogLevel.allCases, id: \.rawValue) { l in
                        Text(l.label).tag(l)
                    }
                }
                .pickerStyle(.segmented)
            }
            Section("Entradas (\(entries.count))") {
                ForEach(entries) { e in
                    VStack(alignment: .leading, spacing: 2) {
                        HStack {
                            Text(e.level.label)
                                .font(.caption2.bold())
                                .foregroundStyle(color(for: e.level))
                            Text("[\(e.category)]")
                                .font(.caption2.monospaced())
                                .foregroundStyle(.secondary)
                            Spacer()
                            Text(e.timestamp.formatted(date: .omitted, time: .standard))
                                .font(.caption2)
                                .foregroundStyle(.secondary)
                        }
                        Text(e.message)
                            .font(.caption.monospaced())
                    }
                }
            }
        }
        .navigationTitle("Logs")
        .toolbar {
            ToolbarItem(placement: .primaryAction) {
                Menu {
                    Button("Atualizar") { refresh() }
                    Button("Limpar logs", role: .destructive) {
                        model.log.clear()
                        refresh()
                    }
                    if let url = exportURL {
                        ShareLink(item: url) {
                            Label("Exportar log", systemImage: "square.and.arrow.up")
                        }
                    } else {
                        Button {
                            export()
                        } label: {
                            Label("Exportar log", systemImage: "square.and.arrow.up")
                        }
                    }
                } label: {
                    Image(systemName: "ellipsis.circle")
                }
            }
            ToolbarItem(placement: .cancellationAction) {
                Button("Fechar") { dismiss() }
            }
        }
        .onAppear(perform: refresh)
        .onChange(of: minLevel) { _, _ in refresh() }
    }

    private func refresh() {
        entries = model.log.snapshot(minLevel: minLevel).reversed()
    }

    private func export() {
        do {
            exportURL = try model.log.exportTo(directory: model.sandbox.logsDir)
        } catch {
            model.present(title: "Falha ao exportar", message: "\(error)")
        }
    }

    private func color(for level: LogLevel) -> Color {
        switch level {
        case .debug: return .gray
        case .info: return .blue
        case .warning: return .orange
        case .error: return .red
        }
    }
}
