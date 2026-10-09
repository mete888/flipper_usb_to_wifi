import Foundation

enum NationalTodayExtractor {
    static let sourceURL = "https://nationaltoday.com/today/"
    static let maximumHTMLBytes = 256 * 1_024

    static func handles(_ request: BridgeHTTPRequest) -> Bool {
        guard request.method == .get,
              let components = URLComponents(string: request.urlString) else { return false }
        return components.scheme?.lowercased() == "https" &&
            components.host?.lowercased() == "nationaltoday.com" &&
            components.path == "/today/" && components.query == nil
    }

    static func extract(from data: Data) -> Data? {
        guard let html = String(data: data, encoding: .utf8),
              let marker = html.range(of: "single-date-header-content"),
              let paragraphStart = html.range(of: "<p", range: marker.upperBound..<html.endIndex),
              let contentStart = html.range(of: ">", range: paragraphStart.upperBound..<html.endIndex),
              let paragraphEnd = html.range(of: "</p>", range: contentStart.upperBound..<html.endIndex)
        else { return nil }

        let fragment = String(html[contentStart.upperBound..<paragraphEnd.lowerBound])
            .replacingOccurrences(of: "(?i)<(?:b|strong)(?:\\s[^>]*)?>", with: "[[B]]", options: .regularExpression)
            .replacingOccurrences(of: "(?i)</(?:b|strong)>", with: "[[/B]]", options: .regularExpression)
        let plain = decodeEntities(stripTags(fragment))
            .replacingOccurrences(of: "\u{2014}", with: " - ")
            .replacingOccurrences(of: "\u{2013}", with: "-")
            .replacingOccurrences(of: "\u{2018}", with: "'")
            .replacingOccurrences(of: "\u{2019}", with: "'")
            .replacingOccurrences(of: "\u{201C}", with: "\"")
            .replacingOccurrences(of: "\u{201D}", with: "\"")
            .replacingOccurrences(of: "\u{00A0}", with: " ")
            .split(whereSeparator: { $0.isWhitespace })
            .joined(separator: " ")
        let normalized = plain.folding(options: .diacriticInsensitive, locale: Locale(identifier: "en_US_POSIX"))
        let ascii = normalized.unicodeScalars.filter { (32...126).contains($0.value) }
            .map(String.init).joined()
        guard !ascii.replacingOccurrences(of: "[[B]]", with: "")
            .replacingOccurrences(of: "[[/B]]", with: "")
            .trimmingCharacters(in: .whitespaces).isEmpty else { return nil }
        return Data(ascii.utf8.prefix(BridgeConfiguration.maximumResponseBytes))
    }

    private static func stripTags(_ input: String) -> String {
        var output = ""
        var insideTag = false
        for character in input {
            if character == "<" { insideTag = true }
            else if character == ">" { insideTag = false }
            else if !insideTag { output.append(character) }
        }
        return output
    }

    private static func decodeEntities(_ input: String) -> String {
        var result = input
        let named = [
            "&amp;": "&", "&quot;": "\"", "&#039;": "'", "&apos;": "'",
            "&lt;": "<", "&gt;": ">", "&nbsp;": " ",
            "&#8211;": "-", "&#8212;": " - ", "&#8216;": "'", "&#8217;": "'",
            "&#8220;": "\"", "&#8221;": "\"",
        ]
        for (entity, replacement) in named {
            result = result.replacingOccurrences(of: entity, with: replacement)
        }
        return result
    }
}
