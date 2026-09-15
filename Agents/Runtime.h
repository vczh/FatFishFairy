#ifndef FATFISH_RUNTIME_H
#define FATFISH_RUNTIME_H

#include "Json.h"
#include "Memory.h"
#include "Platform.h"

namespace fatfish
{
	struct AgentPrompts
	{
		vl::WString tools, guidance, vision, fairy, character;
	};

	enum class AgentPhase
	{
		Vision,
		Fairy,
	};

	// The current observation still exceeds context after all fairy recovery attempts.
	class FairyContextRecoveryExhausted : public ContextLimitExceeded
	{
	};

	// The UI supplies exact character paths. Only an absent selected file uses the fallback.
	extern vl::WString                                  LoadCharacterPrompt(const vl::filesystem::FilePath& selectedFile, const vl::filesystem::FilePath& fallbackFile);

	class FairyApplication
	{
	private:
		vl::glr::json::Parser                            parser;
		ApiConfig                                       config;
		MemoryStore                                     memory;
		AgentPrompts                                    prompts;
		// Only completed rounds are retained. The active round is owned by RunRound.
		vl::collections::List<vl::Ptr<vl::glr::json::JsonArray>> fairyRounds;
		vl::Ptr<vl::glr::json::JsonNode>                  toolSchema;
		vl::Func<vl::WString(const vl::WString&)>         complete;
		vl::Func<void(vl::collections::List<MonitorSnapshot>&)> capture;
		vl::Func<WebResponse(const vl::WString&)>         fetch;
		vl::Func<vl::WString()>                          characterProvider;
		vl::Ptr<CancellationToken>                      cancellation;

		vl::WString                                     RunAgent(bool vision, vl::Ptr<vl::glr::json::JsonArray> history);
		vl::WString                                     ExecuteTool(const vl::WString& name, const vl::WString& arguments, vl::WString& spoken, bool& speechSubmitted);
		void                                            Initialize();

	public:
		// Complete assistant JSON messages, before tool execution; true identifies Vision.
		vl::Event<void(bool, const vl::WString&)>         ResponseReceived;
		// Synchronous notifications on the round's owning thread, before work/recovery.
		vl::Event<void(AgentPhase)>                     PhaseChanged;
		// Every fairy context rejection, including the one that exhausts recovery.
		vl::Event<void()>                               ContextOverflow;

		// The UI supplies both folders; neither location nor their relationship is assumed here.
		                                                FairyApplication(const vl::filesystem::FilePath& envFolder, const vl::filesystem::FilePath& memoryFolder,
		                                                    vl::Func<vl::WString()> loadCharacter,
		                                                    vl::Ptr<CancellationToken> cancellationToken = nullptr);
		// Inject I/O to exercise the actual agent loop without credentials or desktop access.
		                                                FairyApplication(const vl::filesystem::FilePath& memoryFolder, const ApiConfig& apiConfig, const AgentPrompts& agentPrompts,
		                                                    vl::Func<vl::WString(const vl::WString&)> completion,
		                                                    vl::Func<void(vl::collections::List<MonitorSnapshot>&)> snapshots,
		                                                    vl::Func<WebResponse(const vl::WString&)> httpGet,
		                                                    vl::Ptr<CancellationToken> cancellationToken = nullptr,
		                                                    vl::Func<vl::WString()> loadCharacter = {});
		// Join each agent's nonempty speak texts in order with newlines, across all tool replies.
		// Forward the complete vision result to the fairy and return the complete fairy result.
		vl::WString                                     RunRound();
		// Call between rounds on the owning thread. Saved memories remain available.
		void                                            ResetFairySession();
	};

	// Own one application on a worker. The UI requests the next round only
	// after displaying the previous result. StopAndWait cancels I/O and joins it.
	class DesktopAgentRunner : public vl::Thread
	{
	private:
		vl::Func<vl::Ptr<FairyApplication>()>            createApplication;
		vl::Func<void(const vl::WString&)>               publish;
		vl::Func<void(const vl::WString&)>               persistSpeech;
		vl::Func<void(const vl::WString&)>               reportProgress;
		vl::Ptr<CancellationToken>                      cancellation;
		vl::EventObject                                nextRound;
		vl::SpinLock                                   lockCharacter;
		vl::filesystem::FilePath                        characterFile;
		vl::filesystem::FilePath                        fallbackCharacterFile;
		bool                                            resetFairySession = false; // Protected by lockCharacter.
		vl::filesystem::FilePath                        activeCharacterFile; // Worker-owned selection for this round.

		vl::WString                                     ReadCharacterPrompt();

	protected:
		void                                            Run() override;

	public:
		// Progress is reported on the worker as V/F plus consecutive failures (zero omitted).
		                                                DesktopAgentRunner(const vl::filesystem::FilePath& envFolder, const vl::filesystem::FilePath& memoryFolder,
		                                                    const vl::filesystem::FilePath& initialCharacterFile, const vl::filesystem::FilePath& fallbackFile,
		                                                    vl::Func<void(const vl::WString&)> publishResult,
		                                                    vl::Func<void(const vl::WString&)> progress = {});
		// Inject a factory to test the actual worker with offline model responses.
		                                                DesktopAgentRunner(vl::Func<vl::Ptr<FairyApplication>()> factory,
		                                                    vl::Ptr<CancellationToken> cancellationToken, vl::Func<void(const vl::WString&)> publishResult,
		                                                    vl::Func<void(const vl::WString&)> saveSpeech = {},
		                                                    vl::Func<void(const vl::WString&)> progress = {});
		                                                ~DesktopAgentRunner();
		// Finish the current round, then use this character with a fresh fairy session.
		// Selecting the same file does not reset; saved memories are retained.
		void                                            SetCharacterFile(const vl::filesystem::FilePath& selectedFile);
		void                                            RequestRound();
		void                                            StopAndWait();
	};
}

#endif
