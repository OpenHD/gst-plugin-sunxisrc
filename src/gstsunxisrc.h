/*
 * Cedar H264 Encoder Plugin
 * Copyright (C) 2014 Enrico Butera <ebutera@users.sourceforge.net>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_SUNXISRC_H__
#define __GST_SUNXISRC_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/base/gstpushsrc.h>

#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>

#include "h264enc.h"

G_BEGIN_DECLS

/* #defines don't like whitespacey bits */
#define GST_TYPE_SUNXISRC			(gst_sunxisrc_get_type())
#define GST_SUNXISRC(obj)			(G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_SUNXISRC, GstSunxiSrc))
#define GST_SUNXISRC_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_SUNXISRC, GstSunxiSrcClass))
#define GST_IS_SUNXISRC(obj)		(G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_SUNXISRC))
#define GST_IS_SUNXISRC_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_SUNXISRC))

typedef struct _GstSunxiSrc		GstSunxiSrc;
typedef struct _GstSunxiSrcClass	GstSunxiSrcClass;

struct _GstSunxiSrc
{
	GstPushSrc element;

	GstPad *srcpad;
    GstCaps *caps;
    GstVideoInfo info;
    GstClockTime duration;
    
    gint32 bitrate;
	gint32 pic_init_qp;
	gint32 keyframe_interval;
	gboolean always_copy;
    
	int width;
	int height;

    gint32 fps_d;
    gint32 fps_n;
    
	h264enc *enc;
};

struct _GstSunxiSrcClass 
{
	GstPushSrcClass parent_class;
};

GType gst_sunxisrc_get_type(void);

G_END_DECLS

#endif /* __GST_SUNXISRC_H__ */
