#pragma once

#include "pch.h"
#include "Vulture.h"

// Class holding state of the application mainly used for communication between editor and renderer
// without passing parameters into renderer functions
class State
{
public:

	enum class RenderState
	{
		PreviewRender,
		FileRender,
		FileRenderFinished
	};

	static RenderState CurrentRenderState;

	static bool RunDenoising;
	static bool Denoised;
	static bool ShowDenoised;
	static bool RunTonemapping;
	static bool RecreateRayTracingPipeline;
	static bool RecompilePosterizeShader;
	static bool ModelChanged;
	static bool ChangeSkybox;
	static bool ReplacePalletDefineInPosterizeShader;
	static bool UseChromaticAberration;

	static float ModelScale;
	static std::string ModelPath;

	static Vulture::Tonemap::Tonemappers CurrentTonemapper;

	static Vulture::Entity CurrentSkyboxEntity;
	static std::string CurrentSkyboxPath;

	static Vulture::Entity CurrentModelEntity;
};
