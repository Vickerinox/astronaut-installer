#include "message.h"
#include "main.h"

void keyWait(u32 key)
{
	while (!programEnd)
	{
		swiWaitForVBlank();
		scanKeys();

		if (keysDown() & key)
			break;
	}
}

bool choiceBox(const char* message)
{
	const int choiceRow = 10;
	int cursor = 0;

	clearScreen(&bottomScreen);

	printf("\x1b[93m");	//yellow
	printf("%s\n", message);
	printf("\x1b[97m");	//white
	printf("\x1b[%d;0H\tYes\n\tNo\n", choiceRow);

	while (!programEnd)
	{
		swiWaitForVBlank();
		scanKeys();

		//Clear cursor
		printf("\x1b[%d;0H ", choiceRow + cursor);

		if (keysDown() & (KEY_UP | KEY_DOWN))
			cursor = !cursor;

		//Print cursor
		printf("\x1b[%d;0H>", choiceRow + cursor);

		if (keysDown() & (KEY_A | KEY_START))
			break;

		if (keysDown() & KEY_B)
		{
			cursor = 1;
			break;
		}
	}

	scanKeys();
	return (cursor == 0)? YES: NO;
}
void printProgessBar(const char* msg, int progress, int total) {
    int bar = 30 * progress / total;
    printf("\x1b[97m%s (%d/%d KiB)\n", msg, progress>>10, total>>10);
    printf("\x1b[97m[\x1b[90m..............................\x1b[97m]");
	char buf[30] = {0};
    for (int i = 0; i < bar; i++) {
        buf[i] = '=';
    }
	printf("\x1b[31D\x1b[92m%s\x1b[97m", buf);
}

void annoyer() {
	consoleClear();
	printf("You're about to install an\nunverified stage 2 mod. Please\nbe careful and have an unbricking\nmethod like GCDBOOT on hand.\n");
	printf("Press this button combo to continue:\n");

	

	int buttons[] = {KEY_A, KEY_B, KEY_X, KEY_Y, KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN};
	char* button_names[] = {"A", "B", "X", "Y", "LEFT", "RIGHT", "UP", "DOWN"};

	for (int i = 0; i < sizeof(buttons)/sizeof(int); i++) {
		int swap_a = rand() % (sizeof(buttons)/sizeof(int));
		int swap_b = rand() % (sizeof(buttons)/sizeof(int));
		
		int but = buttons[swap_a];
		char* but2 = button_names[swap_a];

		buttons[swap_a] = buttons[swap_b];
		button_names[swap_a] = button_names[swap_b];
		buttons[swap_b] = but;
		button_names[swap_b] = but2;
	}

	start_combo:
	printf("\x1b[10;2f\x1b[97m");
	for (int i = 0; i < sizeof(buttons)/sizeof(int); i++) {
		printf("%s ", button_names[i]);
	}
	printf("\x1b[10;2f\x1b[92m");
	for (int i = 0; i < sizeof(buttons)/sizeof(int); i++) {
		while (!programEnd)
		{
			swiWaitForVBlank();
			scanKeys();

			if (keysDown() & buttons[i])
			{
				printf("%s ", button_names[i]);
				break;
			} else if (keysDown() & ~buttons[i]) {
				goto start_combo;
			}
		}
	}
}

bool choicePrint(const char* message)
{
	bool choice = NO;

	printf("\x1b[93m");	//yellow
	printf("\n%s\n", message);
	printf("\x1b[97m");	//white
	printf("Yes - [A]\nNo  - [B]\n");

	while (!programEnd)
	{
		swiWaitForVBlank();
		scanKeys();

		if (keysDown() & KEY_A)
		{
			choice = YES;
			break;
		}

		else if (keysDown() & KEY_B)
		{
			choice = NO;
			break;
		}
	}

	scanKeys();
	return choice;
}

const static u16 keys[] = {KEY_UP, KEY_DOWN, KEY_RIGHT, KEY_LEFT, KEY_A, KEY_B, KEY_X, KEY_Y};
const static char *keysLabels[] = {"\x18", "\x19", "\x1A", "\x1B", "<A>", "<B>", "<X>", "<Y>"};

bool randomConfirmBox(const char* message)
{
	const int choiceRow = 10;
	int sequencePosition = 0;

	u8 sequence[8];
	for (int i = 0; i < sizeof(sequence); i++)
	{
		sequence[i] = rand() % (sizeof(keys) / sizeof(keys[0]));
	}

	clearScreen(&bottomScreen);

	printf("\x1b[93m");	//yellow
	printf("%s\n", message);
	printf("\x1b[97m");	//white
	printf("\n<START> cancel\n");

	while (!programEnd && sequencePosition < sizeof(sequence))
	{
		swiWaitForVBlank();
		scanKeys();

		//Print sequence
		printf("\x1b[%d;0H", choiceRow);
		for (int i = 0; i < sizeof(sequence); i++)
		{
			printf("\x1B[%0om", i < sequencePosition ? 032 : 047);
			printf("%s ", keysLabels[sequence[i]]);
		}

		if (keysDown() & (KEY_UP | KEY_DOWN | KEY_RIGHT | KEY_LEFT | KEY_A | KEY_B | KEY_X | KEY_Y))
		{
			if (keysDown() & keys[sequence[sequencePosition]])
				sequencePosition++;
			else
				sequencePosition = 0;
		}

		if (keysDown() & KEY_START)
		{
			sequencePosition = 0;
			break;
		}
	}

	scanKeys();
	return sequencePosition == sizeof(sequence);
}

void messageBox(const char* message)
{
	clearScreen(&bottomScreen);
	messagePrint(message);
}

void messagePrint(const char* message)
{
	printf("%s\n", message);
	printf("\nOkay - [A]\n");

	while (!programEnd)
	{
		swiWaitForVBlank();
		scanKeys();

		if (keysDown() & (KEY_A | KEY_B | KEY_START))
			break;
	}

	scanKeys();
}
