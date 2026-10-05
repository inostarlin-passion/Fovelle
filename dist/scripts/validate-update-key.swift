import Foundation
import CryptoKit
// Read private material from stdin only; never place it in process arguments or logs.
let seedText = String(data: FileHandle.standardInput.readDataToEndOfFile(), encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines) ?? ""
guard let seed = Data(base64Encoded: seedText), seed.count == 32,
      let key = try? Curve25519.Signing.PrivateKey(rawRepresentation: seed),
      CommandLine.arguments.count == 2,
      key.publicKey.rawRepresentation.base64EncodedString() == CommandLine.arguments[1] else {
    FileHandle.standardError.write(Data("Update signing seed does not match the app public key\n".utf8))
    exit(1)
}
