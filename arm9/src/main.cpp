#include <cstdio>
#include <dirent.h>
#include <string_view>
#include <string>
#include <format>
#include <exception>
#include <memory>

#include <filesystem.h>
#include <nds/arm9/dldi.h>


#include "consoleInfo.h"
#include "main.h"
#include "menu.h"
#include "message.h"
#include "nand/nandio.h"
#include "storage.h"
#include "version.h"
#include "unlaunch.h"
#include "nocashFooter.h"
#include "update.h"
#include "buttons.h"

using namespace std::string_view_literals;

volatile bool programEnd = false;
static volatile bool arm7Exiting = false;
static bool retailLauncherTmdPresentAndToBePatched = true;
static ASTRONAUT_VERSION foundAstronautVersion = INVALID;
static std::span<uint8_t> customBgSpan{};
static bool advancedOptionsUnlocked = false;
static bool isLauncherVersionSupported = true;
static bool fatTouched = false;

PrintConsole topScreen;
PrintConsole bottomScreen;

int bgGifTop;
int bgGifBottom;

static struct {
	uint64_t consoleId;
	uint32_t cid[4];
} consoleIdAndCid;

struct Stage2 {
	Sha1Digest sha;
	bool unlaunch_supported;
};

// these are SHA1 checksums of the first block of every stage 2 known so far
// https://docs.randommeaninglesscharacters.com/stage2.html
static constexpr std::array knownStage2s{
	Stage2{"dd95fd20026925fbaaa5641517758e41397be27d"_sha1, true},  // v2435-8325_prod
	Stage2{"f546ee3cb23617b39205f6eaaa127ed347c4a132"_sha1, true},  // v2435-8325_dev
	Stage2{"8d99c1c8cf82cb672d9b3ecd587ef9eb4f4aca2d"_sha1, true},  // v2665-9336_prod
	Stage2{"7005bb39f6e2e3d2c627079b1c2d15a9b5045801"_sha1, true},  // v2725-9336_dev
	Stage2{"d734155da34c4789847b7e5fe8a7a84e131064e9"_sha1, false}, // SDMC_20080821-134255_dev
	Stage2{"70c647961d5216a3801d9b48170b294a58fe3c2e"_sha1, false}, // v1935-7470_dev
	Stage2{"fc76bd0f41f53ea8610cde197d965f5723af779a"_sha1, false}, // v1935-7470_prod
	Stage2{"4b476c6aacb5e867c025a54ad6166e423ce81293"_sha1, false}, // v2262-8067_dev
	Stage2{"d35d0870ddaf49f3db675a663c759f15dbfccd7e"_sha1, false}, // v2262-8067_prod
	Stage2{"2611443b63b94d46b4c71810d84c7b93fd5bd594"_sha1, false}, // vNONE-NONE_Unknown_dev
	Stage2{"a8b5a025378a1d0c7bbb2cf50cab6edf7b9bc312"_sha1, false}, // vNONE-NONE_Updater_dev
	Stage2{"2611443b63b94d46b4c71810d84c7b93fd5bd594"_sha1, false}, // vNONE-NONE_X4_dev
	Stage2{"0caa17616108b26f83bd98256ad0350f37504e75"_sha1, false}, // vNONE-NONE_X6_prod
};

enum {
	MAIN_MENU_SAFE_UNINSTALL,
	MAIN_MENU_SAFE_INSTALL,
	//MAIN_MENU_SEARCH_FOR_UPDATES,
	MAIN_MENU_EXIT,
	MAIN_MENU_SAFE_UNINSTALL_NO_BACKUP,
	MAIN_MENU_WRITE_NOCASH_FOOTER_ONLY,
	
};

u16* batteries[20];

static int timer = 0; 
void show_battery() {  
	u32 value = getBatteryLevel();
	unsigned int battery_level = value & BATTERY_LEVEL_MASK;
	bool charger_connected = value & BATTERY_CHARGER_CONNECTED;
	timer++;
	u16* ptr = charger_connected ? batteries[16+((timer&48) >> 4)] : batteries[battery_level];
    oamSet(&oamSub, 0,
            2, 173, // X, Y
            0, // Priority
            0, // Palette index
            SpriteSize_32x16, SpriteColorFormat_16Color, // Size, format
            ptr,  // Graphics offset
            -1, // Affine index
            false, // Double size
            false, // Hide
            false, false, // H flip, V flip
            false); // Mosaic
	oamUpdate(&oamSub);
}

void copy_subsprite(void *dst, int sprite)
{
    int frame_size = 32 * 16 / 2;
    int offset = frame_size * sprite;
    uint8_t *base = (uint8_t *)buttonsTiles;

    memcpy(dst, base + offset, frame_size);
}
static void initSprites() {
	oamInit(&oamSub, SpriteMapping_1D_128, false); 
	for (unsigned int i = 0; i < sizeof(batteries)/sizeof(u16*); i++)
    {
        batteries[i] = oamAllocateGfx(&oamSub, SpriteSize_32x16,
                                         SpriteColorFormat_16Color);
        copy_subsprite(batteries[i], i);
    }
    memcpy(SPRITE_PALETTE_SUB, buttonsPal, buttonsPalLen);
}
static void setupScreens()
{
	videoSetMode(MODE_5_2D);
	videoSetModeSub(MODE_5_2D);

	vramSetBankA(VRAM_A_MAIN_BG);
	vramSetBankD(VRAM_D_SUB_SPRITE);
	vramSetBankC(VRAM_C_SUB_BG);

	initSprites();

	consoleInit(&topScreen, 1, BgType_Text4bpp, BgSize_T_256x256, 14, 0, true, true);
	consoleEnhancedColorHandler(NULL);
	consoleInit(&bottomScreen, 1, BgType_Text4bpp, BgSize_T_256x256, 14, 0, false, true);
	consoleEnhancedColorHandler(NULL);
	clearScreen(&bottomScreen);

	bgGifTop = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 2, 0);
	bgHide(bgGifTop);
	bgGifBottom = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 2, 0);
	bgHide(bgGifBottom);

	VRAM_A[100] = 0xFFFF;
}

static int mainMenu(const consoleInfo& info, int cursor)
{
	//top screen
	clearScreen(&topScreen);

	printf("\t\"Safe\" astronaut installer\n");
	printf("\nversion %s\n", VERSION);
	printf("\n\n\x1b[91mWARNING:\x1b[97m This tool can write to"
			"\nyour internal NAND!"
			"\n\nThis always has a risk, albeit"
			"\nlow, of \x1b[91mbricking\x1b[97m your system"
			"\nand should be done with caution!\n");
	printf("\n\t  \x1b[96mhttps://dsi.cfw.guide\x1b[97m\n");
	printf("\x1b[23;0Hedo9300, fork by vikrinox - 2026");

	//menu
	Menu* m = newMenu();
	setMenuHeader(m, "MAIN MENU");

	auto [restore_string, restore_string_no_backup] = [&]{
		if(info.tmdInvalid) {
			return std::make_pair("Restore launcher tmd", "Restore launcher tmd no backup");
		}
		return std::make_pair("Uninstall astronaut", "Uninstall astronaut no backup");
	}();

	char installAstronautStr[32];
	if(foundAstronautVersion != INVALID)
	{
		sprintf(installAstronautStr, "Install astronaut (%s)", getAstronautVersionString(foundAstronautVersion));
	}
	else
	{
		strcpy(installAstronautStr, "Install astronaut");
	}
	addMenuItem(m, restore_string, NULL, !info.isStockTmd() && isLauncherVersionSupported, false);
	addMenuItem(m, installAstronautStr, NULL, foundAstronautVersion != INVALID && info.isStockTmd() && isLauncherVersionSupported, false);
	//addMenuItem(m, "Look for updates", NULL, true, false);
	addMenuItem(m, "Exit", NULL, true, false);
	if(!isLauncherVersionSupported)
	{
		addMenuItem(m, restore_string_no_backup, NULL, !info.isStockTmd(), false);
	}
	else if(advancedOptionsUnlocked)
	{
		addMenuItem(m, restore_string_no_backup, NULL, !info.isStockTmd(), false);
		addMenuItem(m, "Write nocash footer", NULL, info.needsNocashFooterToBeWritten, false);
	}

	m->cursor = cursor;

	//bottom screen
	printMenu(m);

	int konamiCode = 0;
	bool konamiCodeCooldown = false;

	while (!programEnd)
	{
		rand();
		swiWaitForVBlank();
		show_battery();
		scanKeys();

		if (moveCursor(m))
			printMenu(m);

		if (keysDown() & KEY_A)
			break;

		if(advancedOptionsUnlocked)
			continue;

		int held = keysHeld();

		if ((held & (KEY_L | KEY_R | KEY_Y)) == (KEY_L | KEY_R | KEY_Y))
		{
			if(held == (KEY_L | KEY_R | KEY_Y) && !konamiCodeCooldown)
			{
				konamiCodeCooldown = true;
				++konamiCode;
			}
		}
		else
		{
			konamiCodeCooldown = false;
		}
		if (konamiCode == 5)
		{
			advancedOptionsUnlocked = true;
			// Enabled by default when unsupported
			if(isLauncherVersionSupported)
			{
				addMenuItem(m, restore_string_no_backup, NULL, !info.isStockTmd(), false);
			}
			addMenuItem(m, "Write nocash footer", NULL, info.needsNocashFooterToBeWritten, false);
		}
	}

	int result = m->cursor;
	freeMenu(m);

	return result;
}



void setup() {
	keysSetRepeat(25, 5);
	setupScreens();
	

	fifoSetValue32Handler(FIFO_USER_01, [](u32 value32, void*) {
		if (value32 != 0x54495845) // 'EXIT'
			return;
		programEnd = true;
		arm7Exiting = true;
	}, NULL);

	//DSi check
	if (!isDSiMode())
	{
		messageBox("\x1b[91mError:\x1b[93m This app is exclusively for DSi.");
		exit(0);
	}

	fifoWaitDatamsg(FIFO_USER_02);
	fifoGetDatamsg(FIFO_USER_02, sizeof(consoleIdAndCid), (u8*)&consoleIdAndCid);

	//setup sd card and nand access
	if (!fatInitDefault())
	{
		messageBox("fatInitDefault()...\x1b[91mFailed\n\x1b[97m");
	}

	if (!nandInit(false))
	{
		messageBox("\x1b[91mFailed to mount NAND\n\x1b[97m");
	}

	bool isMBR = []{
		// good ol' "buffer being passed to the arm7"
		static std::array<std::uint8_t, 512> secBuff;
		get_io_dsisd()->readSectors(0, 1, secBuff.data());
		return secBuff[510] == 0x55 && secBuff[511] == 0xAA;
	}();

	if(!isMBR)
	{
		messageBox("\x1b[91mWARNING:\x1b[97m This SD is not\n"
				   "formatted as MBR, required by\n"
				   "Unlaunch/Astronaut to work.\n"
				   "If you install it, Unlaunch\n"
				   "won't boot as long as this SD\n"
				   "card is inserted.");
	}
}

void checkStage2Supported() {
	Sha1Digest digest;
	nandio_calculate_stage2_sha(digest.data());
	for(const auto& [sha, unlaunch]: knownStage2s) {
		if(sha == digest) {
			if(!unlaunch) {
				messageBox("\x1b[91mError:\x1b[93m A known stage2 was found but is not compatible with\n"
						   "astronaut.");
				exit(0);
			}
			return;
		}
	}
	messageBox("\x1b[91mError:\x1b[93m An unknown stage2\n"
			   "was found. This is a rare find,\n"
			   "you should look for help\n"
			   "archiving and documenting\n"
			   "your nand");
	exit(0);
}


static char verstring[10] = {0};
char* versionString() {
	return verstring;
}
void setupNitrofs() {
	for(const auto& path : {std::string_view{}, "sd:/ntrboot.nds"sv, "sd:/boot.nds"sv}) {
		if(!nitroFSInit(path.data()))
			continue;
		auto* file = fopen("nitro:/installer.ver", "rb");
		if(!file)
			continue;
		fread(verstring, 1, sizeof(verstring) - 1, file);
		fclose(file);
		if(std::string_view{verstring} == VERSION)
			return;
	}
	messageBox("nitroFSInit()...\x1b[91mFailed\n\x1b[97m");
	exit(0);
}

void checkNocashFooter(consoleInfo& info) {
	NocashFooter footer;

	nandio_read_nocash_footer(&footer);
	constructNocashFooter(&info.nocashFooter, (u8*)consoleIdAndCid.cid, (u8*)&consoleIdAndCid.consoleId);

	info.needsNocashFooterToBeWritten = !isFooterValid(&footer);

	if(!info.needsNocashFooterToBeWritten)
	{
		if(memcmp(&footer, &info.nocashFooter, sizeof(footer)) != 0)
		{
			messageBox("\x1b[91mError:\x1b[93m This console has a\n"
					   "nocash footer embedded in its\n"
					   "nand that doesn't match the one\n"
					   "generated.\n"
					   "The footer already present will\n"
					   "be overwritten.");
			info.needsNocashFooterToBeWritten = true;
		}
	}
}

bool writeNocashFooter(consoleInfo& info) {
	if(!info.needsNocashFooterToBeWritten)
		return true;

	nand_WriteProtect(false);
	printf("Writing nocash footer\n");
	auto res = nandio_write_nocash_footer(&info.nocashFooter);
	nand_WriteProtect(true);

	if(!res)
	{
		messageBox("Failed to write nocash footer");
		return false;
	}
	info.needsNocashFooterToBeWritten = false;
	return true;
}

void waitForBatteryChargedEnough() {
	// 7 is 2 battery bars, require at least that, if charger is plugged in
	// bit 7 will be set, making this value greater than 7
	while (getBatteryLevel() < 7 && !programEnd)
	{
		if (choiceBox("\x1b[97mBattery is too low!\nPlease plug in the console.\n\nContinue?") == NO)
			exit(0);
	}
}

void loadAstronaut() {
	if (fileExists("sd:/astronaut_nightly.bin")) {
		foundAstronautVersion = loadAstronaut("sd:/astronaut_nightly.bin", ASTRONAUT_NIGHTLY);
		if(foundAstronautVersion != INVALID)
			return;
	}
	if (fileExists("sd:/astronaut"))
	{
		foundAstronautVersion = loadAstronaut("sd:/astronaut.bin", ASTRONAUT_0_2_0);
		if(foundAstronautVersion != INVALID)
			return;

		messageBox("\x1b[91mWARNING:\x1b[97m Failed to load astronaut\n"
				   "from the root of the sd card.\n"
				   "Attempting to use the bundled one.");
	}

	foundAstronautVersion = loadAstronaut("nitro:/astronaut.bin", ASTRONAUT_0_2_0);

	if(foundAstronautVersion != INVALID)
		return;

	messageBox("\x1b[91mWARNING:\x1b[97m Failed to load bundled astronaut\n"
			   "installer.\n"
			   "Installing astronaut won't be possible.");
}



void parseLauncherInfo(std::string_view launcher_tid_str, consoleInfo& info) {
	auto launcher_content_path = std::format("nand:/title/00030017/{}/content", launcher_tid_str);

	auto [tmd_found, expected_launcher_build, retailLauncherPath] = [&] {
		std::shared_ptr<DIR> pdir{opendir(launcher_content_path.c_str()), closedir};
		if (!pdir)
			throw std::runtime_error(std::format("Could not open launcher title directory ({})", launcher_content_path));
		dirent* pent;
		std::optional<std::pair<uint32_t, std::string>> foundApp;
		bool tmdFound;
		std::string error_str = "Launcher app not found";
		while((pent = readdir(pdir.get())) != nullptr) {
			if(foundApp && tmdFound) {
				break;
			}
			if(pent->d_type == DT_DIR)
				continue;
			std::string_view filename{pent->d_name};
			if(filename == "title.tmd") {
				tmdFound = true;
				continue;
			}

			if(filename.size() != 12 || !filename.ends_with(".app"))
				continue;

			auto launcher_app_path = std::format("{}/{}", launcher_content_path, filename);
			auto f = fopen(launcher_app_path.data(), "rb");
			uint8_t buff[0x20];
			auto read = fread(buff, 0x20, 1, f);
			fclose(f);

			static constexpr std::array<uint8_t, 0xF> hna
				{'L','A','U','N','C','H','E','R','\0','\0','\0','\0','H','N','A'};

			if(read != 1 || !std::ranges::equal(hna, std::span{buff, buff+0xF})) {
				error_str.append(std::format("\ntried: {}", filename));
				continue;
			}

			uint16_t launcher_app_version;
			memcpy(&launcher_app_version, &buff[0x1E], 2);
			if(launcher_app_version > 7)
				throw std::runtime_error(std::format("Found an unsupported launcher version: {}", launcher_app_version));

			foundApp = std::make_pair(static_cast<uint32_t>(256 * launcher_app_version), std::string{filename});
		}
		if(!foundApp)
			throw std::runtime_error(error_str);
		const auto& [launcher_build, launcher_app_name] = *foundApp;
		return std::make_tuple(tmdFound, launcher_build, std::format("{}/{}", launcher_content_path, launcher_app_name));
	}();

	if((info.tmdFound = tmd_found)) {
		const auto recoveryTmdPath = std::format("nitro:/{}/tmd.{}", launcher_tid_str, static_cast<int>(expected_launcher_build));
		info.launcherTmdPath = std::format("{}/title.tmd", launcher_content_path);
		info.recoveryTmdDataSha = [&] -> Sha1Digest {
			auto file = fopen(std::format("{}.sha1", recoveryTmdPath).data(), "rb");
			if(!file)
				throw std::runtime_error("Good tmd sha1 not found");
			char sha1StrBuff[41]{};
			auto read = fread(sha1StrBuff, sizeof(sha1StrBuff) - 1, 1, file);
			fclose(file);
			if(read != 1)
				throw std::runtime_error("Failed to parse good tmd's sha1 file");
			return {sha1StrBuff};
		}();

		auto patchedTmdSha1 = [&] -> Sha1Digest {
			auto file = fopen(std::format("{}.patch.sha1", recoveryTmdPath).data(), "rb");
			if(!file)
				throw std::runtime_error("Patched tmd sha1 not found");
			char sha1StrBuff[41]{};
			auto read = fread(sha1StrBuff, sizeof(sha1StrBuff) - 1, 1, file);
			fclose(file);
			if(read != 1)
				throw std::runtime_error("Failed to parse patched tmd's sha1 file");
			return {sha1StrBuff};
		}();

		info.recoveryTmdData = [&] {
			auto* sourceTmd = fopen(recoveryTmdPath.data(), "rb");

			std::array<uint8_t, 520> ret;
			auto read = fread(ret.data(), ret.size(), 1, sourceTmd);
			fclose(sourceTmd);
			if(read != 1)
			{
				throw std::runtime_error("Failed to read good tmd's buffer");
			}

			Sha1Digest digest;
			swiSHA1Calc(digest.data(), ret.data(), ret.size());
			if(digest != info.recoveryTmdDataSha)
			{
				throw std::runtime_error("Good tmd's sha mismatching");
			}
			return ret;
		}();

		std::shared_ptr<FILE> tmd{fopen(info.launcherTmdPath.data(), "rb"), [](auto* ptr){ if(ptr) fclose(ptr);}};
		if(!tmd) {
			info.tmdFound = false;
		} else if(auto tmdSize = getFileSize(tmd.get()); tmdSize < 520) {
			//if size isn't at least 520 then the tmd is already invalid
			info.tmdInvalid = true;
		} else {
			info.launcherAppPath = retailLauncherPath;
			if(tmdSize > 520) {
				info.tmdInvalid = true;
			}
			else
			{
				Sha1Digest digest;
				calculateFileSha1(tmd.get(), &digest);
				if(digest == info.recoveryTmdDataSha){
					info.tmdGood = true;
				} else if(digest == patchedTmdSha1) {
					info.tmdPatched = true;
				} else {
					info.tmdInvalid = true;
				}
			}
			if(!info.tmdInvalid) {
				fseek(tmd.get(), 0x1DC, SEEK_SET);
				uint16_t launcherVersion;
				fread(&launcherVersion, sizeof(launcherVersion), 1, tmd.get());
				if(static_cast<uint32_t>(launcherVersion) * 256 != expected_launcher_build) {
					throw std::runtime_error("Launcher version found doesn't match with the one in the tmd");
				}
				info.launcherVersion = launcherVersion;
			}
		}
		if(info.tmdInvalid || !info.tmdFound) {
			// if the tmd is invalid, don't read the launcher version from it and assume it's the one
			// matching the app file
			info.launcherVersion = expected_launcher_build / 256;
		}
	}
}

void retrieveInstalledLauncherInfo(consoleInfo& info) {
	static constexpr auto hnaaTmdPath = "nand:/title/00030017/484e4141/content/title.tmd"sv;
	const auto [launcher_tid_str, region] = [] -> std::pair<std::string, u8> {
		uint32_t launcherTid;
		{
			auto* file = fopen("nand:/sys/HWINFO_S.dat", "rb");
			if(!file)
				return std::make_pair("", static_cast<u8>(0xFF));
			fseek(file, 0xA0, SEEK_SET);
			fread(&launcherTid, sizeof(uint32_t), 1, file);
			fclose(file);
		}
		return std::make_pair(std::format("{:08x}", launcherTid), static_cast<u8>(launcherTid & 0xFF));
	}();

	// I own and know of many people with retail and dev prototypes
	// These can normally be identified by having the region set to ALL (0x41)
	info.isRetail = (region != 0x41 && region != 0xFF);

	//check for unlaunch and region
	if (info.isRetail && launcher_tid_str.size() != 0) {
		parseLauncherInfo(launcher_tid_str, info);
	} else {
		// HWINFO_S may not always exist (PRE_IMPORT). Fill in defaults if that happens.
		(void)0;
	}

	if (auto tmdSize = getFileSizePath(hnaaTmdPath.data()); tmdSize > 520) {
		info.ModdedHNAAtmdFound = true;
	}
}

void uninstall(consoleInfo& info, bool noBackup) {
	if(info.isStockTmd())
	{
		return;
	}
	bool unsafeUninstall = advancedOptionsUnlocked && noBackup;
	if(!isLauncherVersionSupported && !unsafeUninstall)
	{
		return;
	}
	printf("Uninstalling");
	if(!writeNocashFooter(info))
	{
		return;
	}
	nand_WriteProtect(false);
	if(uninstallAstronaut(info, unsafeUninstall))
	{
		messageBox("Uninstall successful!\n");
		info.tmdInvalid = false;
		info.tmdPatched = false;
		info.tmdGood = true;
		info.ModdedHNAAtmdFound = !unsafeUninstall;
		fatTouched = true;
	}
	else
	{
		messageBox("\x1b[91mError:\x1b[93m Uninstall failed\n");
	}
	nand_WriteProtect(true);
}

void install(consoleInfo& info) {
	if(!isLauncherVersionSupported)
	{
		return;
	}
	if(!info.isStockTmd())
	{
		return;
	}
	if(foundAstronautVersion == INVALID)
	{
		return;
	}
	if(choiceBox("Install astronaut?") == NO)
	{
		return;
	}
	if(!retailLauncherTmdPresentAndToBePatched
		&& (choiceBox("There doesn't seem to be a launcher.tmd\n"
					  "file matcing the hwinfo file\n"
					  "Keep installing?") == NO))
	{
		return;
	}
	printf("Installing\n");
	if(!writeNocashFooter(info))
	{
		return;
	}
	nand_WriteProtect(false);
	if(installAstronaut(info))
	{
		messageBox("Install successful!\n");
		info.tmdGood = false;
		info.tmdPatched = true;
		info.ModdedHNAAtmdFound = true;
		fatTouched = true;
	}
	else
	{
		messageBox("\x1b[91mError:\x1b[93m Install failed\n");
	}
	nand_WriteProtect(true);
}


static std::string our_path = "sd:/astronaut-installer.dsi";
void doUpdate() {
	if(int error = lookForUpdates(our_path)) {
		char buffer[100] = {0};
		snprintf(buffer, 100, "Update Failed, error code %d", error);
		char const* error_message = buffer;
		switch(error) {
			
			case UPDATE_VERSION_ALREADY_LATEST:
			error_message = "You're already up to date!";
			break;
			case UPDATE_VERSION_INVALID:
			error_message = "Failed to fetch version info.";
			break;
			case UPDATE_VERSION_CHECK_FAILED:
			error_message = "Failed to check for updates.";
			break;
			case UPDATE_CANCELLED:
			error_message = "Update cancelled.";
			break;
			case UPDATE_BAD_WIFI_INIT:
			error_message = "Failed to initialize WiFi.";
			break;
			case UPDATE_BAD_WIFI_UNINIT:
			error_message = "Failed to uninitialize WiFi.";
			break;
			case UPDATE_BAD_TLS_SEED:
			error_message = "Failed to establish TLS seed.";
			break;
			case UPDATE_BAD_TLS_CERTS:
			error_message = "Failed to establish TLS\n"
							"certificates.";
			break;
			case UPDATE_BAD_TLS_CONNECT:
			error_message = "Failed to establish TLS\n"
							"connection.";
			break;
			case UPDATE_BAD_TLS_CONFIG:
			error_message = "Failed to establish TLS\n"
							"configuration.";
			break;
			case UPDATE_BAD_TLS_SETUP:
			error_message = "Failed to setup TLS.";
			break;
			case UPDATE_BAD_TLS_HOSTNAME:
			error_message = "Failed to resolve TLS hostname.";
			break;
			case UPDATE_BAD_TLS_HANDSHAKE:
			error_message = "Failed to perform TLS handshake.";
			break;
			case UPDATE_BAD_TLS_VERIFY:
			error_message = "Failed to verify TLS\n"
							"certificate.";
			break;
			case UPDATE_BAD_DOWNLOAD_COMM:
			error_message = "Download failed.";
			break;
			case UPDATE_BAD_DOWNLOAD_WRITE:
			error_message = "Failed to write downloaded\n"
							"file to SD card.";
			break;
			case UPDATE_BAD_TLS_FINISH:
			error_message = "Failed to close TLS connection.";
			break;
			case UPDATE_BAD_HTTP_CODE:
			error_message = "Recieved an unrecognized HTTP\n"
							"code while fetching the update.";
			break;
			case UPDATE_BAD_REDIRECT:
			error_message = "Failed to follow file redirect.";
			break;
			case UPDATE_BAD_LENGTH:
			error_message = "The downloaded file was\n"
							"not the expected length.";
			break;
			case UPDATE_BAD_VERIFY:
			case UPDATE_BAD_SHA1:
			error_message = "The downloaded file didn't\n"
							"match the expected SHA1 hash.";
			break;
			case UPDATE_NO_INTERNET:
			error_message = "Couldn't establish an \n"
							"internet connection.";
			break;
			case UPDATE_NO_APS:
			error_message = "There are no access points\n"
							"configured, please go to the\n"
							"system settings and set up\n"
							"at least one wifi network.";
			break;
			case UPDATE_UNKNOWN_ERROR:
			default:
			break;
		}
		messageBox(error_message);
		if(error>0) {
			messageBox("Since the update cleanup failed, the console will now restart.");
			programEnd = true;
		}
	} else {
		messageBox("Update successful! The console will now restart.");
		programEnd = true;
	}
}

void doMainMenu(consoleInfo& info) {
	int cursor = 0;
	customBgSpan = {};
	while(!programEnd)
	{
		cursor = mainMenu(info, cursor);
		if(programEnd)
			break;

		switch (cursor)
		{
		case MAIN_MENU_SAFE_UNINSTALL:
		case MAIN_MENU_SAFE_UNINSTALL_NO_BACKUP:
		{
			uninstall(info, cursor == MAIN_MENU_SAFE_UNINSTALL_NO_BACKUP);
		}
		break;

		case MAIN_MENU_SAFE_INSTALL:
		{
			install(info);
		}
		break;

		case MAIN_MENU_WRITE_NOCASH_FOOTER_ONLY:
			(void)writeNocashFooter(info);
			break;
		
		//case MAIN_MENU_SEARCH_FOR_UPDATES:
		//	doUpdate();
		//	break;

		case MAIN_MENU_EXIT:
			programEnd = true;
			return;
		}
	}
}
int main(int argc, char **argv)
{
	setup();
	checkStage2Supported();
	setupNitrofs();

	loadAstronaut();

	try {

		consoleInfo info;

		retrieveInstalledLauncherInfo(info);

		checkNocashFooter(info);

		// Launcher v4, build v1024 (shipped with firmware 1.4.2 (1.4.3 for china and korea)
		// will fail to launch if another tmd withouth appropriate application, or an invalid
		// tmd (in our case the one installed from unlaunch) is found in the HNAA launcher folder
		// there's really no workaround to that, so that specific version is blacklisted and only uninstalling
		// an "officially" installed unlaunch without leaving any backup behind will be allowed
		if(info.launcherVersion == 4) {
			isLauncherVersionSupported = false;
			messageBox("\x1b[91mWARNING:\x1b[97m This system version\n"
					   "doesn't support this install\n"
					   "method, only uninstalling\n"
					   "unaunch without backups will\n"
					   "be possible");
		}

		if(argc > 0) {
			if (strlen(argv[0]) > 4)
			{
				our_path = std::string(argv[0]);
			}
		}
		
		if(!programEnd) {
			messageBox("\x1b[91mWARNING:\x1b[97m This tool can write to\n"
				   "your internal NAND!\n\n"
				   "This always has a risk, albeit\n"
				   "low, of \x1b[91mbricking\x1b[97m your system\n"
				   "and should be done with caution!\n\n"
				   "If you have not yet done so,\n"
				   "you should make a NAND backup.");
			waitForBatteryChargedEnough();

			doMainMenu(info);
		}
	} catch (const std::exception& e) {
		messageBox(e.what());
	}

	if(fatTouched) {
		printf("Synchronizing FAT tables...\n");
		nandio_synchronize_fats();
	}

	clearScreen(&bottomScreen);

	fifoSendValue32(FIFO_USER_02, 0x54495845); // 'EXIT'

	while (arm7Exiting)
		swiWaitForVBlank();

	return 0;
}

void clearScreen(PrintConsole* screen)
{
	consoleSelect(screen);
	consoleClear();
}