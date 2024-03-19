#define VL_ENTRY_POINT

#include <Vulture.h>
#include "PathTracer.h"

// Create VL Entry point definition
Vulture::Application* Vulture::CreateApplication()
{
	Vulture::ApplicationInfo appInfo;
	appInfo.Name = "Path Tracer";
	appInfo.WorkingDirectory = "";
	appInfo.RayTracingSupport = true;
	return new PathTracer(appInfo, 1600, 900);
}