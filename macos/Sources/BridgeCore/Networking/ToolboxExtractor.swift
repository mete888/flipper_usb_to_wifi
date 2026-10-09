import Foundation
import CoreFoundation

enum ToolboxExtractor {
    enum Kind { case earthquakes, dictionary, currency }
    static let maximumSourceBytes = 64 * 1_024
    static let maximumTextBytes = 1_400
    static let maximumEarthquakes = 10

    static func kind(for request: BridgeHTTPRequest) -> Kind? {
        guard request.method == .get,
              let url = URLComponents(string: request.urlString),
              url.scheme?.lowercased() == "https" else { return nil }
        switch url.host?.lowercased() {
        case "earthquake.usgs.gov":
            return url.path == "/fdsnws/event/1/query" ? .earthquakes : nil
        case "api.dictionaryapi.dev":
            return matches(url.percentEncodedPath, "^/api/v2/entries/en/[a-zA-Z%'-]+$") ? .dictionary : nil
        case "api.frankfurter.dev":
            return matches(url.path, "^/v2/rate/[A-Z]{3}/[A-Z]{3}$") ? .currency : nil
        default: return nil
        }
    }

    static func extract(_ kind: Kind, from data: Data) -> Data? {
        guard data.count <= maximumSourceBytes,
              let json = try? JSONSerialization.jsonObject(with: data) else { return nil }
        let result: String?
        switch kind {
        case .earthquakes: result = earthquakes(json)
        case .dictionary: result = dictionary(json)
        case .currency: result = currency(json)
        }
        guard let result, result.utf8.count <= maximumTextBytes else { return nil }
        return Data(result.utf8)
    }

    private static func matches(_ text: String, _ pattern: String) -> Bool {
        text.range(of: pattern, options: .regularExpression) != nil
    }

    private static func text(_ value: Any?, limit: Int) -> String {
        guard var input = value as? String else { return "" }
        for (source, target) in [("—", "-"), ("–", "-"), ("’", "'"), ("‘", "'"), ("“", "\""), ("”", "\"")] {
            input = input.replacingOccurrences(of: source, with: target)
        }
        let normalized = input.folding(options: .diacriticInsensitive, locale: Locale(identifier: "en_US_POSIX"))
        // Remove UTF-8 and escape/control sequences before they reach the FAP.
        let ascii = normalized.unicodeScalars.compactMap { scalar -> String? in
            if scalar.value > 127 { return nil }
            return (32...126).contains(scalar.value) ? String(scalar) : " "
        }.joined()
        return String(ascii.split(whereSeparator: { $0.isWhitespace }).joined(separator: " ").prefix(limit))
    }

    private static func number(_ value: Any?) -> Double? {
        guard let n = value as? NSNumber,
              CFGetTypeID(n) != CFBooleanGetTypeID(), n.doubleValue.isFinite else { return nil }
        return n.doubleValue
    }

    private static func decimal(_ value: Double, places: Int) -> String {
        String(format: "%.*f", locale: Locale(identifier: "en_US_POSIX"), places, value)
    }

    private static func currency(_ json: Any) -> String? {
        guard let data = json as? [String: Any], let base = data["base"] as? String,
              let quote = data["quote"] as? String, let date = data["date"] as? String,
              matches(base, "^[A-Z]{3}$"), matches(quote, "^[A-Z]{3}$"),
              matches(date, "^[0-9]{4}-[0-9]{2}-[0-9]{2}$"),
              let rate = number(data["rate"]), rate > 0, rate <= 1e9 else { return nil }
        let formatter = DateFormatter()
        formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.timeZone = TimeZone(secondsFromGMT: 0)
        formatter.dateFormat = "yyyy-MM-dd"
        formatter.isLenient = false
        guard let day = formatter.date(from: date), formatter.string(from: day) == date else { return nil }
        return "FIBRATE1\n\(base)\t\(quote)\t\(decimal(rate, places: 12))\t\(date)\n"
    }

    private static func earthquakes(_ json: Any) -> String? {
        guard let data = json as? [String: Any], let features = data["features"] as? [Any] else { return nil }
        var events = [(time: Double, magnitude: Double?, depth: Double, place: String)]()
        for raw in features {
            guard let feature = raw as? [String: Any],
                  let properties = feature["properties"] as? [String: Any],
                  properties["type"] as? String == "earthquake",
                  let geometry = feature["geometry"] as? [String: Any],
                  let coordinates = geometry["coordinates"] as? [Any], coordinates.count >= 3,
                  let depth = number(coordinates[2]), let time = number(properties["time"]),
                  time >= 0, time <= 4_102_444_800_000 else { continue }
            let place = text(properties["place"], limit: 90)
            events.append((time, number(properties["mag"]), depth, place.isEmpty ? "Unknown location" : place))
        }
        events.sort { $0.time > $1.time }
        let clock = DateFormatter()
        clock.locale = Locale(identifier: "en_US_POSIX")
        clock.timeZone = TimeZone(secondsFromGMT: 0)
        clock.dateFormat = "MM-dd HH:mm 'UTC'"
        let sections = events.prefix(maximumEarthquakes).map { event -> String in
            let magnitude = event.magnitude.map { decimal($0, places: 1) } ?? "N/A"
            let stamp = clock.string(from: Date(timeIntervalSince1970: event.time / 1000))
            return "M \(magnitude) - \(event.place)\n\(stamp)\nDepth: \(decimal(event.depth, places: 1)) km"
        }
        let body = sections.isEmpty ? "No recent earthquakes reported." : sections.joined(separator: "\n\n")
        return "FIBTOOLS1\n" + body + "\n\nSource: USGS"
    }

    private static func dictionary(_ json: Any) -> String? {
        guard let entries = json as? [Any], let entry = entries.first as? [String: Any],
              let meanings = entry["meanings"] as? [Any] else { return nil }
        let word = text(entry["word"], limit: 48)
        var sections = [word]
        for raw in meanings {
            guard let meaning = raw as? [String: Any], let definitions = meaning["definitions"] as? [Any] else { continue }
            for item in definitions {
                guard let definition = item as? [String: Any] else { continue }
                let value = text(definition["definition"], limit: 200)
                if value.isEmpty { continue }
                let part = text(meaning["partOfSpeech"], limit: 24)
                var section = (part.isEmpty ? "Meaning" : part) + ": " + value
                let example = text(definition["example"], limit: 80)
                if !example.isEmpty { section += "\nExample: " + example }
                sections.append(section)
                break
            }
            if sections.count >= 4 { break }
        }
        guard !word.isEmpty, sections.count > 1 else { return nil }
        let license = entry["license"] as? [String: Any]
        let sources = entry["sourceUrls"] as? [Any]
        let attribution = sources.flatMap { $0.first }.map { text($0, limit: 120) } ?? "Wiktionary via Free Dictionary API"
        return "FIBTOOLS1\n" + sections.joined(separator: "\n\n") + "\n\nSource: " + attribution +
            "\n" + text(license?["name"], limit: 40) + "\n" + text(license?["url"], limit: 90) + "\nShortened ASCII text"
    }
}
