#ifndef ALLWINNER_V4L2_H
#define ALLWINNER_V4L2_H

#include <stdio.h>

bool CamReadFrame(uint8_t **Buf);
void CamClose(void);
bool CamOpen(unsigned int width, unsigned int height, unsigned char **inbuf_pointers, int in_buffers);
bool CamStartCapture(void);
void CamStopCapture(void);

#endif // ALLWINNER_V4L2_H
