//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================

#include "bink_video.h"
#include "video_macros.h"

#include "filesystem.h"
#include "tier0/icommandline.h"
#include "tier1/strtools.h"
#include "tier1/utllinkedlist.h"
#include "tier1/KeyValues.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "materialsystem/itexture.h"
#include "vtf/vtf.h"
#include "pixelwriter.h"
#include "tier2/tier2.h"
#include "tier0/threadtools.h"
#include "platform.h"


#include "bink_material.h"
#include "tier0/memdbgon.h"

// ===========================================================================
// Singleton to expose Bink video subsystem
// ===========================================================================
static CBinkVideoSubSystem g_BinkSystem;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CBinkVideoSubSystem, IVideoSubSystem, VIDEO_SUBSYSTEM_INTERFACE_VERSION, g_BinkSystem );


// ===========================================================================
// List of file extensions and features supported by this subsystem
// ===========================================================================
VideoFileExtensionInfo_t s_BinkExtensions[] = 
{
	{ ".bik", VideoSystem::BINK,  VideoSystemFeature::FULL_PLAYBACK },
	{ ".bik2", VideoSystem::BINK,  VideoSystemFeature::FULL_PLAYBACK },
	{ ".blk", VideoSystem::BINK,  VideoSystemFeature::FULL_PLAYBACK },
	{ ".avi", VideoSystem::BINK,  VideoSystemFeature::FULL_PLAYBACK },
};

const int s_BinkExtensionCount = ARRAYSIZE( s_BinkExtensions );
const VideoSystemFeature_t	CBinkVideoSubSystem::DEFAULT_FEATURE_SET = VideoSystemFeature::FULL_PLAYBACK;

// ===========================================================================
// CBinkVideoSubSystem class
// ===========================================================================
CBinkVideoSubSystem::CBinkVideoSubSystem() :
	m_bBinkInitialized( false ),
	m_LastResult( VideoResult::SUCCESS ),
	m_CurrentStatus( VideoSystemStatus::NOT_INITIALIZED ),
	m_AvailableFeatures( CBinkVideoSubSystem::DEFAULT_FEATURE_SET ),
	m_pCommonServices( nullptr ),
	m_bAudioConfigured( false )
{
	memset( &m_AudioSpec, 0, sizeof(m_AudioSpec) );
}

CBinkVideoSubSystem::~CBinkVideoSubSystem()
{
	ShutdownBink();		// Super redundant safety check
}

// ===========================================================================
// IAppSystem methods
// ===========================================================================
bool CBinkVideoSubSystem::Connect( CreateInterfaceFn factory )
{
	if ( !BaseClass::Connect( factory ) )
	{
		return false;
	}

	if ( g_pFullFileSystem == nullptr || materials == nullptr ) 
	{
		Msg( "Bink video subsystem failed to connect to missing a required system\n" );
		return false;
	}
	return true;
}

void CBinkVideoSubSystem::Disconnect()
{
	BaseClass::Disconnect();
}

void* CBinkVideoSubSystem::QueryInterface( const char *pInterfaceName )
{

	if ( IS_NOT_EMPTY( pInterfaceName ) )
	{
		if ( V_strncmp(	pInterfaceName, VIDEO_SUBSYSTEM_INTERFACE_VERSION, Q_strlen( VIDEO_SUBSYSTEM_INTERFACE_VERSION ) + 1) == STRINGS_MATCH )
		{
			return (IVideoSubSystem*) this;
		}
	}

	return nullptr;
}


InitReturnVal_t CBinkVideoSubSystem::Init()
{
	InitReturnVal_t nRetVal = BaseClass::Init();
	if ( nRetVal != INIT_OK )
	{
		return nRetVal;
	}

	return INIT_OK;

}

void CBinkVideoSubSystem::Shutdown()
{
	// Make sure we shut down Bink
	ShutdownBink();

	BaseClass::Shutdown();
}


// ===========================================================================
// IVideoSubSystem identification methods  
// ===========================================================================
VideoSystem_t CBinkVideoSubSystem::GetSystemID()
{
	return VideoSystem::BINK;
}


VideoSystemStatus_t CBinkVideoSubSystem::GetSystemStatus()
{
	return m_CurrentStatus;
}


VideoSystemFeature_t CBinkVideoSubSystem::GetSupportedFeatures()
{
	return m_AvailableFeatures;
}


const char* CBinkVideoSubSystem::GetVideoSystemName()
{
	return "BINK";
}


// ===========================================================================
// IVideoSubSystem setup and shutdown services
// ===========================================================================
bool CBinkVideoSubSystem::InitializeVideoSystem( IVideoCommonServices *pCommonServices )
{
	m_AvailableFeatures = DEFAULT_FEATURE_SET;			// Put here because of issue with static const int, binary OR and DEBUG builds

	AssertPtr( pCommonServices );
	m_pCommonServices = pCommonServices;

	return ( m_bBinkInitialized ) ? true : SetupBink();
}


bool CBinkVideoSubSystem::ShutdownVideoSystem()
{
	return (  m_bBinkInitialized ) ? ShutdownBink() : true;
}


VideoResult_t CBinkVideoSubSystem::VideoSoundDeviceCMD( VideoSoundDeviceOperation_t operation, void *pDevice, void *pData )
{
	switch ( operation ) 
	{
		case VideoSoundDeviceOperation::SET_SDL_PARAMS:
		{
			if ( !pData )
				return SetResult( VideoResult::BAD_INPUT_PARAMETERS );
			const VideoAudioSpec &audioSpec = *static_cast<const VideoAudioSpec *>( pData );
			if ( audioSpec.m_Freq <= 0 || audioSpec.m_Channels == 0 )
				return SetResult( VideoResult::BAD_INPUT_PARAMETERS );

			AUTO_LOCK( m_AudioMaterialMutex );
			m_AudioSpec = audioSpec;
			m_bAudioConfigured = true;
			for ( int i = 0; i < m_AudioMaterialList.Count(); ++i )
			{
				if ( !m_AudioMaterialList[i]->ConfigureAudioOutput( m_AudioSpec ) )
					return SetResult( VideoResult::AUDIO_ERROR_OCCURED );
			}
			return SetResult( VideoResult::SUCCESS );
		}

		case VideoSoundDeviceOperation::SDLMIXER_CALLBACK:
		{
			if ( !pDevice || !pData )
				return VideoResult::SUCCESS;
			const int outputBytes = *static_cast<const int *>( pData );
			AUTO_LOCK( m_AudioMaterialMutex );
			if ( !m_bAudioConfigured )
				return VideoResult::SUCCESS;
			for ( int i = 0; i < m_AudioMaterialList.Count(); ++i )
				m_AudioMaterialList[i]->MixAudio( static_cast<uint8_t *>( pDevice ), outputBytes );
			return VideoResult::SUCCESS;
		}

		case VideoSoundDeviceOperation::SET_DIRECT_SOUND_DEVICE:
		{
			return SetResult( VideoResult::OPERATION_NOT_SUPPORTED );
		}

		case VideoSoundDeviceOperation::SET_MILES_SOUND_DEVICE:
		case VideoSoundDeviceOperation::HOOK_X_AUDIO:
		{
			return SetResult( VideoResult::OPERATION_NOT_SUPPORTED );
		}

		default:
		{
			return SetResult( VideoResult::UNKNOWN_OPERATION );
		}
	}
}


void CBinkVideoSubSystem::RegisterAudioMaterial( CBinkMaterial *pMaterial )
{
	if ( !pMaterial || !pMaterial->HasAudio() )
		return;
	AUTO_LOCK( m_AudioMaterialMutex );
	if ( m_AudioMaterialList.Find( pMaterial ) == -1 )
		m_AudioMaterialList.AddToTail( pMaterial );
	if ( m_bAudioConfigured )
		pMaterial->ConfigureAudioOutput( m_AudioSpec );
}


void CBinkVideoSubSystem::UnregisterAudioMaterial( CBinkMaterial *pMaterial )
{
	AUTO_LOCK( m_AudioMaterialMutex );
	int index = m_AudioMaterialList.Find( pMaterial );
	if ( index != -1 )
		m_AudioMaterialList.FindAndFastRemove( pMaterial );
}


// ===========================================================================
// IVideoSubSystem supported extensions & features
// ===========================================================================
int CBinkVideoSubSystem::GetSupportedFileExtensionCount()
{
	return s_BinkExtensionCount;
}

 
const char* CBinkVideoSubSystem::GetSupportedFileExtension( int num )
{
	return ( num < 0 || num >= s_BinkExtensionCount ) ? nullptr : s_BinkExtensions[num].m_FileExtension;
}

 
VideoSystemFeature_t CBinkVideoSubSystem::GetSupportedFileExtensionFeatures( int num )
{
	 return ( num < 0 || num >= s_BinkExtensionCount ) ? VideoSystemFeature::NO_FEATURES : s_BinkExtensions[num].m_VideoFeatures;
}


// ===========================================================================
// IVideoSubSystem Video Playback and Recording Services
// ===========================================================================
VideoResult_t CBinkVideoSubSystem::PlayVideoFileFullScreen( const char *filename, void *mainWindow, int windowWidth, int windowHeight, int desktopWidth, int desktopHeight, bool windowed, float forcedMinTime, VideoPlaybackFlags_t playbackFlags )
{
	(void)mainWindow;
	(void)desktopWidth;
	(void)desktopHeight;

	if ( m_CurrentStatus != VideoSystemStatus::OK )
		return SetResult( VideoResult::SYSTEM_NOT_AVAILABLE );
	if ( IS_EMPTY_STR( filename ) || windowWidth <= 0 || windowHeight <= 0 )
		return SetResult( VideoResult::BAD_INPUT_PARAMETERS );
	if ( playbackFlags & ~VideoPlaybackFlags::VALID_FULLSCREEN_FLAGS )
		return SetResult( VideoResult::FEATURE_NOT_AVAILABLE );
	if ( ANY_BITFLAGS_SET( playbackFlags, VideoPlaybackFlags::LOOP_VIDEO | VideoPlaybackFlags::PRELOAD_VIDEO ) )
		return SetResult( VideoResult::FEATURE_NOT_AVAILABLE );

	VideoPlaybackFlags_t materialFlags = VideoPlaybackFlags::TEXTURES_ACTUAL_SIZE | VideoPlaybackFlags::DONT_AUTO_START_VIDEO;
	if ( BITFLAGS_SET( playbackFlags, VideoPlaybackFlags::NO_AUDIO ) )
		materialFlags |= VideoPlaybackFlags::NO_AUDIO;

	CBinkMaterial videoMaterial;
	if ( !videoMaterial.Init( "__fullscreen_bink", filename, materialFlags ) )
		return SetResult( videoMaterial.GetLastResult() );
	RegisterAudioMaterial( &videoMaterial );
	if ( !videoMaterial.StartVideo() )
	{
		UnregisterAudioMaterial( &videoMaterial );
		videoMaterial.Shutdown();
		return SetResult( videoMaterial.GetLastResult() );
	}

	int videoWidth = 0, videoHeight = 0;
	int textureWidth = 0, textureHeight = 0;
	videoMaterial.GetVideoImageSize( &videoWidth, &videoHeight );
	videoMaterial.GetTextureSize( &textureWidth, &textureHeight );
	{
		CMatRenderContextPtr renderContext( materials );
		renderContext->GetRenderTargetDimensions( windowWidth, windowHeight );
	}

	int outputWidth = videoWidth, outputHeight = videoHeight;
	int outputX = 0, outputY = 0;
	if ( !m_pCommonServices->CalculateVideoDimensions( videoWidth, videoHeight, windowWidth, windowHeight,
		playbackFlags, &outputWidth, &outputHeight, &outputX, &outputY ) )
	{
		UnregisterAudioMaterial( &videoMaterial );
		videoMaterial.Shutdown();
		return SetResult( VideoResult::VIDEO_ERROR_OCCURED );
	}

	VideoResult_t inputResult = m_pCommonServices->InitFullScreenPlaybackInputHandler( playbackFlags, forcedMinTime, windowed );
	if ( inputResult != VideoResult::SUCCESS )
	{
		UnregisterAudioMaterial( &videoMaterial );
		videoMaterial.Shutdown();
		return SetResult( inputResult );
	}
	while ( !videoMaterial.IsFinishedPlaying() )
	{
		bool abortEvent = false, pauseEvent = false, quitEvent = false;
		if ( m_pCommonServices->ProcessFullScreenInput( abortEvent, pauseEvent, quitEvent ) )
		{
			if ( abortEvent || quitEvent )
				break;
			if ( pauseEvent )
				videoMaterial.SetPaused( !videoMaterial.IsPaused() );
		}

		if ( !videoMaterial.IsPaused() )
			videoMaterial.Update();

		CMatRenderContextPtr renderContext( materials );
		renderContext->Viewport( 0, 0, windowWidth, windowHeight );
		renderContext->DepthRange( 0.0f, 1.0f );
		renderContext->SetToneMappingScaleLinear( Vector( 1, 1, 1 ) );
		renderContext->ClearColor3ub( 0, 0, 0 );
		renderContext->ClearBuffers( true, true, true );
		renderContext->DrawScreenSpaceRectangle( videoMaterial.GetMaterial(), outputX, outputY, outputWidth, outputHeight,
			0, 0, videoWidth - 1, videoHeight - 1, textureWidth, textureHeight, nullptr, 1, 1 );
		renderContext->Flush( true );
		materials->SwapBuffers();

		ThreadSleep( videoMaterial.IsPaused() ? 5 : 1 );
	}

	VideoResult_t playbackResult = videoMaterial.GetLastResult();
	m_pCommonServices->TerminateFullScreenPlaybackInputHandler();
	UnregisterAudioMaterial( &videoMaterial );
	videoMaterial.Shutdown();
	return SetResult( playbackResult );
}


// ===========================================================================
// IVideoSubSystem Video Material Services
//   note that the filename is absolute and has already resolved any paths
// ===========================================================================
IVideoMaterial* CBinkVideoSubSystem::CreateVideoMaterial( const char *pMaterialName, const char *pVideoFileName, VideoPlaybackFlags_t flags )
{
	SetResult( VideoResult::BAD_INPUT_PARAMETERS );
	AssertExitN( m_CurrentStatus == VideoSystemStatus::OK && IS_NOT_EMPTY( pMaterialName ) || IS_NOT_EMPTY( pVideoFileName ) );

	CBinkMaterial *pVideoMaterial = new CBinkMaterial();
	if ( pVideoMaterial == nullptr || pVideoMaterial->Init( pMaterialName, pVideoFileName,
		flags | VideoPlaybackFlags::DONT_AUTO_START_VIDEO ) == false )
	{
		SAFE_DELETE( pVideoMaterial );
		SetResult( VideoResult::VIDEO_ERROR_OCCURED );
		return nullptr;
	}

	IVideoMaterial	*pInterface = (IVideoMaterial*) pVideoMaterial;
	m_MaterialList.AddToTail( pInterface );
	RegisterAudioMaterial( pVideoMaterial );
	if ( !BITFLAGS_SET( flags, VideoPlaybackFlags::DONT_AUTO_START_VIDEO ) && !pVideoMaterial->StartVideo() )
	{
		UnregisterAudioMaterial( pVideoMaterial );
		m_MaterialList.FindAndFastRemove( pInterface );
		delete pVideoMaterial;
		SetResult( VideoResult::VIDEO_ERROR_OCCURED );
		return nullptr;
	}

	SetResult( VideoResult::SUCCESS );
	return pInterface;
}


VideoResult_t CBinkVideoSubSystem::DestroyVideoMaterial( IVideoMaterial *pVideoMaterial )
{
	AssertExitV( m_CurrentStatus == VideoSystemStatus::OK, SetResult( VideoResult::SYSTEM_NOT_AVAILABLE ) );
	AssertPtrExitV( pVideoMaterial, SetResult( VideoResult::BAD_INPUT_PARAMETERS ) );

	if ( m_MaterialList.Find( pVideoMaterial ) != -1 )
	{
		CBinkMaterial *pObject = (CBinkMaterial*) pVideoMaterial;
		UnregisterAudioMaterial( pObject );
		pObject->Shutdown();
		delete pObject;

		m_MaterialList.FindAndFastRemove( pVideoMaterial );

		return SetResult( VideoResult::SUCCESS );
	}

	return SetResult (VideoResult::MATERIAL_NOT_FOUND );
}


// ===========================================================================
// IVideoSubSystem Video Recorder Services
// ===========================================================================
IVideoRecorder* CBinkVideoSubSystem::CreateVideoRecorder()
{
	SetResult( VideoResult::FEATURE_NOT_AVAILABLE );
	return nullptr;
}


VideoResult_t CBinkVideoSubSystem::DestroyVideoRecorder( IVideoRecorder *pRecorder )
{
	return SetResult( VideoResult::FEATURE_NOT_AVAILABLE );
}

VideoResult_t CBinkVideoSubSystem::CheckCodecAvailability( VideoEncodeCodec_t codec )
{
	AssertExitV( m_CurrentStatus == VideoSystemStatus::OK, SetResult( VideoResult::SYSTEM_NOT_AVAILABLE ) );
	AssertExitV( codec >= VideoEncodeCodec::DEFAULT_CODEC && codec < VideoEncodeCodec::CODEC_COUNT, SetResult( VideoResult::BAD_INPUT_PARAMETERS ) );

	return SetResult( VideoResult::FEATURE_NOT_AVAILABLE );
}


// ===========================================================================
// Status support
// ===========================================================================
VideoResult_t CBinkVideoSubSystem::GetLastResult()
{
	return m_LastResult;
}


VideoResult_t CBinkVideoSubSystem::SetResult( VideoResult_t status )
{
	m_LastResult = status;
	return status;
}


// ===========================================================================
// Bink Initialization & Shutdown
// ===========================================================================
bool CBinkVideoSubSystem::SetupBink()
{
	SetResult( VideoResult::INITIALIZATION_ERROR_OCCURED);
	AssertExitF( m_bBinkInitialized == false );

	// This is set early to indicate we have already been through here, even if we error out for some reason
	m_bBinkInitialized = true;
	m_CurrentStatus = VideoSystemStatus::OK;
	m_AvailableFeatures = DEFAULT_FEATURE_SET;
	// $$INIT CODE HERE$$


	// Note that we are now open for business....	
	m_bBinkInitialized = true;
	SetResult( VideoResult::SUCCESS );

	return true;
}


bool CBinkVideoSubSystem::ShutdownBink()
{
	if ( m_bBinkInitialized && m_CurrentStatus == VideoSystemStatus::OK )
	{

	}

	m_bBinkInitialized = false;
	m_CurrentStatus = VideoSystemStatus::NOT_INITIALIZED;
	m_AvailableFeatures = VideoSystemFeature::NO_FEATURES;
	{
		AUTO_LOCK( m_AudioMaterialMutex );
		m_AudioMaterialList.RemoveAll();
		m_bAudioConfigured = false;
	}
	SetResult( VideoResult::SUCCESS );

	return true;
}
