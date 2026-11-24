#ifndef ISERVERPASSWORDCHECK_H
#define ISERVERPASSWORDCHECK_H

#include "tier1/interface.h"

#define HL2SB_SERVER_PASSWORD_CHECK_VERSION "HL2SBServerPasswordCheck001"

enum class ServerPasswordDecision
{
	UseDefault,
	Accept,
	Reject,
};

class IServerPasswordCheck : public IBaseInterface
{
public:
	virtual ServerPasswordDecision CheckPassword( uint64 steamID, const char *address,
		const char *serverPassword, const char *clientPassword, const char *name,
		char *rejectReason, int rejectReasonSize ) = 0;
};

#endif
