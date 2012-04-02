#include <gst/gst.h>
#include <glib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>

#define ERR(e,arg...) do {\
  g_printerr ("%s():%d: " e "\n",__func__,__LINE__, ##arg);\
} while(0)

#define CHECKP(p, expr) do { \
  if(!(p)) {\
      ERR("condition %s failed", #p); \
      expr; \
  } \
} while(0)

#define GOTO(code,label) do { code; goto label ; } while(0)

static gboolean bus_call (GstBus *bus, GstMessage *msg, gpointer data)
{
  GMainLoop *loop = (GMainLoop *) data;

  switch (GST_MESSAGE_TYPE (msg)) {

    case GST_MESSAGE_EOS:
      g_printerr ("End of stream\n");
      g_main_loop_quit (loop);
      break;

    case GST_MESSAGE_ERROR: {
      gchar  *debug;
      GError *error;

      gst_message_parse_error (msg, &error, &debug);
      g_free (debug);

      g_printerr ("Error: %s\n", error->message);
      g_error_free (error);

      g_main_loop_quit (loop);
      break;
    }
    default:
      break;
  }

  return TRUE;
}

void usage(const char *progname)
{
  g_printerr ("Usage: %s VPID VTYPE APID ATYPE\n", progname);
  exit(-1);
}

struct pipeline {
  GMainLoop *loop;
  GstBus *bus;
  GstElement *pipeline;

  GstElement *mdemuxsrc;
  GstElement *msvdhddec;
  GstElement *vqueue;
  GstElement *mvdusink;

  GstElement *aqueue;
  GstElement *decodebin;
  GstElement *aconvert;
  GstElement *alsasink;

  int apid, vpid;
  char atype[20], vtype[20];
};

#define LINESIZE 50

#define STREQ(a,b) (strcmp(a,b)==0)

int name2state(const char *line, GstState *state)
{
  if (STREQ(line,"null")) {
    *state = GST_STATE_NULL;
  }
  else if(STREQ(line,"ready")) {
    *state = GST_STATE_READY;
  }
  else if (STREQ(line,"paused")) {
    *state = GST_STATE_PAUSED;
  }
  else if (STREQ(line,"playing")) {
    *state = GST_STATE_PLAYING;
  }
  else {
    return -1;
  }
  return 0;
}

static void mdvb_decodebin_newpad_cb (
  GstElement *decodebin,
  GstPad     *pad,
  gboolean    last,
  gpointer    data)
{
  GstPad *audiopad;
  GstElement *aconvert;
  GstPadLinkReturn lret;

  aconvert = (GstElement*) data;

  /* only link once */
  audiopad = gst_element_get_static_pad (aconvert, "sink");
  if (GST_PAD_IS_LINKED (audiopad)) {
    g_printerr ("Failed to link decodebin's pad (already linked)\n");
    g_object_unref (audiopad);
    return;
  }

  /* link'n'play */
  lret = gst_pad_link(pad, audiopad);
  if(lret != GST_PAD_LINK_OK) {
    g_printerr ("Faield to link decodebin's pad, retcode %d\n", lret);
    g_object_unref (audiopad);
    return;
  }

  g_object_unref (audiopad);
}

gboolean mdvb_stdin_cb (GIOChannel *channel, GIOCondition condition, gpointer data)
{
  ssize_t len;
  gchar line[LINESIZE];
  struct pipeline *pip;
  int ret;
  GstState state;

  pip = (struct pipeline*) data;
  len = read(0, line, LINESIZE);
  if(len < 0) {
    goto out;
  }
  if(len > 0 && line[len-1] == '\n')
    line[len-1] = 0;
  else
    line[len] = 0;

  if(len == 0) {
    g_printerr("Exiting main loop\n");
    g_main_loop_quit (pip->loop);
    goto out;
  }

  if(3 == sscanf(line, "%i %10s %i", 
      &pip->vpid, pip->vtype, &pip->apid) ) {

    gst_element_set_state (GST_ELEMENT(pip->pipeline), GST_STATE_NULL);

    g_object_set (G_OBJECT (pip->mdemuxsrc), 
      "vpid", pip->vpid, 
      "vtype", pip->vtype, 
      "apid", pip->apid,
      /* FIXME: use user-specified audio type */
      "atype", "mpeg1",
      "vbufsize", 65536, 
      "abufsize", 65536, 
      NULL);

    gst_element_set_state (GST_ELEMENT(pip->pipeline), GST_STATE_PLAYING);
    goto out;
  }

  if (0 == strcmp(line,"list")) {
    g_print("%d %s %d %s\n", pip->vpid, pip->vtype, pip->apid, pip->atype);
    goto out;
  }

  if(0 == name2state(line, &state) ) {
    g_printerr("Switching to state %d\n", state);
    ret = gst_element_set_state (GST_ELEMENT(pip->pipeline), state);
    g_printerr("Switched %d retcode %d\n", state, ret);
    goto out;
  }

  g_printerr(
    "Unsupported command %s. VALID INPUT: \n"
    "* one of: null ready paused playing, followed by \\n\n"
    "* list\\n\n"
    "* VPID VTYPE APID ATYPE\\n\n", 
    line);

out:
  /* continue polling */
  return TRUE;
}

int main (int argc, char *argv[])
{
  int ret = 0;
  GMainLoop *loop;
  struct pipeline pipeline;
  struct pipeline *pip = &pipeline;
  GIOChannel *in;
  int start_now = 0;
  gchar* demuxdev = NULL;
  int outmode = 2;
  int nodstrect = 0;

  do
  {
    GError *err;
    GOptionContext *ctx;
    GOptionEntry options[] = {
      {"outmode", 'o', 0, G_OPTION_ARG_INT, &outmode,
        ("Output video mode (1-12). Default is 2. See `gst-inspect mvdusink' for details"), NULL},
      {"nodstrect", 'R', 0, G_OPTION_ARG_INT, &nodstrect,
        ("Set to 1 to skip passing dstrect=\"1920x1080+0+0\" argument to mvdusink"), NULL},
      {"demuxdev", 'd', 0, G_OPTION_ARG_FILENAME, &demuxdev,
        ("Path to demux device. See `gst-inspect mdemuxsrc' for details"), NULL},
      {NULL}
    };
    if (!g_thread_supported ())
      g_thread_init (NULL);
    ctx = g_option_context_new ("[ADDITIONAL ARGUMENTS]");
    g_option_context_add_main_entries (ctx, options, NULL);
    g_option_context_add_group (ctx, gst_init_get_option_group ());
    if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
      ERR("Error initializing: %s\n", GST_STR_NULL(err->message));
      exit (1);
    }
    g_option_context_free (ctx);
  } while(0);

  /* Initialisation */
  gst_init (&argc, &argv);

  pip->loop = g_main_loop_new (NULL, FALSE);
  CHECKP(pip->loop, GOTO(ret=-1, err));

  /* Create gstreamer elements */
  pip->pipeline = gst_pipeline_new ("mwatch");
  CHECKP(pip->pipeline,GOTO(ret=-1,err));

  pip->mdemuxsrc   = gst_element_factory_make ("mdemuxsrc", "mdemuxsrc");
  CHECKP(pip->mdemuxsrc,GOTO(ret=-1,err));
  pip->msvdhddec   = gst_element_factory_make ("msvdhddec", "msvdhddec");
  CHECKP(pip->msvdhddec,GOTO(ret=-1,err));
  pip->vqueue      = gst_element_factory_make ("queue", "vqueue");
  CHECKP(pip->vqueue,GOTO(ret=-1,err));
  pip->mvdusink    = gst_element_factory_make ("mvdusink", "mvdusink");
  CHECKP(pip->mvdusink,GOTO(ret=-1,err));
  pip->aqueue      = gst_element_factory_make ("queue", "aqueue");
  CHECKP(pip->aqueue,GOTO(ret=-1,err));
  pip->decodebin   = gst_element_factory_make ("decodebin", "decodebin");
  CHECKP(pip->decodebin,GOTO(ret=-1,err));
  pip->aconvert    = gst_element_factory_make ("audioconvert", "audioconvert");
  CHECKP(pip->aconvert,GOTO(ret=-1,err));
  g_signal_connect (pip->decodebin, "new-decoded-pad", 
    G_CALLBACK (mdvb_decodebin_newpad_cb), pip->aconvert);
  pip->alsasink    = gst_element_factory_make ("alsasink", "alsasink");
  CHECKP(pip->alsasink,GOTO(ret=-1,err));

  if(demuxdev != NULL) {
    g_object_set (G_OBJECT (pip->mdemuxsrc), 
      "demuxdev", demuxdev,
      NULL);
  }

  if(argc == 5 || argc == 4) {
    pip->vpid = atoi(argv[1]);
    strncpy(pip->vtype, argv[2], sizeof(pip->vtype)-1);
    pip->apid = atoi(argv[3]);
    /*strncpy(pip->atype, argv[4], sizeof(pip->atype)-1);*/
    strncpy(pip->atype, "?", sizeof(pip->atype)-1);
    g_object_set (G_OBJECT (pip->mdemuxsrc), 
      "vpid", pip->vpid,
      "vtype", pip->vtype, 
      "apid", pip->apid, 
      /* FIXME: use user-specified audio type */
      "atype", "mpeg1",
      "vbufsize", 65536, 
      "abufsize", 65536, 
      NULL);
    start_now = 1;
  }
  else {
    pip->vpid = -1;
    strncpy(pip->vtype, "?", sizeof(pip->vtype)-1);
    pip->apid = -1;
    strncpy(pip->atype, "?", sizeof(pip->atype)-1);
    start_now = 0;
  }

  g_object_set (G_OBJECT (pip->vqueue), 
    "max-size-time", 0ULL, 
    NULL);

  g_object_set (G_OBJECT (pip->aqueue), 
    "max-size-time", 0ULL, 
    NULL);

  if(nodstrect == 1) {
    g_object_set (G_OBJECT (pip->mvdusink), 
      "outmode", outmode, 
      "sync", 1, 
      NULL);
  }
  else {
    g_object_set (G_OBJECT (pip->mvdusink), 
      "outmode", outmode, 
      "dstrect", "1920x1080+0+0", 
      "sync", 1, 
      NULL);
  }

  g_object_set (G_OBJECT (pip->msvdhddec), 
    "frameskip", 2, 
    NULL);

  g_object_set (G_OBJECT (pip->alsasink), 
    "drift-tolerance", 80000ULL,
    "sync", 1, 
    NULL);

  /* We add all elements into the pipeline */
  gst_bin_add_many (GST_BIN (pip->pipeline),
    pip->mdemuxsrc,
    pip->msvdhddec,
    pip->vqueue,
    pip->mvdusink,
    pip->aqueue,
    pip->decodebin,
    pip->aconvert,
    pip->alsasink,
    NULL);

  /* Link the elements together */
  if(! gst_element_link_pads (pip->mdemuxsrc, "vsrc", pip->msvdhddec, NULL)) {
    g_printerr ("Unable to connect mdemuxsrc and msvdhddec\n");
    GOTO(ret=-1,err);
  }
  if(! gst_element_link_many (pip->msvdhddec, pip->vqueue, pip->mvdusink, NULL)) {
    g_printerr ("Unable to link video-branch\n");
    GOTO(ret=-1,err);
  }
  if(! gst_element_link_pads (pip->mdemuxsrc, "asrc", pip->decodebin, NULL)) {
    g_printerr ("Unable to connect mdemuxsrc and decodebin\n");
    GOTO(ret=-1,err);
  }
  if(! gst_element_link_many (pip->aconvert, pip->aqueue, pip->alsasink, NULL)) {
    g_printerr ("Unable to link audio-branch\n");
    GOTO(ret=-1,err);
  }

  /* we add a message handler */
  pip->bus = gst_pipeline_get_bus (GST_PIPELINE (pip->pipeline));
  CHECKP(pip->bus, GOTO(ret=-2,err));

  gst_bus_add_watch (pip->bus, bus_call, pip->loop);

  g_printerr ("Registering stdin channel\n");
  in = g_io_channel_unix_new(0);
  CHECKP(in, GOTO(ret=-2,err));
  g_io_add_watch(in, G_IO_IN|G_IO_PRI, mdvb_stdin_cb, pip);

  if(start_now) {
    /* Set the pipeline to "playing" state */
    g_printerr ("Setting pipeline's state to PLAYING\n");
    gst_element_set_state (pip->pipeline, GST_STATE_PLAYING);
  }
  else {
    g_printerr ("Setting pipeline's state to NULL (waiting for command)\n");
    gst_element_set_state (pip->pipeline, GST_STATE_NULL);
  }

  /* Iterate */
  g_printerr ("Running loop...\n");
  g_main_loop_run (pip->loop);

  /* Out of the main loop, clean up nicely */
  g_printerr ("Returned, stopping playback\n");
  gst_element_set_state (pip->pipeline, GST_STATE_NULL);

err:
  g_printerr ("Deleting pipeline\n");
  gst_object_unref (GST_OBJECT (pip->pipeline));
  return ret;

usage:
  usage(argv[0]);
}

