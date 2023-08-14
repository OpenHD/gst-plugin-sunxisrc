#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/stat.h>

#define CLEAR(x) memset(&(x), 0, sizeof(x))

struct buffer {
        void   *start;
        size_t  length;
};

static char            *dev_name = "/dev/video0";
static int              fd = -1;
static struct buffer          *buffers = NULL;
static unsigned int     n_buffers = 0;
static unsigned char **buffer_pointers = NULL;

static struct v4l2_buffer CInputBuffer;
static bool BufNeedsQueueing = false;


static int xioctl(int fh, int request, void *arg)
{
    int r;

    do 
    {
            r = ioctl(fh, request, arg);
    } while (-1 == r && EINTR == errno);

    return r;
}

static bool read_frame(uint8_t **Buf)
{
    CLEAR(CInputBuffer);
    
    *Buf = NULL;
    
    CInputBuffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    CInputBuffer.memory = V4L2_MEMORY_USERPTR;

    if (-1 == xioctl(fd, VIDIOC_DQBUF, &CInputBuffer)) 
    {
        switch (errno) 
        {
            case EAGAIN:
                    return false;

            case EIO:
                    /* Could ignore EIO, see spec. */

                    /* fall through */

            default:
                printf("%s() Error: VIDIOC_DQBUF\n", __func__);
                return false;
        }
    }
    #if 0
    for (i = 0; i < n_buffers; ++i)
            if (buf.m.userptr == (unsigned long)buffers[i].start
                && buf.length == buffers[i].length)
                    break;

    assert(i < n_buffers);
#endif
    *Buf = (unsigned char *)CInputBuffer.m.userptr;

    //printf("Done, %x,%x,%x,%x\n", *((unsigned int *)buf.m.userptr), *(((unsigned int *)buf.m.userptr) + 1), *(((unsigned int *)buf.m.userptr) + 2), *(((unsigned int *)buf.m.userptr) + 3));
    BufNeedsQueueing = true;
    
    return true;
}

void CamStopCapture(void)
{
    enum v4l2_buf_type type;
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(fd, VIDIOC_STREAMOFF, &type))
    {
         printf("%s() Error:VIDIOC_STREAMOFF \n", __func__);
    }
}

bool CamStartCapture(void)
{
    unsigned int i;
    enum v4l2_buf_type type;

    for (i = 0; i < n_buffers; ++i) 
    {
        struct v4l2_buffer buf;

        CLEAR(buf);
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_USERPTR;
        buf.index = i;
        buf.m.userptr = (unsigned long)buffers[i].start;
        buf.length = buffers[i].length;
        if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
        {
            printf("%s() Error: VIDIOC_QBUF\n", __func__);
            return false;
        }
    }
    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (-1 == xioctl(fd, VIDIOC_STREAMON, &type))
    {
         printf("%s() Error: VIDIOC_STREAMON\n", __func__);
        return false;
    }
    return true;
}
            

static void uninit_camera(void)
{
    #if 0
    unsigned int i;

    for (i = 0; i < n_buffers; ++i)
    {
       if (-1 == munmap(buffers[i].start, buffers[i].length))
        {
            printf("%s() Error: munmap\n", __func__);
        }
    }
    #endif
    
    free(buffers);
}

static bool init_userp(unsigned int buffer_size)
{
    struct v4l2_requestbuffers req;
    
    CLEAR(req);

    req.count  = n_buffers;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_USERPTR;

    if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) 
    {
        if (EINVAL == errno) 
        {
            fprintf(stderr, "%s does not support "
                     "user pointer i/o\n", dev_name);
            return false;
        } 
        else 
        {
            printf("Error VIDIOC_REQBUFS");
            return false;
        }
    }

    buffers = calloc(n_buffers, sizeof(*buffers));

    if (!buffers) {
        fprintf(stderr, "Out of memory\n");
        return false;
    }

    for (int i = 0; i < n_buffers; ++i) {
        buffers[i].length = buffer_size;
        //buffers[n_buffers].start = malloc(buffer_size);
        printf("Buffer %d set to %p\n", n_buffers, buffer_pointers[i]);
        buffers[i].start = buffer_pointers[i];

        if (!buffers[i].start) 
        {
            fprintf(stderr, "Out of memory\n");
            return false;
        }
    }
    return true;
}


static bool init_camera(int width, int height)
{
    struct v4l2_capability cap;
    struct v4l2_cropcap cropcap;
    struct v4l2_crop crop;
    struct v4l2_format fmt;
    unsigned int min;

    if (-1 == xioctl(fd, VIDIOC_QUERYCAP, &cap)) 
    {
        if (EINVAL == errno) 
        {
            printf("%s() Error: %s is no V4L2 device\n", __func__, dev_name);
            return false;
        } 
        else 
        {
            printf("%s()Error:VIDIOC_QUERYCAP \n", __func__);
            return false;
        }
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) 
    {
        printf("%s() Error:%s is no video capture device \n", __func__, dev_name);
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_STREAMING)) 
    {
         printf("%s() Error: %s does not support streaming i/o\n", __func__, dev_name);
        return false;
    }

    /* Select video input, video standard and tune here. */
    CLEAR(cropcap);

    cropcap.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (0 == xioctl(fd, VIDIOC_CROPCAP, &cropcap)) 
    {
        crop.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        crop.c = cropcap.defrect; /* reset to default */

        xioctl(fd, VIDIOC_S_CROP, &crop);
    } 
    
    CLEAR(fmt);

    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    fmt.fmt.pix.width       = width; 
    fmt.fmt.pix.height      = height; 
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12; //replace
    //fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV; //replace
    
    fmt.fmt.pix.field       = V4L2_FIELD_ANY;

    if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
    {
         printf("%s() Error in VIDIOC_S_FMT\n", __func__);
        return false;
    }

    /* Buggy driver paranoia. */
    min = (fmt.fmt.pix.width * 2);
    if (fmt.fmt.pix.bytesperline < min)
    {
        fmt.fmt.pix.bytesperline = min;
    }
    min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
    if (fmt.fmt.pix.sizeimage < min)
    {
        fmt.fmt.pix.sizeimage = min;
    }
    printf("Init buffer of %d bytes, expecting %d\n", fmt.fmt.pix.sizeimage, width * height * 2);
    return  init_userp(fmt.fmt.pix.sizeimage); //init_mmap();
}

static void close_device(void)
{
    if (-1 == close(fd))
    {
         printf("%s() could not close\n", __func__);
    }
    fd = -1;
}

static bool open_device(void)
{
    struct stat st;

    if (-1 == stat(dev_name, &st)) 
    {
        printf("%s() Cannot identify '%s': %d, %s\n",
                 __func__, dev_name, errno, strerror(errno));
        return false;
    }

    if (!S_ISCHR(st.st_mode)) 
    {
        printf("%s() %s is no device\n", __func__, dev_name);
        return false;
    }

    fd = open(dev_name, O_RDWR /* required */ | O_NONBLOCK, 0);

    if (-1 == fd) 
    {
        printf("%s() Cannot open '%s': %d, %s\n",
                 __func__, dev_name, errno, strerror(errno));
        return false;
    }
    return true;
}

bool CamReadFrame(uint8_t **Buf)
{
    fd_set fds;
    struct timeval tv;
    int r;
    
    *Buf = NULL;
    
    if(true == BufNeedsQueueing)
    {        
        if (-1 == xioctl(fd, VIDIOC_QBUF, &CInputBuffer))
        {
            printf("Error VIDIOC_QBUF");
            return false;
        }
        BufNeedsQueueing = false;
    }
    
    FD_ZERO(&fds);
    FD_SET(fd, &fds);

    /* Timeout. */
    tv.tv_sec = 0;
    tv.tv_usec = 100 * 1000;

    r = select(fd + 1, &fds, NULL, NULL, &tv);

    if ((-1 == r) && (EINTR != errno)) 
    {
        printf("%s() Error: select\n", __func__);
        return false;
    }

    if (0 == r) 
    {
        printf("%s() Error: select timeout\n", __func__);
        return false;
    }

    return read_frame(Buf);
}
        
void CamClose(void)
{
    uninit_camera();
    close_device();
}

bool CamOpen(unsigned int width, unsigned int height, unsigned char **inbuf_pointers, int in_buffers)
{
    printf("%s()\n", __func__);
    buffer_pointers = inbuf_pointers;
    n_buffers = in_buffers;
    if(!open_device())
    {
        printf("%s() Could not open device\n", __func__);
		return false;
    }
    if(!init_camera(width, height))
    {
        printf("%s() Could not init device\n", __func__);
		return false;
    }
    printf("%s(): Camera opened succesfully\n", __func__);
    return true;
}
