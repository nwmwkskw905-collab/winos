import SwiftUI
#if !PORTICO_XCODE_MONOLITHIC
import PorticoCore
#endif
@main
struct PorticoApp: App {
    @StateObject private var model = AppModel()

    var body: some Scene {
        WindowGroup {
            RootView()
                .environmentObject(model)
                .preferredColorScheme(model.colorScheme)
                .task { model.bootstrap() }
        }
    }
}

struct RootView: View {
    @EnvironmentObject var model: AppModel
    @State private var useWinOSHome = true

    var body: some View {
        Group {
            if useWinOSHome {
                NavigationStack {
                    WinOSHomeView()
                }
            } else {
                NavigationStack {
                    LibraryView()
                }
            }
        }
        .alert(model.alertTitle, isPresented: $model.showingAlert) {
            Button("OK", role: .cancel) {}
        } message: {
            Text(model.alertMessage)
        }
        .toolbar {
            ToolbarItem(placement: .bottomBar) {
                Button {
                    useWinOSHome.toggle()
                } label: {
                    Label(useWinOSHome ? "Modo Clássico" : "Modo WinOS", systemImage: useWinOSHome ? "rectangle.grid.1x2" : "sparkles.rectangle.stack")
                        .font(.caption2)
                }
                .tint(WinOSBrand.textTertiary)
            }
        }
    }
}
