#ifndef UNLAUNCH_H
#define UNLAUNCH_H
#include <string_view>
#include <span>

#include "consoleInfo.h"

typedef enum ASTRONAUT_VERSION {
	ASTRONAUT_INDEV,
	ASTRONAUT_0_1_0,
	INVALID,
} ASTRONAUT_VERSION;

static constexpr auto MAX_GIF_SIZE = 0x3C70;

const char* getAstronautVersionString(ASTRONAUT_VERSION);

bool uninstallAstronaut(const consoleInfo& info, bool removeHNAABackup);
bool installAstronaut(const consoleInfo& info, bool disableAllPatches, const char* splashSoundBinaryPatchPath, std::span<uint8_t> customBackground);

ASTRONAUT_VERSION loadAstronaut(std::string_view path);
ASTRONAUT_VERSION loadUnlaunchLikeHomebrew(std::string_view path);

#endif
