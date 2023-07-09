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
static struct buffer          *buffers;
static unsigned int     n_buffers;

 

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
    struct v4l2_buffer buf;
    CLEAR(buf);
    
    *Buf = NULL;
    
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl(fd, VIDIOC_DQBUF, &buf)) 
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

    if(buf.index >= n_buffers)
    {
        printf("%s() Error: buf greater than num buffers\n", __func__);
        return false;
    }


    if (-1 == xioctl(fd, VIDIOC_QBUF, &buf))
    {
         printf("%s() Error: VIDIOC_QBUF\n", __func__);
        return false;
    }
    *Buf = buffers[buf.index].start;

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
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

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
    unsigned int i;

    for (i = 0; i < n_buffers; ++i)
    {
       if (-1 == munmap(buffers[i].start, buffers[i].length))
        {
            printf("%s() Error: munmap\n", __func__);
        }
    }
    free(buffers);
}


static bool init_mmap(void)
{
    struct v4l2_requestbuffers req;

    CLEAR(req);

    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (-1 == xioctl(fd, VIDIOC_REQBUFS, &req)) {
        if (EINVAL == errno) 
        {
            printf("%s() Error: %s does not support memory mapping\n", __func__, dev_name);
            return false;
        } 
        else 
        {
            printf("%s() Error: VIDIOC_REQBUFS\n", __func__);
            return false;
        }
    }

    if (req.count < 2) 
    {
         printf("%s() Error: Insufficient buffer memory on %s\n", __func__, dev_name);
        return false;
    }
    buffers = calloc(req.count, sizeof(*buffers));

    if (!buffers) 
    {
         printf("%s() Error: Out of memory\n", __func__);
        return false;
    }

    for (n_buffers = 0; n_buffers < req.count; ++n_buffers) 
    {
        struct v4l2_buffer buf;

        CLEAR(buf);

        buf.type        = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory      = V4L2_MEMORY_MMAP;
        buf.index       = n_buffers;

        if (-1 == xioctl(fd, VIDIOC_QUERYBUF, &buf))
        {
            printf("%s() Error: VIDIOC_QUERYBUF\n", __func__);
        }

        buffers[n_buffers].length = buf.length;
        buffers[n_buffers].start =
            mmap(NULL /* start anywhere */,
                  buf.length,
                  PROT_READ | PROT_WRITE /* required */,
                  MAP_SHARED /* recommended */,
                  fd, buf.m.offset);

        if (MAP_FAILED == buffers[n_buffers].start)
        {
            printf("%s() Error: mmap\n", __func__);
            return false;
        }
    }
    return true;
}

static bool init_camera(void)
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

    fmt.fmt.pix.width       = 1280; //replace
    fmt.fmt.pix.height      = 720; //replace
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_NV12; //replace
    fmt.fmt.pix.field       = V4L2_FIELD_ANY;

    if (-1 == xioctl(fd, VIDIOC_S_FMT, &fmt))
    {
         printf("%s() Error in VIDIOC_S_FMT\n", __func__);
        return false;
    }

    /* Buggy driver paranoia. */
    min = fmt.fmt.pix.width * 2;
    if (fmt.fmt.pix.bytesperline < min)
    {
        fmt.fmt.pix.bytesperline = min;
    }
    min = fmt.fmt.pix.bytesperline * fmt.fmt.pix.height;
    if (fmt.fmt.pix.sizeimage < min)
    {
        fmt.fmt.pix.sizeimage = min;
    }

    return  init_mmap();
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

bool CamOpen(void)
{
    printf("%s()\n", __func__);
    if(!open_device())
    {
        printf("%s() Could not open device\n", __func__);
		return false;
    }
    if(!init_camera())
    {
        printf("%s() Could not init device\n", __func__);
		return false;
    }
    printf("%s(): Camera opened succesfully\n", __func__);
    return true;
}

