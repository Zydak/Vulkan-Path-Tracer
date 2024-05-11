#include "pch.h"
#include "State.h"

State::RenderState State::CurrentRenderState = RenderState::PreviewRender;

bool State::RunDenoising = false;

bool State::Denoised = false;

bool State::ShowDenoised = false;

bool State::RecreateRayTracingPipeline = false;

bool State::RecompilePosterizeShader = false;

bool State::ModelChanged = false;

bool State::ChangeSkybox = false;

bool State::ReplacePalletDefineInPosterizeShader = false;

bool State::UseChromaticAberration = false;

float State::ModelScale = 0.5f;

std::string State::ModelPath;

Vulture::Tonemap::Tonemappers State::CurrentTonemapper = Vulture::Tonemap::Tonemappers::Filmic;

Vulture::Entity State::CurrentSkyboxEntity;

std::string State::CurrentSkyboxPath;

