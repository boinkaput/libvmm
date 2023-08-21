#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <net/if.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include "shared_ringbuffer.h"


/*
 * It'd be nice to use PACKET_TX_RING and PACKET_RX_RING to get
 * direct DMA mapping, but I see no way to coerce these to the
 * sDDF memory regions, so we're stuck with copying for now.
 */
#define NMAPPINGS 3
/*
 * There are two shared memory regions for buffers,
 * and one for rings.
 */
struct sddf_comms {
    struct ring_buffer *tx_inuse;
    struct ring_buffer *tx_empty;
    struct ring_buffer *rx_inuse;
    struct ring_buffer *rx_empty;
    unsigned char *ring_physbase;
    unsigned char *ring_base;
    unsigned char *tx_buf_physbase;
    unsigned char *tx_buf_base;
    unsigned char *rx_buf_physbase;
    unsigned char *rx_buf_base;
};

struct buf_desc {
    unsigned char *payload;
    size_t len;
};


static int
getbuf(unsigned char *base, struct ring_buffer *rp, struct buf_desc *bp)
{
    if (dequeue(rp, &bp->payload, &bp->len, NULL))
        return 0;
    bp->payload -= physbase; 
    bp->payload += base;
    return 1;
}

// putbuf(struct buf_desc *bp, unsigned char *base, ...)

/*
 * Set up a raw socket in promiscuous mode
 * All incoming packets will be sent to UIO for processing outside.
 */
int
setup_socket(const char *devicename, int *ifindex)
{
    struct ifreq ifr;
    int s;
    s = socket(AF_PACKET, SOCK_RAW|SOCK_NONBLOCK, htons(ETH_P_ALL));

    strncpy(ifr.ifr_name, devicename, sizeof ifr.ifr_name);
    if (-1 == ioctl(s, SIOCGIFFLAGS, &ifr)) {
        perror("Error: could not get device flags");
        close(s);
        return -1;
    }

    ifr.ifr_flags |= IFF_PROMISC;
    if (-1 == ioctl(s, SIOCSIFFLAGS, &ifr)) {
        perror("Error: could not set device flags");
        close(s);
        return -1;
    }
    if (ioctl(s, SIOCGIFINDEX, &ifr))
    {
        perror("Error: Could not get device index");
    }

    *ifindex = ifr.ifr_ifindex;
    return s;
}

/*
 * Read an ASCII number from a file.
 * Returns the number or -1 on error.
 */
long int_from_file(char *f)
{
    FILE *fin;
    long i;

    if (!(fin = fopen(f, "r"))){
        perror(f);
        return -1;
    }
    if (fscanf(fin, "%ld", &i) != 1) {
        fprintf(stderr, "%s: %s\n", f, strerror(errno));
        fclose(fin);
        return -1;
    }
    fclose(fin);
    return i;
}

/*
 * Set up all the shared regions for the sDDF
 * Expect three regions, one each for Tx and Rx buffers,
 * with the base address in the 'addr' file in sysfs.
 * and one containing the ring buffers.
 */
int
setup_uio(int devicenum, struct sddf_comms *sdfp) {
    char sysname[64];
    int uiofd;
    int strend; /* index of end of sysfs uio maps pathname */
    unsigned char *mappings[NMAPPINGS];
    int i;
    uintptr_t addr[NMAPPINGS];
    size_t sz[NMAPPINGS];

    snprintf(sysname, sizeof sysname, "/dev/uio%d", devicenum);
    uiofd = open(sysname, O_RDWR);

    strend = snprintf(sysname, sizeof sysname,
                      "/sys/class/uio/uio%d/maps/map0/", devicenum);
    for (i = 0; i < NMAPPINGS; i++) {
        sysname[strend-2] = '0' + i;
        strncpy(sysname + strend, sizeof sysname - strend, "addr");
        addr[i] = int_from_file(sysname);
        strncpy(sysname + strend, sizeof sysname - strend, "size");
        sz[i] = int_from_file(sysname);

        fprintf(stderr, "Mapping %d; addr %lu sz %lu\n",
                i, addr[i], sz[i]);

        /* If we can map 1:1 it'll be easier. */
        mappings[i] = mmap(addr, sz, PROT_READ|PROT_WRITE, MAP_SHARED,
                           uiofd, i * getpagesize());
        if (mappings[i] == MAP_FAILED) {
            fprintf(stderr, "mapping of region %d failed: %s\n",
                    i, strerror(errno));
            close(uiofd);
            return -1;
        }
    }

    /*
     * Mapping 0: The Rings
     */
    sdfp->ring_base = mappings[0];
    sdfp->ring_physbase = addr[0];

    /*
     * Mapping 1: Rx buffers
     */
    sdfp->rx_buf_base = mappings[1];
    sdfp->rx_buf_physbase = addr[1];
    /*
     * Mapping 2: Tx buffers
     */
    sdfp->tx_buf_base = mappings[2];
    sdfp->tx_buf_physbase = addr[2];

    setup_rings(sdfp, addr, sz);
    return uiofd;
}


void
mainloop(int uiofd, int sock, int ifindex, struct sddf_comms *sdfp)
{
    struct pollfd[2];
#define UIOFD 0
#define SOCKFD 1
    struct sockaddr_ll ll;
    struct buf_desc txbuf;
    struct buf_desc rxbuf;
    pollfd[UIOFD].fd = uiofd;
    pollfd[UIOFD].events = POLLIN;

    pollfd[SOCKFD].fd = sock;
    pollfd[SOCKFD].events = POLLIN|POLLOUT;

    memset(&ll, 0, sizeof ll);
    ll.sll_family = AF_PACKET;
    ll.sll_addr = 0;
    ll.sll_ifindex = ifindex;
    ll.sll_protocol =  0;

    for (;;) {
        poll(pollfd, 2, -1);
        if (pollfd[SOCKFD].revents & POLLOUT) {
            /* Handle Transmits first; free up buffers for more transmits */
            while (getbuf(sdfp->tx_buf_base, sdfp->tx_phys_base, sdfp->tx_inuse, &txbuf)) {
                struct ether_header *ehp = txbuf->payload;
                memcpy(ll.sll_addr, ehp->ether_dhost, ETH_ALEN);
                /* Need to deal with EWOULDBLOCK here */
                sendto(sockfd, txbuf->payload, txbuf->len, 0, &ll, sizeof ll);
                putbuf(&txbuf, sdfp->txbuf_base, sdfp->txbuf_phys_base,
                       sdfp->tx_free);
            }
        }
        pollfd[SOCKFD].evince &= ~ POLLOUT;
        if (pollfd[SOCKFD].revents & POLLIN) {
            if (getbuf(sdfp->rx_buf_base, sdfp->rx_free, &rxbuf)) {
                recv(sockfd, rxbuf->payload, rxbuf->size, 0);
                putbuf(&rxbuf, sdfp->rx_used);
            }
        }
    }
}
