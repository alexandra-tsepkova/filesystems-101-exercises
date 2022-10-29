#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/stat.h>
#include <liburing.h>
#include <solution.h>


#define QD	4
#define BS	(256 * 1024)

struct io_data {
    int read;
    int offset;
    struct iovec iov;
};

static int get_file_size(int fd, off_t *size){
    struct stat f_stat;
    if (fstat(fd, &f_stat) < 0){
        return 1;
    }
    *size = f_stat.st_size;
    return 0;
}

static int setup(struct io_uring *ring)
{
    int result;

    result = io_uring_queue_init(QD, ring, 0);
    if (result < 0) {
        return 1;
    }

    return 0;
}

static int read_queue(struct io_uring *ring, int fd, int offset, off_t size){
    struct io_data *data = {0};
    struct io_uring_sqe *sqe;

    sqe = io_uring_get_sqe(ring);
    if (!sqe){
        return 1;
    }
    data = malloc(size + sizeof(*data));
    if (!data)
        return 1;

    data->read = 1;
    data->offset = offset;

    data->iov.iov_base = data + 1;
    data->iov.iov_len = size;

    io_uring_prep_readv(sqe, fd, &data->iov, 1, offset);
    io_uring_sqe_set_data(sqe, data);
//    printf("Read with size %ld, offset = %d\n", size, offset);
    return 0;
}

static int write_queue(struct io_uring *ring, int len_read, struct io_data *data, int out){
    struct io_uring_sqe *sqe;
    sqe = io_uring_get_sqe(ring);

    data->iov.iov_len = len_read;
    data->read = 0;

    io_uring_prep_writev(sqe, out, &data->iov, 1, data->offset);
    io_uring_sqe_set_data(sqe, data);

    int ret = io_uring_submit(ring);
    if(ret < 0){
        return ret;
    }
    return 0;
}

int copy(int in, int out)
{
    (void) out;
    struct io_uring ring;
    off_t in_size;

    int ret = 0;

    if (get_file_size(in, &in_size) == 1){
        return -errno;
    }

    if (setup(&ring) == 1){
        return -errno;
    }
    const off_t file_size = in_size;

    off_t size = in_size; // size of one read
    off_t offset = 0;  // offset for each read
    for(int i = 0; i < QD; ++i){
        if (size > BS){
            size = BS;
        }
        else if (in_size == 0) {
            break;
        }
        ret = read_queue(&ring,in, offset, size);
        if (ret != 0) {
            return ret;
        }

        in_size -= size;
        offset += size;
    }

    ret = io_uring_submit(&ring);
    if (ret < 0) {
        return ret; // in this case ret is set to -errno
    }

    int writes_left = ret;
    int pending = ret;

    struct io_uring_cqe *cqe;

    while ((writes_left > 0) || (offset < file_size) || (pending > 0)){
        struct io_data *data = {0};

        int ret = io_uring_wait_cqe(&ring, &cqe);
        if(ret < 0){
            free(data);
            return ret;
        }

        data = io_uring_cqe_get_data(cqe);
        io_uring_cqe_seen(&ring, cqe);
        int len_read = cqe->res;
        if (len_read < 0){
            free(data);
            return len_read;
        }
        pending--;


        if (data->read == 0){

            if (offset >= file_size){
                free(data);
                continue;
            }
            else{
                ret = read_queue(&ring,  in, offset, size);
                if (ret != 0) {
                    free(data);
                    return ret;
                }
                pending++;
                writes_left++;
                ret = io_uring_submit(&ring);
                if(ret < 0){
                    free(data);
                    return -ret;
                }

                offset += size;

                if (size < BS){
                    break;
                }
            }
            free(data);
        }
        else {
            pending++;
            writes_left--;
            ret = write_queue(&ring, len_read, data, out);
            if (ret != 0) {
                free(data);
                return ret;
            }
       }
    }
    io_uring_queue_exit(&ring);
    return 0;
}