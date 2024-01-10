/*
 * TODO: license
 */

#include <stdio.h>
#include <stdint.h>
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
#include <sddf/network/shared_ringbuffer.h>

// @jade: should this be part of the shared mem region and gets initialize by the other end? 
typedef struct net_sddf_handler {
    size_t init_ch; // init/get MAC protocol
    size_t tx_ch;
    size_t rx_ch;
    ring_handle_t rx_ring;
    ring_handle_t tx_ring;
} sddf_handler_t;

/********************************* libmicrokit *********************************/
// init: map tx, rx rings
// void sddf_net_init(void)
// {
//         /* initialize the client interface */
// }

// void notified(microkit_channel ch);
// microkit_msginfo protected(microkit_channel ch, microkit_msginfo msginfo);
// void fault(microkit_channel ch, microkit_msginfo msginfo);

// static inline void
// microkit_notify(microkit_channel ch);

// static inline void
// microkit_fault_reply(microkit_msginfo msginfo);

// static inline microkit_msginfo
// microkit_ppcall(microkit_channel ch, microkit_msginfo msginfo);

// static inline microkit_msginfo
// microkit_msginfo_new(uint64_t label, uint16_t count)
// {
//     return seL4_MessageInfo_new(label, 0, 0, count);
// }

// static inline uint64_t
// microkit_msginfo_get_label(microkit_msginfo msginfo)
// {
//     return seL4_MessageInfo_get_label(msginfo);
// }

// static void
// microkit_mr_set(uint8_t mr, uint64_t value)
// {
//     seL4_SetMR(mr, value);
// }

// static uint64_t
// microkit_mr_get(uint8_t mr)
// {
//     return seL4_GetMR(mr);
// }

/********************************* net backend *********************************/

int main(int argc, char *argv[]) {
    if (argc != 4) {
        printf("Usage: %s <filename> <size> <map_index>\n\n"
               "TODO\n",
               argv[0]);
        return 1;
    }

    char *dataport_name = argv[1];
    int length = atoi(argv[2]);
    assert(length > 0);

    int region = atoi(argv[3]);

    printf("dataport name: %s, size: %d, region: %d\n", dataport_name, length, region);

    int fd = open(dataport_name, O_RDWR);
    assert(fd >= 0);

    char *dataport;
    if ((dataport = mmap(NULL, length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, region * getpagesize())) == (void *) -1) {
        printf("mmap failed, errno: %d\n", errno);
        close(fd);
	    return 1;
    }
    return 0;
}