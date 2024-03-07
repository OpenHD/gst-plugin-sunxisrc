/*
 * Cedar Camera src Encoder Plugin
 * Copyright (C) 2014 Enrico Butera <ebutera@users.sourceforge.net>
 *
 * Byte stream utils:
 * Copyright (c) 2014 Jens Kuske <jenskuske@gmail.com>
 *
 * Gst template code:
 * Copyright (C) 2005 Thomas Vander Stichele <thomas@apestaart.org>
 * Copyright (C) 2005 Ronald S. Bultje <rbultje@ronald.bitfreak.net>
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

/**
 * SECTION:element-sunxisrc
 *
 * H264 Encoder plugin using Sunxi hardware engine
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -ve videotestsrc ! sunxisrc ! h264parse ! matroskamux ! filesink location="cedar.mkv"
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <time.h>
#include <stdbool.h>
#include <stdint.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include "gstsunxisrc.h"
#include "AllwinnerV4L2.h"

GST_DEBUG_CATEGORY_STATIC(gst_sunxisrc_debug);
#define GST_CAT_DEFAULT gst_sunxisrc_debug

#define WIDTH 1280
#define HEIGHT 720
//#define BUF_SIZE ((WIDTH * HEIGHT * 3) / 2)
#define BUF_SIZE ((WIDTH * HEIGHT * 2))
#define NUM_BUFFERS 3

#define NUM_TIME_STEPS 7
uint32_t FrameTimes[NUM_TIME_STEPS] = {0};
uint32_t MaxFrameTimes[NUM_TIME_STEPS] = {0};
#define NUM_FRAMERATE_TIMES 60
uint32_t FrameNum[NUM_FRAMERATE_TIMES] = {0};
uint32_t FramerateTimes[NUM_FRAMERATE_TIMES] = {0};
uint32_t EncTimes[NUM_FRAMERATE_TIMES] = {0};
static bool InitDone = false;
long long GetNowUs()
{
    struct timeval now;
    gettimeofday(&now, NULL);
    return now.tv_sec * 1000000 + now.tv_usec;
}

enum
{
	PROP_0,
    PROP_BITRATE,
	PROP_QP,
	PROP_KEYFRAME_INTERVAL,
	PROP_ALWAYS_COPY
};


static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE("src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 1, MAX ], " "height = (int) [ 1, MAX ], "
        "stream-format = (string) bytestream, "
        "alignment = (string) nal, "
        "profile = (string) { high, main, baseline,  }")
    );

#define gst_sunxisrc_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE(GstSunxiSrc, gst_sunxisrc, 
    GST_TYPE_PUSH_SRC,
    GST_DEBUG_CATEGORY_INIT (gst_sunxisrc_debug, "sunxisrc",
      0, "Template sunxisrc"));

static 	struct h264enc_params H264Params = {
    .width = WIDTH,
    .height = HEIGHT,
    .src_width = WIDTH,
    .src_height = HEIGHT,
    .profile_idc = 77,	// Main Profile
    .level_idc = 41,
    .qp = 20,
    .bitrate = 10000,
    .keyframe_interval = 8
};

GstSunxiSrc *filt= NULL;  // This definitely shouldn't be a global, should just be passed to each function by gstreamer;s

static void gst_sunxisrc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void gst_sunxisrc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

//static gboolean gst_sunxisrc_set_caps(GstBaseSrc * bsrc, GstCaps * caps);
//static GstCaps *gst_sunxisrc_get_caps (GstBaseSrc * bsrc, GstCaps * filter);
static GstFlowReturn gst_sunxisrc_create(GstPushSrc * psrc, GstBuffer **buf);

static GstStateChangeReturn gst_sunxisrc_change_state(GstElement *element, GstStateChange transition);
static gboolean gst_sunxisrc_start (GstBaseSrc * basesrc);
static gboolean gst_sunxisrc_stop (GstBaseSrc * basesrc);

/* initialize the sunxisrc's class */
static void gst_sunxisrc_class_init(GstSunxiSrcClass *klass)
{
	GObjectClass *gobject_class;
	GstElementClass *gstelement_class;
    GstBaseSrcClass *gstbasesrc_class;
    GstPushSrcClass *gstpushsrc_class;

    printf("Entering %s\n",  __func__);
    
    
	gobject_class = (GObjectClass *) klass;
	gstelement_class = (GstElementClass *) klass;
    gstbasesrc_class = (GstBaseSrcClass *) klass;
    gstpushsrc_class = (GstPushSrcClass *) klass;
	gst_element_class_set_details_simple(gstelement_class,
		"sunxisrc_cs",
		"Sunxi Camera src Encoder CS",
		"Camera src Encoder Plugin for Sunxi hardware based on the Allwinner (closed source) library",
		"Pete Allen <peter.allenm@gmail.com>, Based on H264 encoder code by Enrico Butera <ebutera@users.berlios.de>, Kyle Hu <kyle.hu.gz@gmail.com>, George Talusan <george.talusan@gmail.com>");

	gst_element_class_add_pad_template(gstelement_class, gst_static_pad_template_get(&src_factory));
  
	gobject_class->set_property = gst_sunxisrc_set_property;
	gobject_class->get_property = gst_sunxisrc_get_property;

	gstelement_class->change_state = gst_sunxisrc_change_state;

    //gstbasesrc_class->get_caps = gst_sunxisrc_get_caps;
    //gstbasesrc_class->set_caps = gst_sunxisrc_set_caps;
    gstbasesrc_class->start = gst_sunxisrc_start;
    gstbasesrc_class->stop = gst_sunxisrc_stop;
    gstpushsrc_class->create = gst_sunxisrc_create;
    
    g_object_class_install_property (gobject_class, PROP_BITRATE,
		g_param_spec_int ("bitrate", "BITRATE", "H264 target bitrate (kbits)",
		1000, 64000, 5000, G_PARAM_READWRITE));
        
	g_object_class_install_property (gobject_class, PROP_QP,
		g_param_spec_int ("qp", "QP", "H264 quantization parameters",
		0, 47, 15, G_PARAM_READWRITE));

	g_object_class_install_property (gobject_class, PROP_KEYFRAME_INTERVAL,
		g_param_spec_int ("keyint", "keyframe-interval", "Keyframe Interval",
		0, 500, 0, G_PARAM_READWRITE));

	g_object_class_install_property (gobject_class, PROP_ALWAYS_COPY,
		g_param_spec_boolean ("always-copy", "Always Copy", "Always Copy Buffers",
		TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
    printf("Done %s\n",  __func__);
}


/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void gst_sunxisrc_init(GstSunxiSrc *filter)
{
	//gst_pad_set_chain_function(filter->sinkpad, GST_DEBUG_FUNCPTR(gst_sunxisrc_chain));
    printf("Entering %s\n",  __func__);
   gst_base_src_set_live (GST_BASE_SRC (filter), TRUE);
   gst_base_src_set_format (GST_BASE_SRC (filter), GST_FORMAT_TIME);
    
	//filter->srcpad = gst_pad_new_from_static_template(&src_factory, "src");
	//gst_pad_use_fixed_caps(filter->srcpad);

	//gst_element_add_pad(GST_ELEMENT(filter), filter->srcpad);

    printf("Done %s\n",  __func__);
}

static void gst_sunxisrc_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	GstSunxiSrc *filter = GST_SUNXISRC(object);
    printf("Entering %s\n",  __func__);
	switch (prop_id) {
    case PROP_BITRATE:
		filter->bitrate = g_value_get_int(value);
        H264Params.bitrate = filter->bitrate;
        h264_set_bitrate(filter->bitrate);
		break;
	case PROP_QP:
		filter->pic_init_qp = g_value_get_int(value);
		break;

	case PROP_KEYFRAME_INTERVAL:
		filter->keyframe_interval = g_value_get_int(value);
        H264Params.keyframe_interval = filter->keyframe_interval;
		break;

	case PROP_ALWAYS_COPY:
		filter->always_copy = g_value_get_boolean(value);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
    printf("Done %s\n",  __func__);
}

static void gst_sunxisrc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstSunxiSrc *filter = GST_SUNXISRC(object);
    printf("Entering %s\n",  __func__);
	switch (prop_id) {
    case PROP_BITRATE:
		g_value_set_int(value, filter->bitrate);
		break;
	case PROP_QP:
		g_value_set_int(value, filter->pic_init_qp);
		break;

	case PROP_KEYFRAME_INTERVAL:
		g_value_set_int(value, filter->keyframe_interval);
		break;

	case PROP_ALWAYS_COPY:
		g_value_set_boolean(value, filter->always_copy);
		break;

	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		break;
	}
    printf("Done %s\n",  __func__);
}

#if 0
static GstCaps *
gst_sunxisrc_get_caps (GstBaseSrc * bsrc, GstCaps * filter)
{
  GstSunxiSrc *Sunxi;
  GstCaps *caps;

  Sunxi = GST_SUNXISRC(gst_pad_get_parent(bsrc));
  if(Sunxi->caps != NULL)
  {
    caps = gst_caps_copy (Sunxi->caps);
  }
  else
  {
      caps = gst_pad_get_pad_template_caps (GST_BASE_SRC_PAD (Sunxi));
  }
  GST_DEBUG_OBJECT (Sunxi, "The caps before filtering are %" GST_PTR_FORMAT,
      caps);

  if (filter && caps) {
    GstCaps *tmp = gst_caps_intersect (caps, filter);
    gst_caps_unref (caps);
    caps = tmp;
  }

  GST_DEBUG_OBJECT (Sunxi, "The caps after filtering are %" GST_PTR_FORMAT, caps);

  return caps;
}

/* GstElement vmethod implementations */

/* this function handles the link with other elements */
static gboolean gst_sunxisrc_set_caps(GstBaseSrc * bsrc, GstCaps * caps)
{
	GstSunxiSrc *filter;
	GstVideoInfo vinfo;

	filter = GST_SUNXISRC(gst_pad_get_parent(bsrc));


    GstStructure *structure = gst_caps_get_structure (caps, 0);
    if (!gst_structure_has_name (structure, "video/x-h264")) 
    {
        printf("Invalid GST_VIDEO_FORMAT\n");
        goto unsupported_caps;
    }
    gst_video_info_from_caps(&vinfo, caps);
    
    if((vinfo.width != 1280) || (vinfo.height != 720))
    {
        printf("Invalid width/height %dx%d\n", vinfo.width, vinfo.height);
        goto unsupported_caps;
    }
    filter->width = vinfo.width;
    filter->height = vinfo.height;
    
    if(filter->caps)
    {
        gst_caps_unref(filter->caps);
    }
    filter->caps = gst_caps_copy(caps);
    return TRUE;

    unsupported_caps:
      GST_ERROR_OBJECT (bsrc, "Unsupported caps: %" GST_PTR_FORMAT, caps);
      return FALSE;
}
#endif


/* create function
 * this function does the actual processing
 */
static GstFlowReturn gst_sunxisrc_create(GstPushSrc * psrc, GstBuffer **buf)
{
    static int32_t TimeUntilPPS = 1;
	GstSunxiSrc *filter;
	GstBuffer *outbuf = NULL;
	gsize len0, len1;
    gsize pps_sps_len = 0;
   // printf("Entering %s\n",  __func__);
	filter = GST_SUNXISRC(psrc);
    //filter = filt;
    static unsigned int StartFrameTime = 0;
    
    int FrameCTime = 0;
    FrameTimes[FrameCTime ++] = GetNowUs();
    
    /* Wait for a frame to come in */
    /* Give MIPI a new buffer */
    /* Start encode */
    /* Create buffer for encode result */
    /* Wait for encode to finish */
    /* Free camera buffer */
    /* Send H264 out */


    uint8_t *CamBuf = NULL;
    if(!CamReadFrame(&CamBuf))
    {
/*         GST_ELEMENT_ERROR (src, RESOURCE, FAILED, 
        "SunxiSrc error calling AllwinnerV4l2WaitForFrame", (NULL)); */
        return GST_FLOW_ERROR;
    }
    GstClock *clock = gst_element_get_clock (GST_ELEMENT (filter));
    GstClockTime clock_time = gst_clock_get_time (clock);
    FrameTimes[FrameCTime ++] = GetNowUs();
    
    GST_CAT_DEBUG(gst_sunxisrc_debug, "received frame from allwinner");
    
	h264enc_set_input_buffer(filter->enc, CamBuf, BUF_SIZE);
    FrameTimes[FrameCTime ++] = GetNowUs();

	if (!h264enc_encode_picture(filter->enc)) {
         GST_ERROR("cedar h264 encode failed\n");
		return GST_FLOW_ERROR;
	}
    FrameTimes[FrameCTime ++] = GetNowUs();
    
	len0 = h264enc_get_bytestream_length(filter->enc, 0);
	len1 = h264enc_get_bytestream_length(filter->enc, 1);
    FrameTimes[FrameCTime ++] = GetNowUs();
    
    bool DoPPS = false;
    // Send SPS and PPS with keyframes or sporadically if we don't have keyframes
    TimeUntilPPS --;
    //if((h264enc_is_keyframe(filter->enc)) || (TimeUntilPPS <= 0))
    if((TimeUntilPPS <= 0))
    {
        pps_sps_len = h264enc_get_initial_bytestream_length(filter->enc);
        TimeUntilPPS = 60;
        DoPPS = true;
    }
        
    gsize offset = 0;
    
    outbuf = gst_buffer_new_and_alloc(len0 + len1 + pps_sps_len);
    

    if(DoPPS == true)
    {
        gst_buffer_fill(outbuf, offset, h264enc_get_intial_bytestream_buffer(filter->enc), pps_sps_len);
        offset += pps_sps_len;
    }

    if(len0)
    {
        gst_buffer_fill(outbuf, offset, h264enc_get_bytestream_buffer(filter->enc, 0), len0);
        offset += len0;
    }
    if(len1 > 0)
    {
        gst_buffer_fill(outbuf, offset, h264enc_get_bytestream_buffer(filter->enc, 1), len1);
        offset += len1;
    }
    FrameTimes[FrameCTime ++] = GetNowUs();
	
    h264enc_done_outputbuffer(filter->enc);
    
    GST_BUFFER_TIMESTAMP(outbuf) = GST_CLOCK_DIFF (gst_element_get_base_time (GST_ELEMENT (filter)), clock_time);

    /*
    if (src->stop_requested) 
    {
        if (*buf != NULL) 
        {
             gst_buffer_unref (*buf);
             *buf = NULL;
        }
        return GST_FLOW_FLUSHING;
    }
    */
   GST_CAT_DEBUG(gst_sunxisrc_debug, "create method done");

    *buf = outbuf;
    
    GST_BUFFER_DURATION (*buf) = filter->duration;
    FrameTimes[FrameCTime ++] = GetNowUs();
    
    {
        static long long AvEnc = 0;
        static long long AvTotTime = 0;
        static long long AvTimeSinceLastFrame = 0;
        static unsigned int MaxEnc = 0;
        static unsigned int MaxTotTime =  0;
        static unsigned int MaxTimeSinceLastFrame =  0;
        static int Periodic = 0;
        static unsigned int FrameCount = 0;
        //static int FramerateTimeCount = 0;
    
        FrameCount ++;
        unsigned int EncTime = FrameTimes[3] - FrameTimes[2];
        unsigned int TotTime = FrameTimes[6] - FrameTimes[0];
        unsigned int TimeSinceLastFrame = FrameTimes[1] - StartFrameTime;
        
        #if 0
        if(TimeSinceLastFrame > 18500)
        {
            FramerateTimes[FramerateTimeCount] = TimeSinceLastFrame;
            EncTimes[FramerateTimeCount] = EncTime;
            FrameNum[FramerateTimeCount] = FrameCount;
            FramerateTimeCount ++;
        }
        #endif
        
        StartFrameTime = FrameTimes[1];
        
        AvEnc += EncTime;
        AvTotTime += TotTime;
        AvTimeSinceLastFrame += TimeSinceLastFrame;

        
        if(EncTime > MaxEnc)
        {
            MaxEnc = EncTime;
        }
        if(TimeSinceLastFrame > MaxTimeSinceLastFrame)
        {
            MaxTimeSinceLastFrame = TimeSinceLastFrame;
        }
        
        if(TotTime > MaxTotTime)
        {
            MaxTotTime =  TotTime;
            memcpy(MaxFrameTimes, FrameTimes, NUM_TIME_STEPS * sizeof(uint32_t));
        }
        #if 0
        if(EncTime > 12000)
        {
            printf("Encode times was %dus, total %dus\n", (unsigned int) EncTime, (unsigned int) TotTime);
        }
        else if(TotTime > 30000)
        {
            printf("Tot time was %dus, encode %dus\n", (unsigned int) TotTime, (unsigned int) EncTime);
        }
        #endif
        if(Periodic ++ >= 60)
        {
            Periodic = 0;
            AvEnc /= 60;
            AvTotTime /= 60;
            AvTimeSinceLastFrame /= 60;
            fprintf(stderr, "TimeSinceLast: %d, Frame time: %d:%d:%d:%d:%d:%d Tot %d. Av encode was %d, Max %d, Tot Av %d max %d. LastFrame: Av %d, Max %d\n", (unsigned int)TimeSinceLastFrame, (unsigned int)(MaxFrameTimes[1]-MaxFrameTimes[0]), (unsigned int)(MaxFrameTimes[2]-MaxFrameTimes[1]), (unsigned int)(MaxFrameTimes[3]-MaxFrameTimes[2]), (unsigned int)(MaxFrameTimes[4]-MaxFrameTimes[3]), (unsigned int)(MaxFrameTimes[5]-MaxFrameTimes[4]), (unsigned int)(MaxFrameTimes[6]-MaxFrameTimes[5]), TotTime, (unsigned int)AvEnc, MaxEnc, (unsigned int)AvTotTime, MaxTotTime, (unsigned int)AvTimeSinceLastFrame, MaxTimeSinceLastFrame);
            AvEnc = AvTotTime = MaxEnc = MaxTotTime = AvTimeSinceLastFrame = MaxTimeSinceLastFrame = 0;
#if 0
            if(fp)
            {
                for(int i = 0; i < FramerateTimeCount; i++)
                    fprintf(fp, "%d\t%d\t%d\n", FrameNum[i], FramerateTimes[i], EncTimes[i]);
                FramerateTimeCount = 0;
            }
            else
            {
                fp = fopen("/home/openhd/FrameTimeLog.txt","w");
            }
#endif
        }
    }
   // printf("Done %s\n",  __func__);
	return GST_FLOW_OK;
}

static gboolean
gst_sunxisrc_start (GstBaseSrc * basesrc)
{
    GstSunxiSrc *src = GST_SUNXISRC(GST_OBJECT_PARENT(basesrc));
    GST_LOG_OBJECT(src, "Creating SunxiSrc pipeline");
    printf("Entering %s\n",  __func__);

    src->width = WIDTH;
    src->height = HEIGHT;
    
    if(!InitDone)
    {
        
        if (!src->enc) {
            printf("\n**********Starting new encoder************\n");
            src->width = WIDTH;
            src->height = HEIGHT;
            src->bitrate = H264Params.bitrate;
            src->keyframe_interval = H264Params.keyframe_interval;
            src->always_copy = true;
            src->fps_d = 1;
            src->fps_n = 60;
            
            src->duration = gst_util_uint64_scale_int (GST_SECOND, src->fps_d,
                            src->fps_n);
            src->enc = h264enc_new(&H264Params, NUM_BUFFERS);
            
            if (!src->enc) {
                GST_ERROR("Cannot initialize H.264 encoder");
                return GST_FLOW_ERROR;
            }
        }
        InitDone = true;
    }    
        printf("gst_sunxisrc_start(): w %d, h %d\n",  src->width,  src->height);
        /* V4l2 init */
        if(!CamOpen(src->width, src->height, h264_get_buffers(src->enc), NUM_BUFFERS))
        {
            printf("Cannot open camera, fail\n");
            return FALSE;
        }
    
    /* Some GST structrs to be filled out */
    gst_video_info_init(&src->info);
    gst_video_info_set_format(&src->info, GST_VIDEO_FORMAT_NV12, src->width, src->height);

    src->caps = gst_video_info_to_caps (&src->info);
    gst_base_src_start_complete(basesrc, GST_FLOW_OK);
    
    filt = src;
        
    printf("Done %s\n",  __func__);
    return TRUE;
}


static gboolean
gst_sunxisrc_stop (GstBaseSrc * basesrc)
{
    //GstSunxiSrc *src = GST_SUNXISRC(GST_OBJECT_PARENT(basesrc));
  /* V4l2 shutdown */
    printf("Entering %s\n",  __func__);
    
    CamClose();
    
    printf("Done %s\n",  __func__);
  return TRUE;
}

static GstStateChangeReturn gst_sunxisrc_change_state(GstElement *element, GstStateChange transition)
{
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
	GstSunxiSrc *filter = GST_SUNXISRC(element);
    printf("Entering %s\n",  __func__);
	switch (transition) {
	case GST_STATE_CHANGE_NULL_TO_READY:
        printf("GST_STATE_CHANGE_NULL_TO_READY\n");
		break;

	case GST_STATE_CHANGE_READY_TO_PAUSED:
        printf("Stopping camera capture\n");
        CamStopCapture();
		break;

	case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
        printf("Starting camera capture\n");
        if(!CamStartCapture())
        {
            printf("Could not start camera capture\n");
        }
		break;

	default:
        
        //printf("GST_STATE_CHANGE unhandled %d\n", transition);
		// silence compiler warning...
		break;
	}

	ret = GST_ELEMENT_CLASS(gst_sunxisrc_parent_class)->change_state(element, transition);
	if (ret == GST_STATE_CHANGE_FAILURE)
    {
        printf("Could not change state\n");
		return ret;
    }
    printf("Done %s\n",  __func__);
	return ret;
}

/* entry point to initialize the plug-in */
static gboolean sunxisrc_init(GstPlugin *sunxisrc)
{
	// debug category for fltering log messages
	GST_DEBUG_CATEGORY_INIT(gst_sunxisrc_debug, "sunxisrc", 0, "Sunxi Camera src Encoder");
    printf("Done %s\n",  __func__);
	return gst_element_register(sunxisrc, "sunxisrc", GST_RANK_NONE, GST_TYPE_SUNXISRC);
    printf("Done %s\n",  __func__);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef PACKAGE
#define PACKAGE "myfirstsunxisrc"
#endif

// gstreamer looks for this structure to register sunxisrcs
GST_PLUGIN_DEFINE (
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	sunxisrc,
	"Sunxi Camera src Encoder CS",
	sunxisrc_init,
	VERSION,
	"LGPL",
	"Sunxi",
	"https://github.com/peteallenm/gst-plugin-sunxisrc"
)
