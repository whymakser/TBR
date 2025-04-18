#ifndef ENGINE_HTTP_H
#define ENGINE_HTTP_H

#include "kernel.h"
#include <memory>

class IHttpRequest
{
public:
	class CConfig *m_pConfig;
};

class IHttp : public IInterface
{
	MACRO_INTERFACE("http", 0)

public:
	virtual void Run(std::shared_ptr<IHttpRequest> pRequest) = 0;
};

#endif
