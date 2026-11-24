// Shared by the engine and game DLLs for the finite frame diagnostic.
#ifndef IHL2SB_FRAME_PROFILE_H
#define IHL2SB_FRAME_PROFILE_H

#define HL2SB_FRAME_PROFILE_INTERFACE_VERSION "HL2SBFrameProfile001"
#define HL2SB_FRAME_PROFILE_DETAILED_INTERFACE_VERSION "HL2SBFrameProfile002"

class IHL2SBFrameProfile
{
public:
	virtual bool IsActive() const = 0;
	virtual void RecordLuaCall( bool bServer, double flProtectedSeconds, double flVMSeconds, int nCalls, int nErrors ) = 0;
};

class IHL2SBFrameProfileDetailed : public IHL2SBFrameProfile
{
public:
	virtual bool IsDetailActive() const = 0;
	virtual void RecordLuaNamedCall( bool bServer, const char *pszName, const char *pszSource,
		int nLine, double flInclusiveSeconds, int nCalls ) = 0;
};

#endif // IHL2SB_FRAME_PROFILE_H
