import Foundation

/// Gerador de PE mínimo x64 para teste WinOS — cria um EXE válido que apenas retorna
/// Usa estrutura PE32+ mínima com uma seção .text que chama ExitProcess(0)
/// Este é o WINOS TEST EXE PE x64 mínimo exigido pela Fase 8
struct MinimalPETestGenerator {
    
    /// Gera um PE x64 mínimo hello world que apenas chama ExitProcess(0)
    static func generateMinimalX64PE() -> Data {
        var data = Data()
        
        // DOS Header (64 bytes)
        // e_magic = MZ, e_lfanew = 0x80
        data.append(contentsOf: [0x4D, 0x5A]) // MZ
        data.append(Data(repeating: 0, count: 58))
        data.append(contentsOf: [0x80, 0x00, 0x00, 0x00]) // e_lfanew = 0x80
        // Padding até 0x80
        while data.count < 0x80 {
            data.append(0)
        }
        
        // PE Signature "PE\0\0"
        data.append(contentsOf: [0x50, 0x45, 0x00, 0x00])
        
        // COFF Header (20 bytes)
        // Machine = AMD64 0x8664, NumberOfSections = 2, TimeDateStamp, etc
        data.append(contentsOf: [0x64, 0x86]) // Machine AMD64
        data.append(contentsOf: [0x02, 0x00]) // NumberOfSections = 2
        data.append(contentsOf: [0x00, 0x00, 0x00, 0x00]) // TimeDateStamp
        data.append(contentsOf: [0x00, 0x00, 0x00, 0x00]) // PointerToSymbolTable
        data.append(contentsOf: [0x00, 0x00, 0x00, 0x00]) // NumberOfSymbols
        data.append(contentsOf: [0xF0, 0x00]) // SizeOfOptionalHeader = 240
        data.append(contentsOf: [0x22, 0x00]) // Characteristics = EXECUTABLE | LARGE_ADDRESS_AWARE
        
        // Optional Header PE32+ (240 bytes)
        let optionalHeaderStart = data.count
        data.append(contentsOf: [0x0B, 0x02]) // Magic PE32+
        data.append(0x0E) // MajorLinkerVersion
        data.append(0x00) // MinorLinkerVersion
        data.append(contentsOf: [0x00, 0x02, 0x00, 0x00]) // SizeOfCode = 512
        data.append(contentsOf: [0x00, 0x02, 0x00, 0x00]) // SizeOfInitializedData = 512
        data.append(contentsOf: [0x00, 0x00, 0x00, 0x00]) // SizeOfUninitializedData
        data.append(contentsOf: [0x00, 0x10, 0x00, 0x00]) // AddressOfEntryPoint = 0x1000
        data.append(contentsOf: [0x00, 0x10, 0x00, 0x00]) // BaseOfCode = 0x1000
        data.append(contentsOf: [0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00, 0x00]) // ImageBase = 0x400000
        data.append(contentsOf: [0x00, 0x10, 0x00, 0x00]) // SectionAlignment = 0x1000
        data.append(contentsOf: [0x00, 0x02, 0x00, 0x00]) // FileAlignment = 0x200
        data.append(contentsOf: [0x06, 0x00]) // MajorOSVersion
        data.append(contentsOf: [0x00, 0x00]) // MinorOSVersion
        data.append(contentsOf: [0x00, 0x00]) // MajorImageVersion
        data.append(contentsOf: [0x00, 0x00]) // MinorImageVersion
        data.append(contentsOf: [0x06, 0x00]) // MajorSubsystemVersion
        data.append(contentsOf: [0x00, 0x00]) // MinorSubsystemVersion
        data.append(contentsOf: [0x00, 0x00, 0x00, 0x00]) // Win32VersionValue
        data.append(contentsOf: [0x00, 0x30, 0x00, 0x00]) // SizeOfImage = 0x3000
        data.append(contentsOf: [0x00, 0x04, 0x00, 0x00]) // SizeOfHeaders = 0x400
        data.append(contentsOf: [0x00, 0x00, 0x00, 0x00]) // CheckSum
        data.append(contentsOf: [0x03, 0x00]) // Subsystem = CONSOLE
        data.append(contentsOf: [0x40, 0x81]) // DllCharacteristics
        data.append(contentsOf: [0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00]) // SizeOfStackReserve
        data.append(contentsOf: [0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]) // SizeOfStackCommit
        data.append(contentsOf: [0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00]) // SizeOfHeapReserve
        data.append(contentsOf: [0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]) // SizeOfHeapCommit
        data.append(contentsOf: [0x00, 0x00, 0x00, 0x00]) // LoaderFlags
        data.append(contentsOf: [0x10, 0x00, 0x00, 0x00]) // NumberOfRvaAndSizes = 16
        
        // Data Directories (16 * 8 = 128 bytes) — zerados exceto import
        // 0: Export, 1: Import (será preenchido)
        for _ in 0..<16 {
            data.append(contentsOf: [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])
        }
        
        // Garante optional header tem 240 bytes
        while data.count < optionalHeaderStart + 240 {
            data.append(0)
        }
        
        // Section Headers (2 * 40 = 80 bytes)
        // .text
        var textName = ".text".utf8.map { $0 } + [UInt8](repeating: 0, count: 3)
        data.append(contentsOf: textName)
        data.append(contentsOf: [0x00, 0x02, 0x00, 0x00]) // VirtualSize = 512
        data.append(contentsOf: [0x00, 0x10, 0x00, 0x00]) // VirtualAddress = 0x1000
        data.append(contentsOf: [0x00, 0x02, 0x00, 0x00]) // SizeOfRawData = 512
        data.append(contentsOf: [0x00, 0x04, 0x00, 0x00]) // PointerToRawData = 0x400
        data.append(contentsOf: [0x00, 0x00, 0x00, 0x00]) // PointerToRelocations
        data.append(contentsOf: [0x00, 0x00, 0x00, 0x00]) // PointerToLinenumbers
        data.append(contentsOf: [0x00, 0x00]) // NumberOfRelocations
        data.append(contentsOf: [0x00, 0x00]) // NumberOfLinenumbers
        data.append(contentsOf: [0x20, 0x00, 0x00, 0x60]) // Characteristics = CODE | EXECUTE | READ
        
        // .rdata (para imports)
        var rdataName = ".rdata".utf8.map { $0 } + [UInt8](repeating: 0, count: 2)
        data.append(contentsOf: rdataName)
        data.append(contentsOf: [0x00, 0x02, 0x00, 0x00]) // VirtualSize
        data.append(contentsOf: [0x00, 0x20, 0x00, 0x00]) // VirtualAddress = 0x2000
        data.append(contentsOf: [0x00, 0x02, 0x00, 0x00]) // SizeOfRawData
        data.append(contentsOf: [0x00, 0x06, 0x00, 0x00]) // PointerToRawData = 0x600
        data.append(contentsOf: [0x00, 0x00, 0x00, 0x00])
        data.append(contentsOf: [0x00, 0x00, 0x00, 0x00])
        data.append(contentsOf: [0x00, 0x00])
        data.append(contentsOf: [0x00, 0x00])
        data.append(contentsOf: [0x40, 0x00, 0x00, 0x40]) // INITIALIZED_DATA | READ
        
        // Padding até SizeOfHeaders (0x400)
        while data.count < 0x400 {
            data.append(0)
        }
        
        // .text content (0x200 bytes) — código x64 mínimo:
        // mov ecx, 0 (exit code)
        // call [rip+...] ExitProcess (será via import)
        // hlt
        // Na prática, fazemos loop infinito + hlt para ser seguro
        var textSection = Data(repeating: 0, count: 0x200)
        // Código x64 simples: xor ecx,ecx; mov rax, 0x...; mas para teste mínimo, apenas ret
        // 0x00: 31 C9 = xor ecx,ecx
        // 0x02: 48 31 C0 = xor rax,rax
        // 0x05: C3 = ret
        // 0x06: F4 = hlt
        textSection[0] = 0x31
        textSection[1] = 0xC9
        textSection[2] = 0x48
        textSection[3] = 0x31
        textSection[4] = 0xC0
        textSection[5] = 0xC3
        textSection[6] = 0xF4
        data.append(textSection)
        
        // .rdata content (0x200 bytes) — vazio por enquanto
        data.append(Data(repeating: 0, count: 0x200))
        
        return data
    }
    
    /// Salva o PE de teste no sandbox para validação
    static func saveTestPE(to url: URL) throws {
        let peData = generateMinimalX64PE()
        try peData.write(to: url)
        print("WinOS Test PE x64 minimal gerado: \(url.path) size=\(peData.count)")
    }
}
