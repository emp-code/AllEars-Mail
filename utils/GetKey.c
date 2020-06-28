#include <stdbool.h>
#include <termios.h>
#include <ctype.h> // for isxdigit
#include <fcntl.h> // for open
#include <stdio.h>
#include <string.h>
#include <unistd.h> // for write

#include <sodium.h>

static void toggleEcho(const bool on) {
	struct termios t;
	if (tcgetattr(STDIN_FILENO, &t) != 0) return;

	if (on) {
		t.c_lflag |= ((tcflag_t)ECHO);
		t.c_lflag |= ((tcflag_t)ICANON);
	} else {
		t.c_lflag &= ~((tcflag_t)ECHO);
		t.c_lflag &= ~((tcflag_t)ICANON);
	}

	tcsetattr(STDIN_FILENO, TCSANOW, &t);
}

int getKey(unsigned char * const master) {
	toggleEcho(false);

	puts("Enter Master Key (hex) - will not echo");

	char masterHex[crypto_secretbox_KEYBYTES * 2];
	for (unsigned int i = 0; i < crypto_secretbox_KEYBYTES * 2; i++) {
		const int gc = getchar();
		if (gc == EOF || !isxdigit(gc)) {toggleEcho(true); return -1;}
		masterHex[i] = gc;
	}

	toggleEcho(true);

	sodium_hex2bin(master, crypto_secretbox_KEYBYTES, masterHex, crypto_secretbox_KEYBYTES * 2, NULL, NULL, NULL);
	sodium_memzero(masterHex, crypto_secretbox_KEYBYTES * 2);
	return 0;
}