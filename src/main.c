#include <stdio.h>
#include <stdint.h>
#include <signal.h>

#ifdef _WIN32
#include <Windows.h>
#include <conio.h>
#else
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/termios.h>
#include <sys/mman.h>
#endif

#define MEMORY_MAX (1 << 16)
uint16_t mem[MEMORY_MAX];	// 65536 locations (2^16)

/* registers */

enum {

	R_R0 = 0,
	R_R1,
	R_R2,
	R_R3,
	R_R4,
	R_R5,
	R_R6,
	R_R7,
	R_PC,
	R_COND,
	R_COUNT

};

uint16_t reg[R_COUNT];

/* opcodes */

enum {

	OP_BR = 0, 	// branch
	OP_ADD, 	// add
	OP_LD,		// load
	OP_ST,		// store
	OP_JSR,		// jump register
	OP_AND,		// bitwise and
	OP_LDR,		// load register
	OP_STR,		// store register
	OP_RTI,		// unused
	OP_NOT,		// bitwise not
	OP_LDI,		// load indirect
	OP_STI,		// store indirect
	OP_JMP,		// jump
	OP_RES,		// reserved (unused)
	OP_LEA,		// load effective address
	OP_TRAP		// execute trap

};

/* condition flags */

enum {

	FL_POS = 1 << 0,
	FL_ZRO = 1 << 1,
	FL_NEG = 1 << 2,

};

/* trap codes */

enum {

	TRAP_GETC = 0x20,	// get char from keyboard, not echoed
	TRAP_OUT = 0x21,	// output a char
	TRAP_PUTS = 0x22,	// output a string
	TRAP_IN = 0x23,		// get char from keyboard, echoed
	TRAP_PUTSP = 0x24,	// output a byte string
	TRAP_HALT = 0x25	// halt the program

};

/* mem mapped registers */

enum {

	MR_KBSR = 0xFE00,	// keyboard status
	MR_KBDR = 0xFE02	// keyboard data

};

#ifdef _WIN32

HANDLE h_stdin = INVALID_HANDLE_VALUE;
DWORD fdw_mode, fdw_old_mode;

void disable_input_buffering() {
	hStdin = GetStdHandle(STD_INPUT_HANDLE);
	GetConsoleMode(h_stdin, &fdw_old_mode);
	fdw_mode = fdw_old_mode
			^ ENABLE_ECHO_INPUT
			^ ENABLE_LINE_INPUT;
	SetConsoleMode(h_stdin, fdw_mode);
	FlushConsoleInputBuffer(h_stdin);
}

void restore_input_buffering() {
	SetConsoleMode(h_stdin, fdw_old_mode);
}

uint16_t check_key() {
	return WaitForSingleObject(h_stdin, 1000) == WAIT_OBJECT_0 && _kbhit();
}

#else

struct termios original_tio;

void disable_input_buffering() {
	tcgetattr(STDIN_FILENO, &original_tio);
	struct termios temp_tio = original_tio;
	temp_tio.c_lflag &= ~ICANON & ~ECHO;
	tcsetattr(STDIN_FILENO, TCSANOW, &temp_tio);
}

void restore_input_buffering() {
	tcsetattr(STDIN_FILENO, TCSANOW, &original_tio);
}

uint16_t check_key() {
	fd_set readfds;
	FD_ZERO(&readfds);
	FD_SET(STDIN_FILENO, &readfds);

	struct timeval timeout;
	timeout.tv_sec = 0;
	timeout.tv_usec = 0;
	return select(1, &readfds, NULL, NULL, &timeout) != 0;
}

#endif

uint16_t extend(uint16_t x, int bit_k) {
	if ((x >> (bit_k - 1)) & 1) {
		x |= (0xFFFF << bit_k);
	}
	return x;
}

void update_flags(uint16_t flag) {
	if (reg[flag] == 0) {
		reg[R_COND] = FL_ZRO;
	} else if (reg[flag] >> 15) {
		reg[R_COND] = FL_NEG;
	} else {
		reg[R_COND] = FL_POS;
	}
}

uint16_t swap_16(uint16_t x) {
	return (x << 8) | (x >> 8);
}

void read_image_file(FILE* file) {
	uint16_t origin;
	fread(&origin, sizeof(origin), 1, file);
	origin = swap_16(origin);

	uint16_t max_read_size = MEMORY_MAX - origin;
	uint16_t* place = mem + origin;
	size_t read = fread(place, sizeof(uint16_t), max_read_size, file);

	while (read-- > 0) {
		*place = swap_16(*place);
		place++;
	}
}

int read_image(const char* path) {
	FILE* file = fopen(path, "rb");
	if (!file) {
		return 0;
	}
	read_image_file(file);
	fclose(file);
	return 1;
}

void mem_write(uint16_t addr, uint16_t val) {
	mem[addr] = val;
}

uint16_t mem_read(uint16_t addr) {
	if (addr == MR_KBSR) {
		if (check_key()) {
			mem[MR_KBSR] = (1 << 15);
			mem[MR_KBDR] = getchar();
		} else {
			mem[MR_KBSR] = 0;
		}
	}
	return mem[addr];
}

void handle_interrupt(int signal) {
	restore_input_buffering();
	printf("\n");
	exit(-2);
}

int main(int argc, const char* argv[]) {
	if (argc < 2) {
		printf("vmra [image_file1] ...\n"); exit(2);
	}

	for (int i = 1; i < argc; i++) {
		if (!read_image(argv[i])) {
			printf("failed to load image: %s\n", argv[i]); exit(1);
		}
	}

	signal(SIGINT, handle_interrupt);
	disable_input_buffering();

	reg[R_COND] = FL_ZRO;

	enum { PC_START = 0x3000 };
	reg[R_PC] = PC_START;

	int running = 1;
	while (running) {
		uint16_t instr = mem_read(reg[R_PC]++);
		uint16_t opc = instr >> 12;

		switch (opc) {
			case OP_BR: {
				uint16_t pc_offset = extend(instr & 0x1FF, 9);
				uint16_t cond_flag = (instr >> 9) & 0x7;

				if (cond_flag & reg[R_COND]) {
					reg[R_PC] += pc_offset;
				}

				break;
			}
			case OP_ADD: {
				// dest reg (DR)
				uint16_t r0 = (instr >> 9) & 0x7;
				// first oper (SR1)
				uint16_t r1 = (instr >> 6) & 0x7;
				// am i in immediate mode? yes 
				uint16_t imm_flag = (instr >> 5) & 0x1;

				if (imm_flag) {
					uint16_t imm5 = extend(instr & 0x1F, 5);
					reg[r0] = reg[r1] + imm5;
				} else {
					uint16_t r2 = instr & 0x7;
					reg[r0] = reg[r1] + reg[r2];
				}

				update_flags(r0);
				break;
			}
			case OP_LD: {
				uint16_t r0 = (instr >> 9) & 0x7;
				uint16_t pc_offset = extend(instr & 0x1FF, 9);
				reg[r0] = mem_read(reg[R_PC] + pc_offset);
				update_flags(r0);
				break;
			}
			case OP_ST: {
				uint16_t pc_offset = extend(instr & 0x1FF, 9);
				uint16_t r0 = (instr >> 9) & 0x7;
				mem_write(reg[R_PC] + pc_offset, reg[r0]); 
				break;
			}
			case OP_JSR: {
				uint16_t long_flag = (instr >> 11) & 1;
				reg[R_R7] = reg[R_PC];

				if (long_flag) {
					uint16_t long_pc_offset = extend(instr & 0x7FF, 11);
					reg[R_PC] += long_pc_offset;
				} else {
					uint16_t r1 = (instr >> 6) & 0x7;
					reg[R_PC] = reg[r1];
				}

				break;
			}
			case OP_AND: {
				uint16_t r0 = (instr >> 9) & 0x7;
				uint16_t r1 = (instr >> 6) & 0x7;
				uint16_t imm_flag = (instr >> 5) & 0x1;

				if (imm_flag) {
					uint16_t imm5 = extend(instr & 0x1F, 5);
					reg[r0] = reg[r1] & imm5;
				} else {
					uint16_t r2 = instr & 0x7;
					reg[r0] = reg[r1] & reg[r2];
				}

				update_flags(r0);
				break;
			}
			case OP_LDR: {
				uint16_t r0 = (instr >> 9) & 0x7;
				uint16_t r1 = (instr >> 6) & 0x7;
				uint16_t pc_offset = extend(instr & 0x3F, 6);
				reg[r0] = mem_read(reg[r1] + pc_offset);
				update_flags(r0);
				break;
			}
			case OP_STR: {
				uint16_t pc_offset = extend(instr & 0x3F, 6);
				uint16_t r0 = (instr >> 9) & 0x7;
				uint16_t r1 = (instr >> 6) & 0x7;
				mem_write(reg[r1] + pc_offset, reg[r0]);
				break;
			}
			case OP_RTI: {
				abort();
			}
			case OP_NOT: {
				// dest reg (DR)
				uint16_t r0 = (instr >> 9) & 0x7;
				// oper (SR)
				uint16_t r1 = (instr >> 6) & 0x7;
				reg[r0] = ~reg[r1];
				update_flags(r0);
				break;
			}
			case OP_LDI: {
				// dest reg (DR)
				uint16_t r0 = (instr >> 9) & 0x7;
				// PCoffset 9
				uint16_t pc_offset = extend(instr & 0x1FF, 9);
				// add pc_offset to curr PC
				reg[r0] = mem_read(mem_read(reg[R_PC] + pc_offset));
				update_flags(r0);
				break;
			}
			case OP_STI: {
				uint16_t pc_offset = extend(instr & 0x1FF, 9);
				uint16_t r0 = (instr >> 9) & 0x7;
				mem_write(mem_read(reg[R_PC] + pc_offset), reg[r0]);
				break;
			}
			case OP_JMP: {
				uint16_t r1 = (instr >> 6) & 0x7;
				reg[R_PC] = reg[r1];
				break;
			}
			case OP_RES: {
				abort();
			}
			case OP_LEA: {
				uint16_t r0 = (instr >> 9) & 0x7;
				uint16_t pc_offset = extend(instr & 0x1FF, 9);
				reg[r0] = reg[R_PC] + pc_offset;
				update_flags(r0);
				break;
			}
			case OP_TRAP: {
				reg[R_R7] = reg[R_PC];

				switch (instr & 0xFF) {
					case TRAP_GETC: {
						reg[R_R0] = (uint16_t) getchar();
						update_flags(R_R0);
						break;
					}
					case TRAP_OUT: {
						putc((char) reg[R_R0], stdout);
						fflush(stdout);
						break;
					}
					case TRAP_PUTS: {
						uint16_t* c = mem + reg[R_R0];
						
						while (*c) {
							putc((char) *c, stdout); c++;
						}
						
						fflush(stdout);
						break;
					}
					case TRAP_IN: {
						printf("enter a char: ");
						char c = getchar();
						putc(c, stdout);
						fflush(stdout);
						reg[R_R0] = (uint16_t) c;
						update_flags(R_R0);
						break;
					}
					case TRAP_PUTSP: {
						uint16_t* c = mem + reg[R_R0];

						while (*c) {
							char c0 = (*c) & 0xFF;
							putc(c0, stdout);
							char c1 = (*c) >> 8;
							if (c1) putc(c1, stdout);
							c++;
						}

						fflush(stdout);
						break;
					}
					case TRAP_HALT: {
						puts("HALT AND CATCH FIRE");
						fflush(stdout);
						running = 0;
						break;
					}
				}

				break;
			}
			default: {
				abort();
				break;
			}
		}
	}

	restore_input_buffering();
	return 0;
}