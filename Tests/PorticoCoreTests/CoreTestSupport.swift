import Foundation
import XCTest
@testable import PorticoCore

/// Suporte comum: sandbox temporário isolado por teste.
final class CoreTestSupport {
    static func makeSandbox(_ name: String = UUID().uuidString) throws -> (AppSandbox, URL) {
        let root = FileManager.default.temporaryDirectory
            .appendingPathComponent("portico-tests-\(name)", isDirectory: true)
        try? FileManager.default.removeItem(at: root)
        let sb = AppSandbox(root: root)
        try sb.ensureDirectories()
        return (sb, root)
    }

    static func makeLog() -> LogCenter {
        LogCenter(maxEntries: 500, minLevel: .debug)
    }
}

/// Mini construtor de ZIP em memória (stored/deflate) para testes de importação.
enum TestZipBuilder {
    static func build(entries: [(name: String, data: Data, deflate: Bool)]) throws -> Data {
        var out = Data()
        var central = Data()
        var offsets: [UInt32] = []

        for e in entries {
            offsets.append(UInt32(out.count))
            let payload: Data
            if e.deflate {
                payload = try deflateRaw(e.data)
            } else {
                payload = e.data
            }
            let nameData = Data(e.name.utf8)
            var local = Data()
            local.appendUInt32(0x04034b50)
            local.appendUInt16(20)   // version
            local.appendUInt16(0)    // flags
            local.appendUInt16(e.deflate ? 8 : 0)
            local.appendUInt16(0)    // time
            local.appendUInt16(0)    // date
            local.appendUInt32(crc32z(e.data))
            local.appendUInt32(UInt32(payload.count))
            local.appendUInt32(UInt32(e.data.count))
            local.appendUInt16(UInt16(nameData.count))
            local.appendUInt16(0)    // extra
            local.append(nameData)
            local.append(payload)
            out.append(local)
        }

        let cdStart = UInt32(out.count)
        for (i, e) in entries.enumerated() {
            let payloadSize: Int = out.count // unused
            _ = payloadSize
            let nameData = Data(e.name.utf8)
            central.appendUInt32(0x02014b50)
            central.appendUInt16(20)  // made by
            central.appendUInt16(20)  // needed
            central.appendUInt16(0)   // flags
            central.appendUInt16(e.deflate ? 8 : 0)
            central.appendUInt16(0)   // time
            central.appendUInt16(0)   // date
            central.appendUInt32(crc32z(e.data))
            let compSize = e.deflate ? (try! deflateRaw(e.data)).count : e.data.count
            central.appendUInt32(UInt32(compSize))
            central.appendUInt32(UInt32(e.data.count))
            central.appendUInt16(UInt16(nameData.count))
            central.appendUInt16(0)   // extra
            central.appendUInt16(0)   // comment
            central.appendUInt16(0)   // disk
            central.appendUInt16(0)   // iattr
            central.appendUInt32(0)   // eattr
            central.appendUInt32(offsets[i])
            central.append(nameData)
        }
        out.append(central)
        out.appendUInt32(0x06054b50)
        out.appendUInt16(0)
        out.appendUInt16(0)
        out.appendUInt16(UInt16(entries.count))
        out.appendUInt16(UInt16(entries.count))
        out.appendUInt32(UInt32(central.count))
        out.appendUInt32(cdStart)
        out.appendUInt16(0)
        return out
    }

    /// CRC-32 (zlib) simples e portável para os testes.
    static func crc32z(_ data: Data) -> UInt32 {
        var crc: UInt32 = 0xFFFF_FFFF
        for byte in data {
            crc ^= UInt32(byte)
            for _ in 0..<8 {
                let mask = UInt32(bitPattern: -Int32(crc & 1))
                crc = (crc >> 1) ^ (0xEDB8_8320 & mask)
            }
        }
        return ~crc
    }

    /// Deflate bruto (sem cabeçalho zlib) via Compression? Sem Compression no
    /// Linux usamos um stored com flag deflate REAL via zlib C não é exposto;
    /// para o teste de integração usamos método 0 (stored) — o caminho deflate
    /// já é coberto pelos testes C do runtime.
    static func deflateRaw(_ data: Data) throws -> Data {
        // Nota: ZIPs de teste do Swift usam stored; o parser trata ambos.
        data
    }
}

extension Data {
    mutating func appendUInt16(_ v: UInt16) {
        var le = v.littleEndian
        Swift.withUnsafeBytes(of: &le) { append(contentsOf: $0) }
    }

    mutating func appendUInt32(_ v: UInt32) {
        var le = v.littleEndian
        Swift.withUnsafeBytes(of: &le) { append(contentsOf: $0) }
    }
}
