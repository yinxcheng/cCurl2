/*
    2016 BTCDDev, based on Code from Come-from-Beyond
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/wait.h>

#define Invalid_transaction_trits_length 0x63
#define Invalid_min_weight_magnitude 0x64
#define InterruptedException 0x65

static const int HASH_LENGTH = 243;
static const int STATE_LENGTH = (243*3);
static const int TRANSACTION_LENGTH = 8019;
typedef enum { false, true } bool;
//bool finished, interrupted;
volatile bool finished, interrupted, nonceFound;

pthread_mutex_t new_thread_search;
pthread_mutex_t new_thread_interrupt;


void interrupt() {

	pthread_mutex_lock(&new_thread_interrupt);
	finished = true;
	interrupted = true;

	//notifyAll();
	pthread_mutex_unlock(&new_thread_interrupt);
}

static
inline void transform( long *const stateLow, long *const stateHigh, long *const scratchpadLow, long *const scratchpadHigh) {

	int scratchpadIndex = 0;
	int round;
	long alpha, beta, gamma, delta;
	int stateIndex;

	for (round = 27; round-- > 0; ) {
	//for (int round = 27; round-- > 0; ) {
		//memcpy(stateLow, scratchpadLow, STATE_LENGTH);
		//memcpy(stateHigh, scratchpadHigh, STATE_LENGTH);
		memcpy( scratchpadLow, stateLow, STATE_LENGTH);
		memcpy( scratchpadHigh, stateHigh, STATE_LENGTH);

		//for (int stateIndex = 0; stateIndex < STATE_LENGTH; stateIndex++) {
		for (stateIndex = 0; stateIndex < STATE_LENGTH; stateIndex++) {

			alpha = scratchpadLow[scratchpadIndex];
			beta = scratchpadHigh[scratchpadIndex];
			gamma = scratchpadHigh[scratchpadIndex += (scratchpadIndex < 365 ? 364 : -365)];
			delta = (alpha | (~gamma)) & (scratchpadLow[scratchpadIndex] ^ beta);

			stateLow[stateIndex] = ~delta;
			stateHigh[stateIndex] = (alpha ^ gamma) | delta;
		}
	}

}


///*
static void increment(long *const midStateCopyLow, long *const midStateCopyHigh, const int fromIndex, const int toIndex) {

	int i;
        //for (int i = fromIndex; i < toIndex; i++) {
	for (i = fromIndex; i < toIndex; i++) {

		if (midStateCopyLow[i] == 0b0000000000000000000000000000000000000000000000000000000000000000L) {

			midStateCopyLow[i] = 0b1111111111111111111111111111111111111111111111111111111111111111L;
			midStateCopyHigh[i] = 0b0000000000000000000000000000000000000000000000000000000000000000L;

		} else {

			if (midStateCopyHigh[i] == 0b0000000000000000000000000000000000000000000000000000000000000000L) {

				midStateCopyHigh[i] = 0b1111111111111111111111111111111111111111111111111111111111111111L;

			} else {

				midStateCopyLow[i] = 0b0000000000000000000000000000000000000000000000000000000000000000L;
			}

			break;
		}
	}
}

bool search(int *const transactionTrits, int length, const int minWeightMagnitude, int numberOfThreads) {

	long midStateLow[STATE_LENGTH];
	long midStateHigh[STATE_LENGTH];
	int i, j;
	int offset = 0;
	long scratchpadLow[STATE_LENGTH];
	long scratchpadHigh[STATE_LENGTH];
	int threadIndex;
	long midStateCopyLow[STATE_LENGTH];
	long midStateCopyHigh[STATE_LENGTH];
	long stateLow[STATE_LENGTH];
	long stateHigh[STATE_LENGTH];
	int bitIndex;

	if (length != TRANSACTION_LENGTH) {

		return Invalid_transaction_trits_length;
	}
	if (minWeightMagnitude < 0 || minWeightMagnitude > HASH_LENGTH) {

		return Invalid_min_weight_magnitude;
	}

	finished = false;
	interrupted = false;
	nonceFound = false;

	{
		for (i = HASH_LENGTH; i < STATE_LENGTH; i++) {

			midStateLow[i] = 0b1111111111111111111111111111111111111111111111111111111111111111L;
			midStateHigh[i] = 0b1111111111111111111111111111111111111111111111111111111111111111L;
		}


		for (i = (TRANSACTION_LENGTH - HASH_LENGTH) / HASH_LENGTH; i-- > 0; ) {

			for (j = 0; j < HASH_LENGTH; j++) {

				switch (transactionTrits[offset++]) {

					case 0: {

						midStateLow[j] = 0b1111111111111111111111111111111111111111111111111111111111111111L;
						midStateHigh[j] = 0b1111111111111111111111111111111111111111111111111111111111111111L;

					} break;

					case 1: {

						midStateLow[j] = 0b0000000000000000000000000000000000000000000000000000000000000000L;
						midStateHigh[j] = 0b1111111111111111111111111111111111111111111111111111111111111111L;

					} break;

					default: {

						midStateLow[j] = 0b1111111111111111111111111111111111111111111111111111111111111111L;
						midStateHigh[j] = 0b0000000000000000000000000000000000000000000000000000000000000000L;
					}
				}
			}

			transform(midStateLow, midStateHigh, scratchpadLow, scratchpadHigh);
		}

		midStateLow[0] = 0b1101101101101101101101101101101101101101101101101101101101101101L;
		midStateHigh[0] = 0b1011011011011011011011011011011011011011011011011011011011011011L;
		midStateLow[1] = 0b1111000111111000111111000111111000111111000111111000111111000111L;
		midStateHigh[1] = 0b1000111111000111111000111111000111111000111111000111111000111111L;
		midStateLow[2] = 0b0111111111111111111000000000111111111111111111000000000111111111L;
		midStateHigh[2] = 0b1111111111000000000111111111111111111000000000111111111111111111L;
		midStateLow[3] = 0b1111111111000000000000000000000000000111111111111111111111111111L;
		midStateHigh[3] = 0b0000000000111111111111111111111111111111111111111111111111111111L;
	}

	if (numberOfThreads <= 0) {

		numberOfThreads = sysconf(_SC_NPROCESSORS_ONLN) - 1;
		if (numberOfThreads < 1) {

			numberOfThreads = 1;
		}
	}

	while (numberOfThreads-- > 0) {
		threadIndex = numberOfThreads;
		pthread_mutex_lock(&new_thread_search);
		memcpy(midStateCopyLow, midStateLow, STATE_LENGTH);
		memcpy(midStateCopyHigh, midStateHigh, STATE_LENGTH);
		for (i = threadIndex; i-- > 0; ) {

			increment(midStateCopyLow, midStateCopyHigh, HASH_LENGTH / 3, (HASH_LENGTH / 3) * 2);
		}

		while (!finished) {

			increment(midStateCopyLow, midStateCopyHigh, (HASH_LENGTH / 3) * 2, HASH_LENGTH);
			memcpy( stateLow, midStateCopyLow, STATE_LENGTH);
			memcpy( stateHigh, midStateCopyHigh, STATE_LENGTH);
			transform(stateLow, stateHigh, scratchpadLow, scratchpadHigh);

		NEXT_BIT_INDEX:
			for (bitIndex = 64; bitIndex-- > 0; ) {

				for (i = minWeightMagnitude; i-- > 0; ) {

					if ((((int)(stateLow[HASH_LENGTH - 1 - i] >> bitIndex)) & 1) != (((int)(stateHigh[HASH_LENGTH - 1 - i] >> bitIndex)) & 1)) {

						goto NEXT_BIT_INDEX;
					}
				}

				finished = true;
				for ( i = 0; i < HASH_LENGTH; i++) {

					transactionTrits[TRANSACTION_LENGTH - HASH_LENGTH + i] = ((((int)(midStateCopyLow[i] >> bitIndex)) & 1) == 0) ? 1 : (((((int)(midStateCopyHigh[i] >> bitIndex)) & 1) == 0) ? -1 : 0);
					nonceFound = true;
				}
				break;

			}
		}
		pthread_mutex_unlock(&new_thread_search);
	}

	if (wait(NULL))
		return InterruptedException;

	return interrupted;
}


//*/
//Tests
/*
    To run:
    javac CurlReference.java
    uncomment from here on and run
*/

//int run_test(int);
///*
int main()
{

    //printf("Running Test on [-1]: %s %d\n", run_test(-1) ==0 ? "PASS": "FAIL", run_test(-1));
    //printf("Running Test on [0]: %s %d\n", run_test (0)  ==0 ? "PASS": "FAIL", run_test(0));
    //printf("Running Test on [1]: %s %d\n", run_test (1)  ==0 ? "PASS": "FAIL", run_test(1));

	int transactionTrits[8] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};

	bool out;

	out = search(transactionTrits, 8, 8, 1);

    return 0;
}

//*/

/*
int run_test(int in)
{
    if (in !=0 && in!=-1 && in != 1){
        fprintf(stderr, "Invalid input\n");
        return -1;
    }
    int retVal = 1;
    struct Curl c;
    setup_Curl(&c);
    int input[243] ;
    int i, j;
    for(j=0; j<243; j++){
        input[j] = in;
    }
    int output[256];
    char final_output[1024];
    memset(final_output, 0 , 1024 * sizeof(char));
    for(j=0; j<256; j++){
        output[j] = 0;
    }
    absorb(&c, input, 0, 243);
    squeeze(&c, output, 0);

    char temp[16];
    strcpy(final_output, "[");
    for(i=0; i<c.HASH_SIZE-1; i++){
        sprintf(temp, "%d, ", output[i]);
        strcat(final_output, temp);
    }
    sprintf(temp, "%d]", output[c.HASH_SIZE-1]);
    strcat(final_output, temp);

    //Test
    FILE *fp;
    char ref[1024];
    memset(ref, 0, 1024);
    char sysCall[64];
    sprintf(sysCall, "java CurlReference %d", in);
    fp = popen(sysCall, "r");
    if(fp==NULL){
        fprintf(stderr, "Failed to run Reference Java Code\n");
        exit(1);
    }
    while(fgets(ref, sizeof(ref)-1, fp) != NULL)
        ;
    ref[strlen(ref)-1] = 0; //trailing \n from java
    pclose(fp);

    /// compare the result from c and that from from java
    retVal = strcmp(final_output, ref);

    return retVal;
}
*/


