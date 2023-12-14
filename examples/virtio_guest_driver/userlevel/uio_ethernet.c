/*
 * TODO: license
 */

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
typedef struct sddf_handler {
    size_t channel; // @jade: how many?

} sddf_handler_t;

/********************************* libmicrokit *********************************/
// init: map tx, rx rings
// bool init()

// enable irq

// ack irq

/********************************* net backend *********************************/

int main(void) {
    return 0;
}