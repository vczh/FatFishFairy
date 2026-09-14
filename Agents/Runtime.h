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
		vl::filesystem::FilePath                         repositoryRoot;
		vl::glr::json::Parser                            parser;
		ApiConfig                                       config;
		MemoryStore                                     memory;
		AgentPrompts                                    prompts;
		vl::Ptr<vl::glr::json::JsonArray>                 fairyHistory;
		vl::Ptr<vl::glr::json::JsonNode>                  toolSchema;
		vl::Func<vl::WString(const vl::WString&)>         complete;
		vl::Func<void(vl::collections::List<MonitorSnapshot>&)> capture;
		vl::Func<WebResponse(const vl::WString&)>         fetch;

		vl::WString                                     RunAgent(bool vision, vl::Ptr<vl::glr::json::JsonArray> history);
		vl::WString                                     ExecuteTool(const vl::WString& name, const vl::WString& arguments, vl::WString& spoken);
		void                                            Initialize();

	public:
		// Complete assistant JSON messages, before tool execution; true identifies Vision.
		vl::Event<void(bool, const vl::WString&)>         ResponseReceived;

		explicit                                        FairyApplication(const vl::filesystem::FilePath& root);
		// Inject I/O to exercise the actual agent loop without credentials or desktop access.
		                                                FairyApplication(const vl::filesystem::FilePath& root, const ApiConfig& apiConfig, const AgentPrompts& agentPrompts,
		                                                    vl::Func<vl::WString(const vl::WString&)> completion,
		                                                    vl::Func<void(vl::collections::List<MonitorSnapshot>&)> snapshots,
		                                                    vl::Func<WebResponse(const vl::WString&)> httpGet);
		vl::WString                                     RunRound();
		static vl::filesystem::FilePath                  FindRepositoryRoot();
	};

	extern void RunSelfTests();
}

#endif
