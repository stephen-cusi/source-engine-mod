#ifndef IGAMETIMESCALE_H
#define IGAMETIMESCALE_H

#define ENGINE_GAMETIMESCALE_INTERFACE_VERSION "EngineGameTimeScale001"

abstract_class IEngineGameTimeScale
{
public:
	virtual void SetGameTimeScale( float scale ) = 0;
};

#endif
