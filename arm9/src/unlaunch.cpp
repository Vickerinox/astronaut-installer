#include "message.h"
#include "sha1digest.h"
#include "storage.h"
#include "tonccpy.h"
#include "unlaunch.h"

#include <nds/sha1.h>

#include <algorithm>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <memory>
#include <format>

static char astronautBuffer[520+(256*1024)];
static char ogAstronautBuffer[520+(256*1024)];
static const char* hnaaTmdPath = "nand:/title/00030017/484e4141/content/title.tmd";
static const char* hnaaBackupTmdPath = "nand:/title/00030017/484e4141/content/title.tmd.bak";

ASTRONAUT_VERSION installerVersion{INVALID};
size_t astronautSize{};

constexpr std::array knownAstronautHashes{
	"0000000000000000000000000000000000000000"_sha1, //nightly release, any hash.
	"99454e7a84adc702247d1f93d165c7195e127378"_sha1, //first private astronaut pre-release (INDEV)
	"797183356a5fc2b6a8cbce04e313fac39e4a6125"_sha1, //first public astronaut release (0.1.0, unfortunately unreproducible)
};


static bool writeAstronautToHNAAFolder();

bool isValidAstronautSize(size_t size)
{
	if(installerVersion == ASTRONAUT_NIGHTLY) {
		return size < sizeof(astronautBuffer)-520;
	} else {
		return size == 80880;
	}
}

static bool removeHnaaLauncher()
{
	auto errString = [] -> std::string {
		if(fileExists(hnaaTmdPath)) {
			if(!toggleFileReadOnly(hnaaTmdPath, false))
			{
				return "\x1B[31mError:\x1B[33m Failed to mark astronauts's title.tmd as writable\nLeaving as is\n";
			}
			if(!removeIfExists(hnaaTmdPath))
			{
				return "\x1B[31mError:\x1B[33m Failed to delete astronauts's title.tmd\n";
			}
		}
		if(!removeIfExists("nand:/title/00030017/484e4141/content"))
		{
			return std::format("\x1B[31mError:\x1B[33m Failed to delete astronaut's content folder: {}\n", errno);
		}
		if(!removeIfExists("nand:/title/00030017/484e4141"))
		{
			return std::format("\x1B[31mError:\x1B[33m Failed to delete astronaut's 484e4141 folder: {}\n", errno);
		}
		return "";
	}();
	if(errString.size())
	{
		messageBox(errString.data());
		return false;
	}
	return true;
}

static bool restoreMainTmd(const consoleInfo& info, bool removeHNAABackup)
{
	std::shared_ptr<FILE> launcherTmdSptr{fopen(info.launcherTmdPath.data(), "r+b"), [](auto* ptr){ if(ptr) fclose(ptr);}};
	if(!launcherTmdSptr)
	{
		messageBox("\x1B[31mError:\x1B[33m Failed to open default launcher's title.tmd\n");
		return false;
	}
	FILE* launcherTmd = launcherTmdSptr.get();

	// If the tmd is patched, assume the HNAA backup is already set in place.
	if(info.tmdPatched) {
		fseek(launcherTmd, 0x190, SEEK_SET);
		// Set back the title.tmd's title id from GNXX to HNXX
		char c = 0x48;
		fwrite(&c, 1, 1, launcherTmd);
		fflush(launcherTmd);
	} else if(!info.tmdGood || info.tmdInvalid) {
		// The tmd isn't good, it either has the wrong size, or the hash didn't match
		// and it wasn't patched with the new method
		// Install the hnaa backup if not found and then truncate the tmd to 520b
		// before restoring it
		if(!info.ModdedHNAAtmdFound && !removeHNAABackup)
		{
			auto choiceString = [&]{
				if(installerVersion != INVALID)
					return "Unlaunch/Astronaut was installed with the\n"
							"legacy method.\n"
							"Before uninstalling it, a\n"
							"failsafe installation will be\n"
							"created.\n"
							"Proceed?";
				return "Unlaunch/Astronaut was installed with the\n"
						"legacy method\n"
						"But a failsafe installation\n"
						"cannot be created since no valid\n"
						"astronaut was provided.\n"
						"Proceed anyways?";
			}();
			if(choiceBox(choiceString) == NO)
			{
				return false;
			}
			if(installerVersion != INVALID)
			{
				if(!writeAstronautToHNAAFolder())
				{
					if(choiceBox("Failsafe installation couldn't\n"
									"be copmleted.\n"
									"Proceed anyways?") == NO)
					{
						return false;
					}
				}
			}
		}
		if (ftruncate(fileno(launcherTmd), 520) != 0) {
			messageBox("\x1B[31mError:\x1B[33m Failed to remove stage2 mod\n");
			return false;
		}
	}

	Sha1Digest digest;
	calculateFileSha1(launcherTmd, &digest);

	// the tmd still doesn't match, write a known good one
	if(digest != info.recoveryTmdDataSha) {
		fseek(launcherTmd, 0, SEEK_SET);
		auto written = fwrite(info.recoveryTmdData.data(), info.recoveryTmdData.size(), 1, launcherTmd);
		if(written != 1) {
			messageBox("\x1B[31mError:\x1B[33m Failed to remove stage2 mod\n");
			return false;
		}
	}
	if(removeHNAABackup && info.ModdedHNAAtmdFound)
	{
		return removeHnaaLauncher();
	}
	return true;
}

static bool patchMainTmd(const char* path)
{
	FILE* launcherTmd = fopen(path, "r+b");
	if(!launcherTmd)
	{
		messageBox("\x1B[31mError:\x1B[33m Failed to open default launcher's title.tmd\n");
		return false;
	}
	// Patches the title.tmd's title id from HNXX to GNXX
	fseek(launcherTmd, 0x190, SEEK_SET);
	char c;
	fread(&c, 1, 1, launcherTmd);
	//if byte is not already set, it's clean
	if(c == 0x48)
	{
		fseek(launcherTmd, -1, SEEK_CUR);
		c = 0x47;
		fwrite(&c, 1, 1, launcherTmd);
	}
	else if(c != 0x47)
	{
		messageBox("\x1B[31mError:\x1B[33m Default launcher's title.tmd was tamprered with, aborting\n");
		fclose(launcherTmd);
		return false;
	}

	fclose(launcherTmd);
	return true;
}

static bool restoreProtoTmd(const char* path)
{
	if (!fileExists(hnaaBackupTmdPath))
	{
		messageBox("\x1B[31mError:\x1B[33m No original tmd found!\nCan't uninstall stage2 mod.\n");
		return false;
	}
	removeIfExists(path);
	rename(hnaaBackupTmdPath, path);
	toggleFileReadOnly(path, false);
	return true;
}

bool uninstallAstronaut(const consoleInfo& info, bool removeHNAABackup)
{
	// TODO: handle retailLauncherTmdPresentAndToBePatched = false on retail consoles
	if (info.isRetail) {
		if(!toggleFileReadOnly(info.launcherTmdPath.data(), false))
		{
			messageBox(std::format("\x1B[31mError:\x1B[33m Failed to make {} writable\n", info.launcherTmdPath).data());
			return false;
		}
		if(!toggleFileReadOnly(info.launcherAppPath.data(), false))
		{
			messageBox(std::format("\x1B[31mError:\x1B[33m Failed to make {} writable\n", info.launcherAppPath).data());
			return false;
		}
		if (!restoreMainTmd(info, removeHNAABackup))
		{
			return false;
		}
	} else {
		if (!toggleFileReadOnly(hnaaTmdPath, false) || !restoreProtoTmd(hnaaTmdPath))
		{
			return false;
		}
	}
	return true;
}

static bool writeAstronautTmd(const char* path)
{
	static constexpr auto unlaunchShaOffset = 0x4000;
	Sha1Digest expectedDigest, actualDigest;
	swiSHA1Calc(expectedDigest.data(), astronautBuffer + unlaunchShaOffset,
				(astronautSize + 520) - unlaunchShaOffset);
	if(calculateFileSha1PathOffset(path, actualDigest.data(), unlaunchShaOffset) && expectedDigest == actualDigest)
	{
		// the tmd hasn't changed, no need to do anything
		return true;
	}

	FILE* targetTmd = fopen(path, "wb");
	if (!targetTmd)
	{
		messageBox("\x1B[31mError:\x1B[33m Failed to open target astronaut tmd\n");
		return false;
	}

	if(!writeToFile(targetTmd, astronautBuffer, astronautSize + 520))
	{
		fclose(targetTmd);
		removeIfExists(path);
		messageBox("\x1B[31mError:\x1B[33m Failed write astronaut to tmd\n");
		return false;
	}

	fclose(targetTmd);

	if(!calculateFileSha1PathOffset(path, actualDigest.data(), unlaunchShaOffset) || expectedDigest != actualDigest)
	{
		removeIfExists(path);
		messageBox("\x1B[31mError:\x1B[33m Astronaut tmd was not properly written\n");
		return false;
	}
	return true;
}

static bool writeAstronautToHNAAFolder()
{
	//Create HNAA launcher folder
	if (!safeCreateDir("nand:/title/00030017")
		|| !safeCreateDir("nand:/title/00030017/484e4141")
		|| !safeCreateDir("nand:/title/00030017/484e4141/content")) {
		return false;
	}

	// We have to remove write protect otherwise reinstalling will fail.
	if (fileExists(hnaaTmdPath) && !toggleFileReadOnly(hnaaTmdPath, false)) {
		messageBox("\x1B[31mError:\x1B[33m Can't remove launcher tmd write protect\n");
		return false;
	}
	if (!writeAstronautTmd(hnaaTmdPath))
	{
		removeHnaaLauncher();
		return false;
	}

	//Mark the tmd as readonly
	if(!toggleFileReadOnly(hnaaTmdPath, true))
	{
		messageBox("\x1B[31mError:\x1B[33m Failed to mark astronaut's title.tmd as read only\n");
		removeHnaaLauncher();
		return false;
	}
	return true;
}

static bool installAstronautRetailConsole(const consoleInfo& info)
{
	if(!writeAstronautToHNAAFolder())
		return false;

	//Finally patch the default launcher tmd to be invalid
	//If there isn't a title.tmd matching the language region in the hwinfo
	// nothing else has to be done, could be a language patch, or a dev system, the user will know what they have done
	if (!info.tmdFound)
		return true;

	// Set tmd as writable in case unlaunch was already installed through the old method
	if(!toggleFileReadOnly(info.launcherTmdPath.data(), false) || !patchMainTmd(info.launcherTmdPath.data()))
	{
		removeHnaaLauncher();
		return false;
	}
	if (!toggleFileReadOnly(info.launcherTmdPath.data(), true) || !toggleFileReadOnly(info.launcherAppPath.data(), true))
	{
		messageBox("\x1B[31mError:\x1B[33m Failed to mark default launcher's title.tmd\nas read only, install might be unstable\n");
	}
	return true;
}

static bool installAstronautProtoConsole(void)
{
	if(choiceBox("Your DSi has a non-standard\nregion.\n"
				"\x1B[31mInstalling astronaut may be\n"
				"unsafe.\x1B[33m"
				"\nCancelling is recommended!"
				"\n\nContinue anyways?") == NO)
		return false;

	// Prototypes DSis are always HNAA. We can't use code that will nuke their launcher.

	// Also some justification for adding proto support: they're really common.
	// "Real" protos (X3, X4, etc) are hard to find but there are tons of release
	// version DSis that are running prototype firmware.
	// Likely factory rejects that never had production firmware flashed.

	// We have to remove write protect otherwise reinstalling will fail.
	if (fileExists(hnaaTmdPath) && !toggleFileReadOnly(hnaaTmdPath, false)) {
		messageBox("\x1B[31mError:\x1B[33m Can't remove launcher tmd write protect\n");
		return false;
	}
	bool hnaaBackupExists = fileExists(hnaaBackupTmdPath);
	// Back up the TMD since we'll be writing to it directly.
	if (!hnaaBackupExists)
	{
		rename(hnaaTmdPath, hnaaBackupTmdPath);
		// Mark backup tmd as readonly, just to be sure
		toggleFileReadOnly("nand:/title/00030017/484e4141/content/title.tmd.bak", true);
	}

	if(!writeAstronautTmd(hnaaTmdPath))
	{
		copyFile("nand:/title/00030017/484e4141/content/title.tmd.bak", hnaaTmdPath);
		return false;
	}

	// Mark the tmd as readonly
	if (!toggleFileReadOnly(hnaaTmdPath, true))
	{
		// There is nothing that can be done at this point.
		messageBox("\x1B[31mError:\x1B[33m Failed to mark tmd as read only\n");
	}
	return true;
}

static bool readAstronautInstaller(std::string_view path)
{
	FILE* astronaut = fopen(path.data(), "rb");
	if (!astronaut)
	{
		messageBox("\x1B[31mError:\x1B[33m Failed to open astronaut file\n");
		return false;
	}

	astronautSize = getFileSize(astronaut);
	if(!isValidAstronautSize(astronautSize))
	{
		messageBox("\x1B[31mError:\x1B[33m Astronaut file is wrong size\n");
		return false;
	}

	// Pad the installer with 520 bytes, those being the size of a valid tmd
	auto readAmount = readFileAll(astronaut, astronautBuffer + 520, sizeof(astronautBuffer) - 520);

	fclose(astronaut);

	if(readAmount != astronautSize)
	{
		messageBox("\x1B[31mError:\x1B[33m Failed to read astronaut file\n");
		return false;
	}
	return true;
}

static bool verifyAstronaut(void)
{
	if(installerVersion == ASTRONAUT_NIGHTLY) {
		Sha1Digest digest;
		swiSHA1Calc(digest.data(), astronautBuffer + 520,  astronautSize-SHA1_LEN);
		Sha1Digest self_digest;
		memccpy(self_digest.data(), astronautBuffer+520+astronautSize-SHA1_LEN, sizeof(char), SHA1_LEN);
		if(digest != self_digest) {
			messageBox("\x1B[31mError:\x1B[33m Provided astronaut has a bad SHA-1 hash\n");
			return false;
		}
	} else {
		Sha1Digest digest;
		swiSHA1Calc(digest.data(), astronautBuffer + 520,  astronautSize);
		auto it = std::ranges::find(knownAstronautHashes, digest);
		if(it == knownAstronautHashes.end())
		{
			messageBox("\x1B[31mError:\x1B[33m Provided astronaut has an unknown hash\n");
			return false;
		}
		auto idx = std::distance(knownAstronautHashes.begin(), it);
		installerVersion = static_cast<ASTRONAUT_VERSION>(idx);
	}
	return true;
}



ASTRONAUT_VERSION loadAstronaut(std::string_view path, ASTRONAUT_VERSION assumption)
{
	if(assumption == ASTRONAUT_NIGHTLY) {
		installerVersion = ASTRONAUT_NIGHTLY;
	}
	if(readAstronautInstaller(path) && verifyAstronaut())
	{
		tonccpy(ogAstronautBuffer, astronautBuffer, sizeof(astronautBuffer));
		return installerVersion;
	}
	return INVALID;
}

std::array astronautVersionStrings{
	"NIGHTLY",
	"INDEV",
	"0.1.0",
	"INVALID",
};

static_assert(astronautVersionStrings.size() == (INVALID + 1));

const char* getAstronautVersionString(ASTRONAUT_VERSION version)
{
	return astronautVersionStrings[version];
}


bool installAstronaut(const consoleInfo& info)
{
	if (installerVersion == INVALID)
		return false;

	if (installerVersion == ASTRONAUT_NIGHTLY)
		annoyer();

	// Treat protos differently
	if (!info.isRetail)
	{
		return installAstronautProtoConsole();
	}
	// Do things normally for production units
	return installAstronautRetailConsole(info);
}
