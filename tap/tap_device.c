/*
 * A case for linux tap device
 *
 * Contact me:
 *     sophowang@163.com
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <errno.h>
#include <arpa/inet.h>

#include <linux/if.h>
#include <linux/if_tun.h>
#include <linux/if_ether.h>
#include <linux/if_packet.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

#define REAL_DEVICE_NAME     "enp0s25"
#define VIR_TAP_DEVICE_NAME  "tap0"

/*
 * Create a TUN/TAP device
 */
int tun_alloc(char *dev, int flags)
{

    struct ifreq ifr;
    int fd, err;
    char *clonedev = "/dev/net/tun";

    if( (fd = open(clonedev , O_RDWR)) < 0 ) {
        perror("Opening /dev/net/tun");
        return fd;
    }

    memset(&ifr, 0, sizeof(ifr));

    ifr.ifr_flags = flags;

    if (*dev) {
        strncpy(ifr.ifr_name, dev, IFNAMSIZ);
    }

    if ((err = ioctl(fd, TUNSETIFF, (void *)&ifr)) < 0 ) {
        perror("ioctl(TUNSETIFF)");
        close(fd);
        return err;
    }

    strcpy(dev, ifr.ifr_name);

    return fd;
}

/*
 * Set interface Flags UP
 */
int set_iff_up(const char *dev_name, int sock)
{
    int ret;
    struct ifreq ifr;
    memcpy(ifr.ifr_name, dev_name, IFNAMSIZ);

    ret = ioctl(sock, SIOCGIFFLAGS, &ifr);
    if (ret < 0) {
        return ret;
    }

    ifr.ifr_flags = ifr.ifr_flags | IFF_UP;
    ret = ioctl(sock, SIOCSIFFLAGS, &ifr);
    if (ret < 0) {
        return ret;
    }
    return ret;
}

/*
 * Set interface Flags promisc up
 */
int set_iff_promisc(const char *dev_name, int sock)
{
    int ret;
    struct ifreq ifr;
    memcpy(ifr.ifr_name, dev_name, IFNAMSIZ);

    ret = ioctl(sock, SIOCGIFFLAGS, &ifr);
    if (ret < 0) {
        return ret;
    }

    ifr.ifr_flags = ifr.ifr_flags | IFF_PROMISC;
    ret = ioctl(sock, SIOCSIFFLAGS, &ifr);
    if (ret < 0) {
        return ret;
    }
    return ret;
}

int main(int argc, char *argv[])
{
    int ret = 0;
    int tap_fd;
    int sock_fd;
    int maxfd;
    unsigned char *buf;

    struct ifreq ifr;
    const char *dev = REAL_DEVICE_NAME;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_ifrn.ifrn_name, dev, IFNAMSIZ);

    char tap_dev[10] = VIR_TAP_DEVICE_NAME;
    tap_fd = tun_alloc(tap_dev, IFF_TAP | IFF_NO_PI);
    if (tap_fd < 0) {
        perror("alloc tap");
        return -1;
    }

    sock_fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (sock_fd < 0) {
        perror("socket");
        return -1;
    }

    ret = set_iff_up(dev, sock_fd);
    if (ret < 0) {
        perror("set_iff_up");
        return -1;
    }

    ret = set_iff_promisc(dev, sock_fd);
    if (ret < 0) {
        perror("set_iff_promisc");
        return -1;
    }

    /*
     * we need bind sock_fd to `REAL_DEVICE_NAME
     */
    struct sockaddr_ll saddr_ll;
    saddr_ll.sll_family = AF_PACKET;
    saddr_ll.sll_protocol = htons(ETH_P_ALL);

    ret = ioctl(sock_fd, SIOCGIFINDEX, &ifr);
    if (ret < 0) {
        perror("ioctl | SIOCGIFINDEX");
        exit(EXIT_FAILURE);
    } else {
        printf("%s index is %d\n", ifr.ifr_ifrn.ifrn_name, ifr.ifr_ifindex);
        saddr_ll.sll_ifindex = ifr.ifr_ifindex;
    }

    ret = ioctl(sock_fd, SIOCGIFHWADDR, &ifr);
    if (ret < 0) {
        perror("ioctl | SIOCGIFHWADDR");
        exit(EXIT_FAILURE);
    } else {
        printf("%s hw addr is ", ifr.ifr_ifrn.ifrn_name);
        int i;
        for (i = 0; i < ETH_ALEN; i++)
            printf("%2x ", (unsigned char)ifr.ifr_hwaddr.sa_data[i]);
        printf("\n");

        memcpy(saddr_ll.sll_addr, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
    }

    saddr_ll.sll_halen = ETH_ALEN;
    ret = bind(sock_fd, (struct sockaddr *)&saddr_ll, sizeof(saddr_ll));
    if (ret < 0) {
        perror("bind ");
        exit(EXIT_FAILURE);
    }

    ret = set_iff_up(tap_dev, sock_fd);
    if (ret < 0) {
        perror("set_iff_up");
        return -1;
    }

    buf = (unsigned char *) malloc(65536);
    memset(buf, 0, 65536);
    struct sockaddr_ll saddr;
    int saddr_len = sizeof(saddr);

    int recv_len_from_dev;

    maxfd = (sock_fd > tap_fd)?sock_fd:tap_fd;
    while (1) {
        fd_set rd_set;
        FD_ZERO(&rd_set);
        FD_SET(sock_fd, &rd_set);
        FD_SET(tap_fd, &rd_set);

        ret = select(maxfd + 1, &rd_set, NULL, NULL, NULL);
        if (ret < 0 && errno == EINTR){
            perror("select EINTR");
        }

        if (ret < 0) {
            perror("select");
            exit(EXIT_FAILURE);
        }

        if(FD_ISSET(sock_fd, &rd_set)) {
            recv_len_from_dev = recvfrom(sock_fd, buf, 65536, 0, (void *)&saddr, (socklen_t *)&saddr_len);
            if (recv_len_from_dev < 0 ) {
                perror("recvfrom");
                return -1;
            }
  
            if (saddr.sll_ifindex == saddr_ll.sll_ifindex) {
                ret = write(tap_fd, buf, recv_len_from_dev);
                if (ret < 0) {
                    perror("write");
                    return -1;
                }
            } else {
                printf("ERROR: packet from index %d\n", saddr.sll_ifindex);
                continue;
            }
        }

        if(FD_ISSET(tap_fd, &rd_set)) {
            int recv_len_from_tap;
            recv_len_from_tap = read(tap_fd, buf, 65536);
            if (recv_len_from_tap < 0) {
                perror("tap fd read");
            }
            int sendlen;
	    // we bind the sock_fd to REAL_DEVICE_NAME, so we can use `send()` instead `sendto()`
            //sendlen = sendto(sock_fd, buf, recv_len_from_tap, 0, (struct sockaddr *)&saddr_ll, sizeof(saddr_ll));
            sendlen = send(sock_fd, buf, recv_len_from_tap, 0);
            if (sendlen < 0) {
                perror("snedto net sock ");
                exit(EXIT_FAILURE);
            }
        }
    } /* while(1) */
}
