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

	class FairyApplication
	{
	private:
		vl::glr::json::Parser                            parser;
		ApiConfig                                       config;
		MemoryStore                                     memory;
		AgentPrompts                                    prompts;
		vl::Ptr<vl::glr::json::JsonArray>                 fairyHistory;
		vl::Ptr<vl::glr::json::JsonNode>                  toolSchema;
		vl::Func<vl::WString(const vl::WString&)>         complete;
		vl::Func<void(vl::collections::List<MonitorSnapshot>&)> capture;
		vl::Func<WebResponse(const vl::WString&)>         fetch;
		vl::Ptr<CancellationToken>                      cancellation;

		vl::WString                                     RunAgent(bool vision, vl::Ptr<vl::glr::json::JsonArray> history);
		vl::WString                                     ExecuteTool(const vl::WString& name, const vl::WString& arguments, vl::WString& spoken);
		void                                            Initialize();

	public:
		// Complete assistant JSON messages, before tool execution; true identifies Vision.
		vl::Event<void(bool, const vl::WString&)>         ResponseReceived;

		// The UI supplies both folders; neither location nor their relationship is assumed here.
		                                                FairyApplication(const vl::filesystem::FilePath& envFolder, const vl::filesystem::FilePath& memoryFolder,
		                                                    vl::Ptr<CancellationToken> cancellationToken = nullptr);
		// Inject I/O to exercise the actual agent loop without credentials or desktop access.
		                                                FairyApplication(const vl::filesystem::FilePath& memoryFolder, const ApiConfig& apiConfig, const AgentPrompts& agentPrompts,
		                                                    vl::Func<vl::WString(const vl::WString&)> completion,
		                                                    vl::Func<void(vl::collections::List<MonitorSnapshot>&)> snapshots,
		                                                    vl::Func<WebResponse(const vl::WString&)> httpGet,
		                                                    vl::Ptr<CancellationToken> cancellationToken = nullptr);
		// Join each agent's nonempty speak texts in order with newlines, across all tool replies.
		// Forward the complete vision result to the fairy and return the complete fairy result.
		vl::WString                                     RunRound();
	};

	// Own one application/session on a worker. The UI requests the next round only
	// after displaying the previous result. StopAndWait cancels I/O and joins it.
	class DesktopAgentRunner : public vl::Thread
	{
	private:
		vl::Func<vl::Ptr<FairyApplication>()>            createApplication;
		vl::Func<void(const vl::WString&)>               publish;
		vl::Ptr<CancellationToken>                      cancellation;
		vl::EventObject                                nextRound;

	protected:
		void                                            Run() override;

	public:
		                                                DesktopAgentRunner(const vl::filesystem::FilePath& envFolder, const vl::filesystem::FilePath& memoryFolder,
		                                                    vl::Func<void(const vl::WString&)> publishResult);
		// Inject a factory to test the actual worker with offline model responses.
		                                                DesktopAgentRunner(vl::Func<vl::Ptr<FairyApplication>()> factory,
		                                                    vl::Ptr<CancellationToken> cancellationToken, vl::Func<void(const vl::WString&)> publishResult);
		                                                ~DesktopAgentRunner();
		void                                            RequestRound();
		void                                            StopAndWait();
	};
}

#endif
