import SwiftUI
import PorticoCore

/// Editor de controles: posição, tamanho, transparência, função, salvar.
struct ControlEditorView: View {
    @Environment(\.dismiss) private var dismiss
    @Binding var profile: ControlProfile
    @State private var selectedID: UUID?
    @State private var working: ControlProfile = ControlProfile()

    var body: some View {
        NavigationStack {
            VStack(spacing: 0) {
                // Canvas de edição (área de jogo 16:9)
                GeometryReader { geo in
                    ZStack(alignment: .topLeading) {
                        RoundedRectangle(cornerRadius: 12)
                            .fill(Color.black.opacity(0.85))
                        ForEach($working.elements) { $el in
                            ControlElementView(element: el,
                                               isSelected: selectedID == el.id,
                                               canvasSize: geo.size)
                                .gesture(dragGesture(for: $el, canvas: geo.size))
                                .onTapGesture { selectedID = el.id }
                        }
                    }
                    .padding()
                }
                .aspectRatio(16.0 / 9.0, contentMode: .fit)

                Divider()

                if let sel = selection {
                    inspector(for: sel)
                } else {
                    Text("Toque em um controle para editar")
                        .font(.footnote)
                        .foregroundStyle(.secondary)
                        .padding()
                }

                HStack {
                    Button {
                        addElement()
                    } label: {
                        Label("Adicionar", systemImage: "plus.circle")
                    }
                    Spacer()
                    Toggle("Mostrar controles", isOn: $working.showTouchControls)
                        .labelsHidden()
                    Text("Visíveis").font(.caption)
                }
                .padding()
            }
            .navigationTitle("Editor de controles")
            .navigationBarTitleDisplayMode(.inline)
            .toolbar {
                ToolbarItem(placement: .cancellationAction) {
                    Button("Cancelar") { dismiss() }
                }
                ToolbarItem(placement: .confirmationAction) {
                    Button("Salvar") {
                        profile = working
                        dismiss()
                    }
                }
                ToolbarItem(placement: .destructive) {
                    if selectedID != nil {
                        Button {
                            removeSelected()
                        } label: {
                            Image(systemName: "trash")
                        }
                    }
                }
            }
            .onAppear { working = profile }
        }
    }

    private var selection: Binding<ControlElement>? {
        guard let sel = working.elements.firstIndex(where: { $0.id == selectedID })
        else { return nil }
        return Binding(
            get: { working.elements[sel] },
            set: { working.elements[sel] = $0 }
        )
    }

    private func inspector(for sel: Binding<ControlElement>) -> some View {
        Form {
            Section("Elemento") {
                Picker("Tipo", selection: sel.kind) {
                    ForEach(ControlKind.allCases, id: \.self) { k in
                        Text(k.displayName).tag(k)
                    }
                }
                Picker("Função", selection: sel.action) {
                    ForEach(InputAction.allCases, id: \.self) { a in
                        Text(a.displayName).tag(a)
                    }
                }
                TextField("Rótulo", text: sel.label)
            }
            Section("Aparência") {
                Slider(value: sel.opacity, in: 0.05...1) {
                    Text("Opacidade \(Int(sel.wrappedValue.opacity * 100))%")
                }
                HStack {
                    Text("Tamanho")
                    Slider(value: sel.frame.wrappedValue.wBinding,
                           in: 0.05...0.5)
                }
            }
        }
        .frame(height: 260)
    }

    private func addElement() {
        let el = ControlElement(
            kind: .faceButton,
            action: .buttonA,
            frame: NormalizedRect(x: 0.45, y: 0.45, w: 0.1, h: 0.16))
        working.elements.append(el)
        selectedID = el.id
    }

    private func removeSelected() {
        working.elements.removeAll { $0.id == selectedID }
        selectedID = nil
    }

    private func dragGesture(for el: Binding<ControlElement>,
                             canvas: CGSize) -> some Gesture {
        DragGesture()
            .onChanged { value in
                var f = el.frame.wrappedValue
                f.x = Double(value.location.x / canvas.width) - f.w / 2
                f.y = Double(value.location.y / canvas.height) - f.h / 2
                el.frame.wrappedValue = f
            }
    }
}

extension NormalizedRect {
    var wBinding: Binding<Double> {
        Binding(get: { w }, set: { w = $0 })
    }
}

/// Representação visual de um controle no editor e no overlay.
struct ControlElementView: View {
    let element: ControlElement
    let isSelected: Bool
    let canvasSize: CGSize

    var body: some View {
        let x = element.frame.x * canvasSize.width
        let y = element.frame.y * canvasSize.height
        let w = element.frame.w * canvasSize.width
        let h = element.frame.h * canvasSize.height

        ZStack {
            shape
                .stroke(Color(element.colorHex).opacity(element.opacity),
                        lineWidth: isSelected ? 3 : 1.5)
            shape
                .fill(Color(element.colorHex).opacity(element.opacity * 0.35))
            Text(element.label)
                .font(.system(size: min(w, h) * 0.32, weight: .bold))
                .foregroundStyle(.white.opacity(element.opacity))
        }
        .frame(width: w, height: h)
        .position(x: x + w / 2, y: y + h / 2)
        .opacity(isSelected ? 1.0 : 0.95)
    }

    private var shape: AnyShape {
        switch element.kind {
        case .faceButton, .startButton, .selectButton:
            return AnyShape(Circle())
        case .analogStick, .dPad:
            return AnyShape(Circle())
        case .shoulder, .trigger:
            return AnyShape(RoundedRectangle(cornerRadius: 8))
        }
    }
}

extension Color {
    init(_ hex: String) {
        var value: UInt64 = 0
        let cleaned = hex.trimmingCharacters(in: CharacterSet(charactersIn: "#"))
        Scanner(string: cleaned).scanHexInt64(&value)
        self.init(red: Double((value >> 16) & 0xFF) / 255.0,
                  green: Double((value >> 8) & 0xFF) / 255.0,
                  blue: Double(value & 0xFF) / 255.0)
    }
}
