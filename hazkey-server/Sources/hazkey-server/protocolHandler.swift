import Foundation
import SwiftProtobuf

class ProtocolHandler {
    private let state: HazkeyServerState

    init(state: HazkeyServerState) {
        self.state = state
    }

    func resetSession() {
        state.resetSession()
    }

    func processProto(data: Data) -> Data {
        let query: Hazkey_RequestEnvelope
        let response: Hazkey_ResponseEnvelope

        do {
            query = try Hazkey_RequestEnvelope(serializedBytes: data)
        } catch {
            NSLog("Failed to parse protobuf: \(error)")
            response = Hazkey_ResponseEnvelope.with {
                $0.status = .failed
                $0.errorMessage = "Failed to parse protobuf: \(error)"
            }
            return serializeResult(unserialized: response)
        }
        switch query.payload {
        case .keyEvent(let req):
            let clientState = state.handle(event: req)
            response = Hazkey_ResponseEnvelope.with {
                $0.status = .success
                $0.clientState = clientState
            }
        case .saveLearningData:
            state.saveLearningData()
            response = Hazkey_ResponseEnvelope.with {
                $0.status = .success
            }
        case .resetState(let req):
            if req.completedPreedit {
                state.commitAllPreedit()
            }
            state.resetSession()
            response = Hazkey_ResponseEnvelope.with {
                $0.status = .success
            }
        case .getConfig:
            do {
            let config = try state.serverConfig.getCurrentConfig()
            response = Hazkey_ResponseEnvelope.with {
                $0.status = .success
                $0.currentConfig = config
            }
            } catch {
            NSLog("Failed to get config: \(error)")
            response = Hazkey_ResponseEnvelope.with {
                $0.status = .failed
                $0.errorMessage = "Failed to get config: \(error)"
            }
            }
        case .setConfig(let req):
            response = state.serverConfig.setCurrentConfig(
                req.fileHashes, req.profiles, state: state)
        case .clearAllHistory_p:
            state.clearProfileLearningData()
            response = Hazkey_ResponseEnvelope.with {
                $0.status = .success
            }
        case .reloadZenzaiModel:
            state.serverConfig.reloadZenzaiModel()
            response = Hazkey_ResponseEnvelope.with {
                $0.status = .success
            }
        case .getDefaultProfile:
            NSLog("Unimplemented: getDefaultProfile")
            response = Hazkey_ResponseEnvelope.with {
                $0.status = .failed
                $0.errorMessage = "Unimplemented: getDefaultProfile"
            }
        case .none:
            NSLog("Payload not specified")
            response = Hazkey_ResponseEnvelope.with {
                $0.status = .failed
                $0.errorMessage = "Payload not specified"
            }
        }
        return serializeResult(unserialized: response)
    }

    private func serializeResult(unserialized: Hazkey_ResponseEnvelope) -> Data {
        do {
            let serialized = try unserialized.serializedData()
            return serialized
        } catch {
            NSLog("Failed to serialize response message: \(unserialized)")
            return Data()
        }
    }
}
