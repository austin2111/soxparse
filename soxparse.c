#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>
#include <sox.h>

// If C89 is needed for any reason, the declarations are going to have to be moved
// to the beginning of every function. So... maybe don't compile this with C89.

sox_format_t *soxinput, *soxoutput; // At least one of these is going to have to be global. Remove one if possible.
char * tokenout;
double currentend = 0, currentstart = 0, lastend = 0, laststart = 0;
char starttime[15] = {0}, endtime[15] = {0};
unsigned long long timeoffset;
#define LINELEN 32 // For our puposes, we shouldn't be going over 32 characters.
void lsx_rewind(sox_format_t * ft);

/*
void format_read(struct sox_format_t * input) {
    printf("\nfiletype is %s\noob pointer is %d\nseekable is %d\nmode is %c\nolength is %llu\nclips is %llu\nsox_errno is %d\nfp is %d\nio_type is %d\ntell_off is %llu\ndata_start is %llu\nhandler is %d\npriv pointer is %d\n", input->filetype, input->oob, input->seekable, input->mode, input->olength, input->clips, input->sox_errno, input->fp, input->io_type, input->tell_off, input->data_start, input->handler, input->priv);
    if (input->sox_errstr != NULL) printf("sox_errstr is %s\n", input->sox_errstr);
    return;
}
// This was here for debugging. Enable if needed.
*/

char trim_out(char * infile, char * outprefix) {
    char filename[40];
    // Old code for reference
    //snprintf(filename, 40, "%s-%s.wav", outprefix, starttime);
    // Convert double to microseconds
    snprintf(filename, 40, "%s-%llu.wav", outprefix, (unsigned long long) ((strtod(starttime, NULL) * 1e6)+timeoffset) );
    // TO DO: Verify return values
    assert( soxoutput = sox_open_write(filename, &soxinput->signal, &soxinput->encoding, NULL, NULL, NULL) ); // TO DO: Input arguments
    lsx_rewind(soxinput);
    sox_effects_chain_t * trimchain;
    trimchain = sox_create_effects_chain(&soxinput->encoding, &soxoutput->encoding);
    sox_effect_t * effect;
    effect = sox_create_effect(sox_find_effect("input"));
    char * arguments[2];
    arguments[0] = (char *) soxinput;
    if (sox_effect_options(effect, 1, arguments) != SOX_SUCCESS) {
        fprintf(stderr, "ERROR: sox_effect_options() returned failure condition for input!\n");
        return 1;
    }

    sox_signalinfo_t interim_signal = soxinput->signal;

    if (sox_add_effect(trimchain, effect, &interim_signal, &soxinput->signal) != SOX_SUCCESS) {
        fprintf(stderr, "ERROR: sox_add_effect() returned failure condition!\n");
        return 1;
    }
    effect = sox_create_effect(sox_find_effect("trim"));
    printf("DEBUG: starttime is %s, endtime is %s\n", starttime, endtime);
    arguments[0] = starttime;
    arguments[1] = endtime;
    if (sox_effect_options(effect, 2, arguments) != SOX_SUCCESS) {
        fprintf(stderr, "ERROR: sox_effect_options() returned failure condition for trim!\n");
        return 1;
    }
    if (sox_add_effect(trimchain, effect, &interim_signal, &soxinput->signal) != SOX_SUCCESS) {
        fprintf(stderr, "ERROR: sox_add_effect() returned failure condition!\n");
        return 1;
    }
    effect = sox_create_effect(sox_find_effect("output"));
    arguments[0] = (char *) soxoutput;
    if (sox_effect_options(effect, 1, arguments) != SOX_SUCCESS) {
        fprintf(stderr, "ERROR: sox_effect_options() returned failure condition for output!\n");
        return 1;
    }
    if (sox_add_effect(trimchain, effect, &interim_signal, &soxoutput->signal) != SOX_SUCCESS) {
        fprintf(stderr, "ERROR: sox_create_effect() returned failure condition for output!\n");
        return 1;
    }
    free(effect);
    sox_flow_effects(trimchain, NULL, NULL);
    sox_delete_effects_chain(trimchain);
    //sox_close(soxinput);
    sox_close(soxoutput);

    return SOX_SUCCESS;
}

char find_start(void * indexline, FILE * indexfile) {

    if (fgets(indexline, LINELEN, indexfile) == NULL) {
        fprintf(stderr, "ERROR: Unexpected end of file!\n");
        return 1;
    }
    tokenout = strtok(indexline, ":");
    if (tokenout != NULL) tokenout = strtok(NULL, ":");
    else {
        fprintf(stderr, "ERROR: Unexpected end of line!\n");
        return 1;
    }
    if (tokenout != NULL) {
        currentstart = strtod(tokenout+1, NULL);
    }
    return 0;

}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        printf("Usage: %s <input wav file> <output file prefix> <index file> <start time in microseconds>\n", argv[0]);
        return 1;
    }

    if (sox_init() != SOX_SUCCESS) {
        fprintf(stderr, "ERROR: sox initialization failed! Exiting...\n");
        return 1;
    }

    assert(soxinput = sox_open_read(argv[1], NULL, NULL, NULL));
    if (soxinput == NULL) {
        fprintf(stderr, "ERROR: Cannot open source file %s\n", argv[0]);
        return 0;
    }
    unsigned int loopcount = 0;
    char * endptr;

    // NOTE: C11 is required for strtoull!
    timeoffset = strtoull(argv[4], &endptr, 10);
    if (endptr == NULL) {
        fprintf(stderr, "ERROR: Couldn't convert timestamp argument to unsigned long long!\n");
        return 1;
    }

    FILE * indexfile = fopen(argv[3], "r");
    if (indexfile == NULL) {
        fprintf(stderr, "ERROR: Couldn't open %s! Exiting...\n", argv[3]);
        sox_close(soxinput);
        return 1;
    }
    char indexline[LINELEN] = {0};
    bool earlytrim = false;
    while (fgets(indexline, LINELEN, indexfile) != NULL) {
        loopcount++;
        tokenout = strtok(indexline, ":");
        if (tokenout != NULL) tokenout = strtok(NULL, ":");
        if (tokenout != NULL) {
            if (strlen(tokenout) > 1) {
                if (!earlytrim)
                lastend = currentend;
                currentend = strtod(tokenout+1, NULL);
                if (currentend > 1) currentend -= 0.8; // Backtrack 800 milliseconds <-- change back to 900 if problems

                printf("DEBUG: currentend - lastend = %f\n", currentend - lastend);
                if ((currentend - lastend < 2) && (currentend != lastend))
                {
                    currentend = lastend;
                    laststart = currentstart;
                    // Adjust end time without moving back start time
                    find_start(indexline, indexfile);
                    printf("DEBUG: Upper currentstart - currentend = %f\n", currentstart - currentend);
                    earlytrim = true;
                    continue;
                }
                if (earlytrim) {
                    laststart = currentstart;
                    printf("DEBUG: Executing earlytrim, lastend = %f, laststart = %f, currentend = %f, currentstart = %f\n", lastend, laststart, currentend, currentstart);

                    if (laststart - lastend < 2.5) laststart = 2.5; // Clip the duration to a 2.5 second window if less
                    else {
                        laststart -= lastend;
                    }

                    snprintf(starttime, 15, "%f", lastend);
                    snprintf(endtime, 15, "%f", laststart);
                    if (trim_out(argv[1], argv[2]) == 1) {
                        sox_close(soxinput);
                        fclose(indexfile);
                        return 1;
                    }
                    printf("DEBUG: ftell for file pointer returns %ld\n", ftell(soxinput->fp));
                    // TO DO: libsox is frequently failing unless we close/open the input file! Why?

                    sox_close(soxinput);
                    assert(soxinput = sox_open_read(argv[1], NULL, NULL, NULL));
                    if (soxinput == NULL) {
                        fprintf(stderr, "ERROR: Cannot open source file %s\n", argv[0]);
                        return 1;
                    }

                    //lsx_rewind(soxinput); // Not necessary since we're re-opening the file
                    earlytrim = false;

                }
                find_start(indexline, indexfile);
                printf("DEBUG: currentstart - currentend = %f\n", currentstart - currentend);
                if (currentstart - currentend < 2.5) currentstart = 2.5; // Clip the duration to a two second window if it's not long enough.
                else currentstart -= currentend;
                snprintf(starttime, 15, "%f", currentend);
                snprintf(endtime, 15, "%f", currentstart);
                if (trim_out(argv[1], argv[2]) == 1) {
                    sox_close(soxinput);
                    fclose(indexfile);
                    return 1;
                }

                printf("DEBUG: loopcount is %d\n", loopcount);

                // TO DO: libsox is frequently failing unless we close/open the input file! Why?

                sox_close(soxinput);
                assert(soxinput = sox_open_read(argv[1], NULL, NULL, NULL));
                if (soxinput == NULL) {
                    fprintf(stderr, "ERROR: Cannot open source file %s\n", argv[0]);
                    return 1;
                }
                //lsx_rewind(soxinput); // Not necessary since we're re-opening the file
            }
            else printf("ERROR: strtok output was too short!\n");
        }
        else {
            fprintf(stderr, "ERROR: strtok output was null!\n");
            sox_close(soxinput);
            fclose(indexfile);
            return 1;
        }
    }

    sox_close(soxinput);
    fclose(indexfile);
    return 0;
}

