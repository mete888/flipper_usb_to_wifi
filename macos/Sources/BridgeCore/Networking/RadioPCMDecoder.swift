import Foundation
import HostRadioDecoder

/// Single URLSession delegate queue owns this decoder. C callbacks are synchronous.
final class RadioPCMDecoder {
    private let decoder: OpaquePointer
    private var output = Data()

    init?() {
        guard let decoder = fib_radio_decoder_alloc() else { return nil }
        self.decoder = decoder
    }

    deinit { fib_radio_decoder_free(decoder) }

    func feed(_ data: Data) -> Data {
        output.removeAll(keepingCapacity: true)
        data.withUnsafeBytes { bytes in
            fib_radio_decoder_feed(decoder, bytes.bindMemory(to: UInt8.self).baseAddress, data.count,
                { bytes, count, context in
                    guard let bytes, let context else { return }
                    let receiver = Unmanaged<RadioPCMDecoder>.fromOpaque(context).takeUnretainedValue()
                    receiver.output.append(bytes, count: count)
                }, Unmanaged.passUnretained(self).toOpaque())
        }
        return output
    }
}
