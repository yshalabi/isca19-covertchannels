#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>


/*
 * Execution state of the program, with the variables
 * that we need to pass around the various functions.
 */
struct state {
    char *buffer;
    struct Node *eviction_set;
    int interval;
    bool debug;
    bool benchmark_mode;
};

/*
 * Parses the arguments and flags of the program and initializes the struct state
 * with those parameters (or the default ones if no custom flags are given).
 */
int init_state(struct state *state, int argc, char **argv)
{

	if (argc<2) {
		return -1;
	}

	state->buffer = NULL;

	// Set some default state values.
	state->eviction_set = NULL;
	state->debug = false;
	state->benchmark_mode = false;

	// This number may need to be tuned up to the specific machine in use
	// NOTE: Make sure that interval is the same in both sender and receiver
	state->interval = 160;

	int size = 4096;
	int offset = 0;
	if (argc>2) {
		offset = atoi(argv[2])*64; // inc. in multiples of 64 byte cache line
	}

	int inFile = open(argv[1], O_RDONLY);
	if(inFile == -1) {
		printf("Failed to Open File\n");
		return -1;
	}

	void *mapaddr = mmap(NULL,size,PROT_READ,MAP_SHARED,inFile,0);
	printf("File mapped at %p\n",mapaddr);

	if (mapaddr == (void*) -1 ) {
		printf("Failed to Map Address\n");
		return -1;
	}

	ADDR_PTR addr = (ADDR_PTR) mapaddr + offset;
	append_string_to_linked_list(&state->eviction_set, addr);
	printf("Address Flushing = %lx\n",addr);

	return 0;
}

/*
 * Sends a bit to the receiver by repeatedly flushing the addresses of the eviction_set
 * for the clock length of state->interval when we are sending a one, or by doing nothing
 * for the clock length of state->interval when we are sending a zero.
 */
void send_bit(bool one, struct state *state)
{
    clock_t start_t, curr_t;

    start_t = clock();
    curr_t = start_t;

    if (one) {
        while ((curr_t - start_t) < state->interval) {
            struct Node *current = state->eviction_set;
            while (current != NULL && (curr_t - start_t) < state->interval) {
                ADDR_PTR addr = current->addr;
                clflush(addr);

                curr_t = clock();
                current = current->next;
            }
        }

    } else {
        start_t = clock();
        while (clock() - start_t < state->interval) {}
    }
}

int main(int argc, char **argv)
{
    // Initialize state and local variables
    struct state state;
    init_state(&state, argc, argv);
    clock_t start_t, end_t;
    int sending = 1;

    printf("Please type a message (exit to stop).\n");
    while (sending) {

        // Get a message to send from the user
        printf("< ");
        char text_buf[128];
        fgets(text_buf, sizeof(text_buf), stdin);

        if (strcmp(text_buf, "exit\n") == 0) {
            sending = 0;
        }

        // Convert that message to binary
        char *msg = string_to_binary(text_buf);
        if (state.debug) {
            printf("%s\n", msg);
        }

        // If we are in benchmark mode, start measuring the time
        if (state.benchmark_mode) {
            start_t = clock();
        }

        // Send a '10101011' byte to let the receiver detect that
        // I am about to send a start string and sync
        for (int i = 0; i < 6; i++) {
            send_bit(i % 2 == 0, &state);
        }
        send_bit(true, &state);
        send_bit(true, &state);

        // Send the message bit by bit
        size_t msg_len = strlen(msg);
        for (int ind = 0; ind < msg_len; ind++) {
            if (msg[ind] == '0') {
                send_bit(false, &state);
            } else {
                send_bit(true, &state);
            }
        }

        // If we are in benchmark mode, finish measuring the
        // time and print the bit rate.
        if (state.benchmark_mode) {
            end_t = clock();
            printf("Bitrate: %.2f Bytes/second\n",
                   ((double) strlen(text_buf)) / ((double) (end_t - start_t) / CLOCKS_PER_SEC));
        }
    }

    printf("Sender finished\n");
    return 0;
}












//Appendix:
void depr_init_state(struct state *state, int argc, char **argv)
{
	uint64_t wantSet = 0;
	if (argc>1) {
		wantSet = atoi(argv[1]);
	}

    // The following calculations are based on the paper:
    //      C5: Cross-Cores Cache Covert Channel (dimva 2015)
    int n = CACHE_WAYS_L3;
    int o = 6;                          // log_2(64), where 64 is the line size
    int s = 15;                         // log_2(8192), where 8192 is the number of cache sets
    int two_o = ipow(2, o);             // 64
    int two_o_s = ipow(2, s) * two_o;   // 524,288
    int b = n * two_o_s;                // size in bytes of the LLC = 8,388,608

    // Allocate a buffer of the size of the LLC
    state->buffer = malloc((size_t) b);

    // Set some default state values.
    state->eviction_set = NULL;
    state->debug = false;
    state->benchmark_mode = false;

    // This number may need to be tuned up to the specific machine in use
    // NOTE: Make sure that interval is the same in both sender and receiver
    state->interval = 160;




    // Construct the eviction_set by taking the addresses that have cache set index 0
    // There should be 128 such addresses in our buffer: one per line per cache set 0 of each slice (8 * 16).
    for (int set_index = 0; set_index < CACHE_SETS_L3; set_index++) {
        for (int line_index = 0; line_index < CACHE_WAYS_L3; line_index++) {

            ADDR_PTR addr = (ADDR_PTR) (state->buffer + set_index * two_o + line_index * two_o_s);
            if (get_cache_set_index(addr) == wantSet) {
	printf("Address cache set index = %lx\n",addr);
                append_string_to_linked_list(&state->eviction_set, addr);
            }
        }
    }

    // Parse the command line flags
    //      -d is used to enable the debug prints
    //      -b is used to enable the benchmark mode (to measure the sending bitrate)
    //      -i is used to specify a custom value for the time interval
    int option;
    while ((option = getopt(argc, argv, "di:w:b")) != -1) {
        switch (option) {
            case 'd':
                state->debug = true;
                break;
            case 'b':
                state->benchmark_mode = true;
                break;
            case 'i':
                state->interval = atoi(optarg);
                break;
            case '?':
                fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
                exit(1);
            default:
                exit(1);
        }
    }
}


