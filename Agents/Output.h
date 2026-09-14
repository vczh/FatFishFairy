#ifndef FATFISH_OUTPUT_H
#define FATFISH_OUTPUT_H

#include "Json.h"

namespace fatfish
{
	extern vl::WString FormatAgentResponse(bool vision, const vl::WString& message, vl::glr::json::Parser& parser);
}

#endif
