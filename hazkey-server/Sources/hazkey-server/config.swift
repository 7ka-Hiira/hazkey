import Foundation
import KanaKanjiConverterModule
import SwiftProtobuf

let KEYMAP_FILE_SIZE_LIMIT = 1024 * 1024  //1MB
let TABLE_FILE_SIZE_LIMIT = 1024 * 1024  //1MB

let builtInKeymaps = [
    "JIS Kana",
    "Japanese Symbol",
    "Fullwidth Period",
    "Fullwidth Comma",
    "Fullwidth Symbol",
    "Fullwidth Number",
    "Fullwidth Space",
].map { name in
    Hazkey_Config_Keymap.with {
        $0.name = name
        $0.isBuiltIn = true
        $0.filename = name
    }
}

let builtInInputTables = [
    "Romaji",
    "Kana",
].map { name in
    Hazkey_Config_InputTable.with {
        $0.name = name
        $0.isBuiltIn = true
        $0.filename = name
    }
}

class HazkeyServerConfig {
    var profiles: [Hazkey_Config_Profile]
    var currentProfile: Hazkey_Config_Profile
    let dictionaryPath: URL
    var zenzaiAvailable: Bool
    var zenzaiModelPath: URL?
    let ggmlBackendDevices: [GGMLBackendDevice]

    init(ggmlBackendDevices: [GGMLBackendDevice]) {
        do {
            profiles = try Self.loadConfig()
        } catch {
            NSLog("Failed to load config: \(error)")
            NSLog("Loading default config...")
            profiles = [HazkeyServerConfig.genDefaultConfig()]
        }

        // TODO: add [0] out of range handling
        currentProfile = profiles[0]

        let fileManager = FileManager()

        // set dictionary path
        dictionaryPath = {
            if let envPath = ProcessInfo.processInfo.environment["HAZKEY_DICTIONARY"],
                fileManager.fileExists(atPath: envPath)
            {
                return URL(filePath: envPath)
            } else {
                return URL(fileURLWithPath: systemResourcePath).appendingPathComponent(
                    "Dictionary", isDirectory: true)
            }
        }()

        // set zenzai model path
        zenzaiModelPath = {
            if ggmlBackendDevices.count == 0 {
                return nil
            }

            let systemZenzaiModelPath = URL(fileURLWithPath: systemResourcePath)
                .appendingPathComponent("zenzai.gguf", isDirectory: false)
            let userZenzaiModelPath = FileManager.default.urls(
                for: .applicationSupportDirectory, in: .userDomainMask
            ).first!
            .appendingPathComponent("hazkey", isDirectory: true)
            .appendingPathComponent("zenzai", isDirectory: true)
            .appendingPathComponent("zenzai.gguf", isDirectory: false)

            if let envPath = ProcessInfo.processInfo.environment["HAZKEY_ZENZAI_MODEL"],
                fileManager.fileExists(atPath: envPath)
            {
                return URL(filePath: envPath)
            } else if fileManager.fileExists(atPath: userZenzaiModelPath.path) {
                return userZenzaiModelPath
            } else if fileManager.fileExists(atPath: systemZenzaiModelPath.path) {
                return systemZenzaiModelPath
            } else {
                return nil
            }
        }()

        self.ggmlBackendDevices = ggmlBackendDevices
        self.zenzaiAvailable = (ggmlBackendDevices.count > 0) && (zenzaiModelPath != nil)
    }

    func getCurrentConfig() -> Hazkey_ResponseEnvelope {
        let profiles: [Hazkey_Config_Profile]
        do {
            profiles = try Self.loadConfig()
        } catch {
            return Hazkey_ResponseEnvelope.with {
                $0.status = .failed
                $0.errorMessage = "\(error)"
            }
        }

        let userKeymapDir = Self.getConfigDirectory().appendingPathComponent(
            "keymap", isDirectory: true
        )
        var keymaps = builtInKeymaps
        do {
            try FileManager.default.createDirectory(
                at: userKeymapDir, withIntermediateDirectories: true)
            let fileURLs = try FileManager.default.contentsOfDirectory(
                at: userKeymapDir,
                includingPropertiesForKeys: [.fileSizeKey],
                options: [.skipsHiddenFiles]
            )

            let keymapFiles = try fileURLs.filter { url in
                guard url.pathExtension.lowercased() == "tsv" else { return false }
                let attrs = try url.resourceValues(forKeys: [.fileSizeKey])
                if let size = attrs.fileSize {
                    return size < KEYMAP_FILE_SIZE_LIMIT
                }
                return false
            }

            for file in keymapFiles {
                keymaps.append(
                    Hazkey_Config_Keymap.with {
                        $0.name = file.deletingPathExtension().lastPathComponent
                        $0.isBuiltIn = false
                        $0.filename = file.lastPathComponent
                    })
            }
        } catch {
            return Hazkey_ResponseEnvelope.with {
                $0.status = .failed
                $0.errorMessage = "Failed to get user keymap files: \(error)"
            }
        }

        let userInputTableDir = Self.getConfigDirectory().appendingPathComponent(
            "table", isDirectory: true
        )
        var inputTables = builtInInputTables
        do {
            try FileManager.default.createDirectory(
                at: userInputTableDir, withIntermediateDirectories: true)
            let fileURLs = try FileManager.default.contentsOfDirectory(
                at: userInputTableDir,
                includingPropertiesForKeys: [.fileSizeKey],
                options: [.skipsHiddenFiles]
            )

            let inputTableFiles = try fileURLs.filter { url in
                guard url.pathExtension.lowercased() == "tsv" else { return false }
                let attrs = try url.resourceValues(forKeys: [.fileSizeKey])
                if let size = attrs.fileSize {
                    return size < TABLE_FILE_SIZE_LIMIT
                }
                return false
            }

            for file in inputTableFiles {
                inputTables.append(
                    Hazkey_Config_InputTable.with {
                        $0.name = file.deletingPathExtension().lastPathComponent
                        $0.isBuiltIn = false
                        $0.filename = file.lastPathComponent
                    })
            }
        } catch {
            return Hazkey_ResponseEnvelope.with {
                $0.status = .failed
                $0.errorMessage = "Failed to get user input table files: \(error)"
            }
        }

        var zenzaiDevices: [Hazkey_Config_BackendDevice] = []
        for devices in ggmlBackendDevices {
            zenzaiDevices.append(
                Hazkey_Config_BackendDevice.with {
                    $0.name = devices.name
                    $0.desc = devices.description
                }
            )
        }

        let currentConfig = Hazkey_Config_CurrentConfig.with {
            $0.fileHashes = []
            $0.zenzaiModelAvailable = zenzaiModelPath != nil
            $0.zenzaiModelPath = zenzaiModelPath?.path ?? ""
            $0.xdgConfigHomePath = Self.getConfigDirectory().absoluteString
            $0.availableKeymaps = keymaps
            $0.availableTables = inputTables
            $0.availableZenzaiBackendDevices = zenzaiDevices
            $0.profiles = profiles
        }
        return Hazkey_ResponseEnvelope.with {
            $0.status = .success
            $0.currentConfig = currentConfig
        }
    }

    func setCurrentConfig(
        _ hashes: [Hazkey_Config_FileHash],
        _ profiles: [Hazkey_Config_Profile],
        state: HazkeyServerState? = nil
    ) -> Hazkey_ResponseEnvelope {
        do {
            try saveConfig(profiles, state: state)
        } catch {
            return Hazkey_ResponseEnvelope.with {
                $0.status = .failed
                $0.errorMessage = "\(error)"
            }
        }

        return Hazkey_ResponseEnvelope.with {
            $0.status = .success
        }
    }

    static func genDefaultConfig() -> Hazkey_Config_Profile {
        var newConf = Hazkey_Config_Profile.init()
        newConf.profileName = "Default"
        newConf.autoConvertMode =
            Hazkey_Config_Profile.AutoConvertMode.autoConvertForMultipleChars
        newConf.auxTextMode = Hazkey_Config_Profile.AuxTextMode.auxTextShowWhenCursorNotAtEnd
        newConf.suggestionListMode =
            Hazkey_Config_Profile.SuggestionListMode.suggestionListShowPredictiveResults
        newConf.numSuggestions = 4
        newConf.useRichSuggestion = false
        newConf.numCandidatesPerPage = 9
        newConf.useRichCandidates = false
        newConf.useInputHistory = true
        newConf.specialConversionMode = Hazkey_Config_Profile.SpecialConversionMode.with {
            $0.commaSeparatedNumber = true
            $0.mailDomain = true
            $0.calendar = true
            $0.time = true
            $0.romanTypography = true
            $0.unicodeCodepoint = true
            $0.hazkeyVersion = true
            $0.halfwidthKatakana = true
            $0.extendedEmoji = true
        }
        newConf.stopStoreNewHistory = false
        newConf.enabledKeymaps = [
            Hazkey_Config_Profile.EnabledKeymap.with {
                $0.name = "Fullwidth Number"
                $0.isBuiltIn = true
                $0.filename = "Fullwidth Number"
            },
            Hazkey_Config_Profile.EnabledKeymap.with {
                $0.name = "Fullwidth Symbol"
                $0.isBuiltIn = true
                $0.filename = "Fullwidth Symbol"
            },
            Hazkey_Config_Profile.EnabledKeymap.with {
                $0.name = "Japanese Symbol"
                $0.isBuiltIn = true
                $0.filename = "Japanese Symbol"
            },
            Hazkey_Config_Profile.EnabledKeymap.with {
                $0.name = "Fullwidth Space"
                $0.isBuiltIn = true
                $0.filename = "Fullwidth Space"
            },
        ]
        newConf.enabledTables = [
            Hazkey_Config_Profile.EnabledInputTable.with {
                $0.name = "Romaji"
                $0.isBuiltIn = true
                $0.filename = "Romaji"
            }
        ]
        newConf.submodeEntryPointChars = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        newConf.zenzaiBackendDeviceName = "CPU"
        newConf.zenzaiEnable = true
        newConf.zenzaiInferLimit = 10
        newConf.zenzaiContextualMode = true
        newConf.zenzaiProfile = ""
        return newConf
    }

    func saveConfig(
        _ newProfiles: [Hazkey_Config_Profile],
        state: HazkeyServerState? = nil
    ) throws {
        let configDir = Self.getConfigDirectory()
        let configPath = configDir.appendingPathComponent("config.json")

        try FileManager.default.createDirectory(
            at: configDir, withIntermediateDirectories: true, attributes: nil)

        var jsonObjects: [Any] = []
        var encodeOptions = JSONEncodingOptions()
        encodeOptions.alwaysPrintEnumsAsInts = true
        encodeOptions.useDeterministicOrdering = true
        for profile in newProfiles {
            let jsonData = try profile.jsonUTF8Data(options: encodeOptions)
            let jsonObject = try JSONSerialization.jsonObject(with: jsonData, options: [])
            jsonObjects.append(jsonObject)
        }

        let jsonData = try JSONSerialization.data(
            withJSONObject: jsonObjects, options: [.prettyPrinted])

        try jsonData.write(to: configPath)

        NSLog("Config saved to: \(configPath.path)")

        profiles = newProfiles
        currentProfile = profiles[0]

        if let state = state {
            state.reinitializeConfiguration()
        }
    }

    static func loadConfig() throws -> [Hazkey_Config_Profile] {
        let configDir = Self.getConfigDirectory()
        let configPath = configDir.appendingPathComponent("config.json")

        // Check if config file exists
        guard FileManager.default.fileExists(atPath: configPath.path) else {
            NSLog("Config file does not exist at: \(configPath.path), returning empty config")
            return [Self.genDefaultConfig()]
        }

        // Read file contents
        let jsonData = try Data(contentsOf: configPath)

        // Parse JSON array
        let jsonArray =
            try JSONSerialization.jsonObject(with: jsonData, options: []) as! [[String: Any]]

        var configs: [Hazkey_Config_Profile] = []
        var decodeOptions = JSONDecodingOptions()
        decodeOptions.ignoreUnknownFields = true
        for jsonObject in jsonArray {
            let jsonObjectData = try JSONSerialization.data(withJSONObject: jsonObject, options: [])
            let config = try Hazkey_Config_Profile(
                jsonUTF8Data: jsonObjectData, options: decodeOptions)
            configs.append(config)
        }

        if configs.count == 0 {
            NSLog("Loaded empty config. returning default config...")
            return [genDefaultConfig()]
        }

        NSLog("Config loaded from: \(configPath.path)")
        return configs
    }

    static func getConfigDirectory() -> URL {
        if let xdgConfigHome = ProcessInfo.processInfo.environment["XDG_CONFIG_HOME"],
            !xdgConfigHome.isEmpty
        {
            return URL(fileURLWithPath: xdgConfigHome).appendingPathComponent("hazkey")
        }

        // Fallback to ~/.config/hazkey
        let homeDir = FileManager.default.homeDirectoryForCurrentUser
        return homeDir.appendingPathComponent(".config").appendingPathComponent("hazkey")
    }

    func genZenzaiMode(leftContext: String)
        -> ConvertRequestOptions.ZenzaiMode
    {
        let deviceName =
            currentProfile.zenzaiBackendDeviceName.isEmpty
            ? "CPU" : currentProfile.zenzaiBackendDeviceName

        if zenzaiAvailable, let zenzaiModelPath = zenzaiModelPath, currentProfile.zenzaiEnable {
            return ConvertRequestOptions.ZenzaiMode.on(
                weight: zenzaiModelPath,
                inferenceLimit: Int(currentProfile.zenzaiInferLimit),
                requestRichCandidates: currentProfile.useRichCandidates,
                personalizationMode: nil,
                versionDependentMode: .v3(
                    ConvertRequestOptions.ZenzaiV3DependentMode.init(
                        profile: currentProfile.zenzaiProfile,
                        topic: currentProfile.zenzaiTopic,
                        style: currentProfile.zenzaiStyle,
                        preference: currentProfile.zenzaiPreference,
                        leftSideContext: currentProfile.zenzaiContextualMode
                            ? leftContext : nil
                    )),
                deviceConfig: createDeviceConfig(deviceName: deviceName)
            )
        } else {
            return ConvertRequestOptions.ZenzaiMode.off
        }
    }

    func genBaseConvertRequestOptions() -> ConvertRequestOptions {
        var userDataDir: URL {
            FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first!
                .appendingPathComponent("hazkey", isDirectory: true)
        }

        let learningType =
            switch (currentProfile.useInputHistory, currentProfile.stopStoreNewHistory) {
            case (true, false):
                LearningType.inputAndOutput
            case (true, true):
                LearningType.onlyOutput
            default:
                LearningType.nothing
            }

        let specialCandidateProviders: [any SpecialCandidateProvider] = {
            let mode = currentProfile.specialConversionMode
            let providers: [SpecialCandidateProvider?] = [
                mode.commaSeparatedNumber ? CommaSeparatedNumberSpecialCandidateProvider() : nil,
                mode.calendar ? CalendarSpecialCandidateProvider() : nil,
                mode.hazkeyVersion ? VersionSpecialCandidateProvider() : nil,
                mode.mailDomain ? EmailAddressSpecialCandidateProvider() : nil,
                mode.romanTypography ? TypographySpecialCandidateProvider() : nil,
                mode.time ? TimeExpressionSpecialCandidateProvider() : nil,
                mode.unicodeCodepoint ? UnicodeSpecialCandidateProvider() : nil,
            ]
            return providers.compactMap { $0 }
        }()

        let zenzaiMode = genZenzaiMode(leftContext: "")

        return ConvertRequestOptions.init(
            N_best: Int(currentProfile.numCandidatesPerPage),
            needTypoCorrection: false,
            requireJapanesePrediction: .disabled,
            requireEnglishPrediction: .disabled,
            keyboardLanguage: .none,
            englishCandidateInRoman2KanaInput: false,
            fullWidthRomanCandidate: true,
            halfWidthKanaCandidate: true,
            learningType: learningType,
            maxMemoryCount: 65536,
            shouldResetMemory: false,
            memoryDirectoryURL: userDataDir.appendingPathComponent(
                "memory", isDirectory: true),
            sharedContainerURL: userDataDir.appendingPathComponent(
                "shared", isDirectory: true),
            textReplacer: .empty,
            specialCandidateProviders: specialCandidateProviders,
            zenzaiMode: zenzaiMode,
            preloadDictionary: false,
            metadata: ConvertRequestOptions.Metadata.init(versionString: "Hazkey 0.2.0")
        )
    }

    func loadKeymap() -> Keymap {
        var maps: Keymap = [:]
        outer: for enabledKeymap in currentProfile.enabledKeymaps.reversed() {
            var newKeymapRule: Keymap
            if enabledKeymap.isBuiltIn {
                switch enabledKeymap.filename {
                case "JIS Kana":
                    newKeymapRule = JISKanaMap
                case "Japanese Symbol":
                    newKeymapRule = japaneseSymbolMap
                case "Fullwidth Period":
                    newKeymapRule = fullwidthPeriodMap
                case "Fullwidth Comma":
                    newKeymapRule = fullwidthCommaMap
                case "Fullwidth Symbol":
                    newKeymapRule = fullwidthSymbolMap
                case "Fullwidth Number":
                    newKeymapRule = fullwidthNumberMap
                case "Fullwidth Space":
                    newKeymapRule = fullwidthSpaceMap
                default:
                    NSLog("Unknown built-in keymap: \(enabledKeymap.name)")
                    continue outer
                }
            } else {
                // load custom keymap
                let customKeymapFile = HazkeyServerConfig.getConfigDirectory()
                    .appendingPathComponent(
                        "keymap", isDirectory: true
                    ).appendingPathComponent(enabledKeymap.filename, isDirectory: false)
                do {
                    let lines = try String(contentsOf: customKeymapFile, encoding: .utf8)
                        .split(separator: "\n")
                        .map { $0.split(separator: "\t") }
                    newKeymapRule = [:]
                    inner: for cols in lines {
                        guard let key = cols[0].first else { continue inner }
                        switch cols.count {
                        case 1:
                            newKeymapRule[key] = nil
                        case 2:
                            newKeymapRule[key] = (cols[1].first!, nil)
                        case 3...:
                            newKeymapRule[key] = (cols[1].first!, cols[2].first)
                        default:
                            NSLog("Unknown columns count: \(cols.count)")
                            continue inner
                        }
                    }
                } catch {
                    NSLog(
                        "Failed to load custom keymap \(enabledKeymap.name): \(error)"
                    )
                    continue outer
                }
            }
            maps.merge(newKeymapRule) { (_, second) in second }
        }

        return maps
    }

    func loadInputTable(tableName: String) {
        var tables: [InputTable] = [compositionSeparatorTable]
        outer: for enabledTable in currentProfile.enabledTables.reversed() {
            let tableToAdd: InputTable
            if enabledTable.isBuiltIn {
                switch enabledTable.filename {
                case "Romaji":
                    tableToAdd = romajiTable
                case "Kana":
                    tableToAdd = kanaTable
                default:
                    debugLog("Unknown built-in input table: \(enabledTable.name)")
                    continue outer
                }
            } else {
                // load custom table
                let customTableFile = HazkeyServerConfig.getConfigDirectory()
                    .appendingPathComponent(
                        "table", isDirectory: true
                    ).appendingPathComponent(enabledTable.filename, isDirectory: false)
                do {
                    tableToAdd = try InputStyleManager.loadTable(from: customTableFile)
                } catch {
                    NSLog("Failed to load custom table \(enabledTable.name)Q \(error)")
                    continue outer
                }
            }
            tables.append(tableToAdd)
        }

        let inputTable = InputTable(tables: tables, order: InputTable.Ordering.lastInputWins)
        InputStyleManager.registerInputStyle(table: inputTable, for: tableName)
    }

    func getSubModeEntryPointChars() -> [Character] {
        return Array(currentProfile.submodeEntryPointChars)
    }

    func reloadZenzaiModel() {
        let fileManager = FileManager()

        let systemZenzaiModelPath = URL(fileURLWithPath: systemResourcePath)
            .appendingPathComponent("zenzai.gguf", isDirectory: false)
        let userZenzaiModelPath = FileManager.default.urls(
            for: .applicationSupportDirectory, in: .userDomainMask
        ).first!
        .appendingPathComponent("hazkey", isDirectory: true)
        .appendingPathComponent("zenzai", isDirectory: true)
        .appendingPathComponent("zenzai.gguf", isDirectory: false)

        zenzaiModelPath = {
            if ggmlBackendDevices.count == 0 {
                return nil
            }

            if let envPath = ProcessInfo.processInfo.environment["HAZKEY_ZENZAI_MODEL"],
                fileManager.fileExists(atPath: envPath)
            {
                return URL(filePath: envPath)
            } else if fileManager.fileExists(atPath: userZenzaiModelPath.path) {
                return userZenzaiModelPath
            } else if fileManager.fileExists(atPath: systemZenzaiModelPath.path) {
                return systemZenzaiModelPath
            } else {
                return nil
            }
        }()

        zenzaiAvailable = (ggmlBackendDevices.count > 0) && (zenzaiModelPath != nil)
    }
}
