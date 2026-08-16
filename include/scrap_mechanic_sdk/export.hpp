#pragma once

#if defined(SCRAP_MECHANIC_SDK_BUILD)
#define SM_SDK_API __declspec(dllexport)
#else
#define SM_SDK_API __declspec(dllimport)
#endif

#if defined(SCRAP_MECHANIC_SDK_BUILD)
#define SM_SDK_C_API extern "C" __declspec(dllexport)
#else
#define SM_SDK_C_API extern "C" __declspec(dllimport)
#endif
