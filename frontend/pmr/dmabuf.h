/*
    CopyRight (c) Luo Yongping, ALL RIHGTS RESERVED !
*/

#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <include/uapi/linux/nvme_ioctl.h>
#include <linux/dma-buf.h>

inline int mapcmb(std::string device, uint64_t size) {
    int fd, dmabuf_fd;

    struct nvme_alloc_user_cmb opts = {0, 0, 0, size, 0};
    // accquire the dmabuf by nvme ioctl
    fd = open(device.c_str(), O_RDONLY);
    if(fd < 3) {
        perror("open nvme device");
        exit(-1);
    }

    ioctl(fd, NVME_IOCTL_ALLOC_USER_CMB, &opts);
    dmabuf_fd = opts.fd;
    if(dmabuf_fd < 3) {
        perror("get dmabuf fd");
        exit(-1);
    }

    close(fd);
    return dmabuf_fd;
}