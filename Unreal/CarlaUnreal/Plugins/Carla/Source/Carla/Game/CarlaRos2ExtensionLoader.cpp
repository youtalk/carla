// Copyright (c) 2026 Computer Vision Center (CVC) at the Universitat Autonoma
// de Barcelona (UAB).
//
// This work is licensed under the terms of the MIT license.
// For a copy, see <https://opensource.org/licenses/MIT>.

#include "Carla/Game/CarlaRos2ExtensionLoader.h"
#include "Carla.h"

#include <dlfcn.h>
#include <string>

using InitFn = int (*)(const CarlaRos2Host *, CarlaRos2Extension *);

bool CarlaRos2ExtensionLoader::Load(const FString &Path, const CarlaRos2Host &Host)
{
  const std::string P = TCHAR_TO_UTF8(*Path);
  _handle = dlopen(P.c_str(), RTLD_NOW | RTLD_LOCAL);
  if (!_handle)
  {
    UE_LOG(LogCarla, Error, TEXT("ROS2 extension: dlopen('%s') failed: %s"),
        *Path, UTF8_TO_TCHAR(dlerror()));
    return false;
  }

  auto init = reinterpret_cast<InitFn>(dlsym(_handle, "carla_ros2_extension_init"));
  if (!init)
  {
    UE_LOG(LogCarla, Error, TEXT("ROS2 extension: symbol carla_ros2_extension_init missing: %s"),
        UTF8_TO_TCHAR(dlerror()));
    dlclose(_handle);
    _handle = nullptr;
    return false;
  }

  _ext = CarlaRos2Extension{};
  const int rc = init(&Host, &_ext);
  if (rc != 0)
  {
    UE_LOG(LogCarla, Error,
        TEXT("ROS2 extension: init returned %d (version mismatch?) — ABORTING load"), rc);
    dlclose(_handle);
    _handle = nullptr;
    return false;
  }

  // The host completes the two-sided handshake: never trust the extension's
  // self-report alone (see CarlaRos2Extension.h's carla_ros2_extension_init doc).
  if (_ext.api_version != CARLA_ROS2_EXTENSION_API_VERSION)
  {
    UE_LOG(LogCarla, Error,
        TEXT("ROS2 extension: api_version %u != host %u — ABORTING load"),
        _ext.api_version, CARLA_ROS2_EXTENSION_API_VERSION);
    dlclose(_handle);
    _handle = nullptr;
    return false;
  }

  _loaded = true;
  UE_LOG(LogCarla, Log, TEXT("ROS2 extension: loaded '%s' (api_version %u)"),
      *Path, _ext.api_version);
  return true;
}

void CarlaRos2ExtensionLoader::Tick(double SimTimeSeconds)
{
  if (_loaded && _ext.on_tick)
  {
    _ext.on_tick(_ext.ext_ctx, SimTimeSeconds);
  }
}

void CarlaRos2ExtensionLoader::Unload()
{
  if (_loaded && _ext.on_shutdown)
  {
    _ext.on_shutdown(_ext.ext_ctx);
  }
  _loaded = false;
  _ext = CarlaRos2Extension{};
  if (_handle)
  {
    dlclose(_handle);   // BEFORE ROS2->Shutdown(), see the header's ordering note.
    _handle = nullptr;
  }
}
