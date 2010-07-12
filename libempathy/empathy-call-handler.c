/*
 * empathy-call-handler.c - Source for EmpathyCallHandler
 * Copyright (C) 2008-2009 Collabora Ltd.
 * @author Sjoerd Simons <sjoerd.simons@collabora.co.uk>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */


#include <stdio.h>
#include <stdlib.h>

#include <telepathy-glib/util.h>
#include <telepathy-glib/interfaces.h>

#include <telepathy-farsight/channel.h>
#include <telepathy-farsight/stream.h>

#include "empathy-call-handler.h"
#include "empathy-call-factory.h"
#include "empathy-dispatcher.h"
#include "empathy-marshal.h"
#include "empathy-utils.h"

G_DEFINE_TYPE(EmpathyCallHandler, empathy_call_handler, G_TYPE_OBJECT)

/* signal enum */
enum {
  CONFERENCE_ADDED,
  SRC_PAD_ADDED,
  SINK_PAD_ADDED,
  REQUEST_RESOURCE,
  CLOSED,
  STREAM_CLOSED,
  LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0};

enum {
  PROP_TP_CALL = 1,
  PROP_GST_BUS,
  PROP_CONTACT,
  PROP_INITIAL_AUDIO,
  PROP_INITIAL_VIDEO,
  PROP_SEND_AUDIO_CODEC,
  PROP_SEND_VIDEO_CODEC
};

/* private structure */

typedef struct {
  gboolean dispose_has_run;
  EmpathyTpCall *call;
  EmpathyContact *contact;
  TfChannel *tfchannel;
  gboolean initial_audio;
  gboolean initial_video;

  FsCodec *send_audio_codec;
  FsCodec *send_video_codec;
} EmpathyCallHandlerPriv;

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyCallHandler)

static void
empathy_call_handler_dispose (GObject *object)
{
  EmpathyCallHandlerPriv *priv = GET_PRIV (object);

  if (priv->dispose_has_run)
    return;

  priv->dispose_has_run = TRUE;

  if (priv->contact != NULL)
    g_object_unref (priv->contact);

  priv->contact = NULL;

  if (priv->tfchannel != NULL)
    g_object_unref (priv->tfchannel);

  priv->tfchannel = NULL;

  if (priv->call != NULL)
    {
      empathy_tp_call_close (priv->call);
      g_object_unref (priv->call);
    }

  priv->call = NULL;

  /* release any references held by the object here */
  if (G_OBJECT_CLASS (empathy_call_handler_parent_class)->dispose)
    G_OBJECT_CLASS (empathy_call_handler_parent_class)->dispose (object);
}

static void
empathy_call_handler_finalize (GObject *object)
{
  EmpathyCallHandlerPriv *priv = GET_PRIV (object);

  fs_codec_destroy (priv->send_audio_codec);
  fs_codec_destroy (priv->send_video_codec);

  if (G_OBJECT_CLASS (empathy_call_handler_parent_class)->finalize)
    G_OBJECT_CLASS (empathy_call_handler_parent_class)->finalize (object);
}

static void
empathy_call_handler_init (EmpathyCallHandler *obj)
{
  EmpathyCallHandlerPriv *priv = G_TYPE_INSTANCE_GET_PRIVATE (obj,
    EMPATHY_TYPE_CALL_HANDLER, EmpathyCallHandlerPriv);

  obj->priv = priv;
}

static void
empathy_call_handler_constructed (GObject *object)
{
  EmpathyCallHandlerPriv *priv = GET_PRIV (object);

  if (priv->contact == NULL)
    {
      g_object_get (priv->call, "contact", &(priv->contact), NULL);
    }
}

static void
empathy_call_handler_set_property (GObject *object,
  guint property_id, const GValue *value, GParamSpec *pspec)
{
  EmpathyCallHandlerPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
      case PROP_CONTACT:
        priv->contact = g_value_dup_object (value);
        break;
      case PROP_TP_CALL:
        priv->call = g_value_dup_object (value);
        break;
      case PROP_INITIAL_AUDIO:
        priv->initial_audio = g_value_get_boolean (value);
        break;
      case PROP_INITIAL_VIDEO:
        priv->initial_video = g_value_get_boolean (value);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}

static void
empathy_call_handler_get_property (GObject *object,
  guint property_id, GValue *value, GParamSpec *pspec)
{
  EmpathyCallHandlerPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
      case PROP_CONTACT:
        g_value_set_object (value, priv->contact);
        break;
      case PROP_TP_CALL:
        g_value_set_object (value, priv->call);
        break;
      case PROP_INITIAL_AUDIO:
        g_value_set_boolean (value, priv->initial_audio);
        break;
      case PROP_INITIAL_VIDEO:
        g_value_set_boolean (value, priv->initial_video);
        break;
      case PROP_SEND_AUDIO_CODEC:
        g_value_set_boxed (value, priv->send_audio_codec);
        break;
      case PROP_SEND_VIDEO_CODEC:
        g_value_set_boxed (value, priv->send_video_codec);
        break;
      default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
    }
}


static void
empathy_call_handler_class_init (EmpathyCallHandlerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GParamSpec *param_spec;

  g_type_class_add_private (klass, sizeof (EmpathyCallHandlerPriv));

  object_class->constructed = empathy_call_handler_constructed;
  object_class->set_property = empathy_call_handler_set_property;
  object_class->get_property = empathy_call_handler_get_property;
  object_class->dispose = empathy_call_handler_dispose;
  object_class->finalize = empathy_call_handler_finalize;

  param_spec = g_param_spec_object ("contact",
    "contact", "The remote contact",
    EMPATHY_TYPE_CONTACT,
    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_CONTACT, param_spec);

  param_spec = g_param_spec_object ("tp-call",
    "tp-call", "The calls channel wrapper",
    EMPATHY_TYPE_TP_CALL,
    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_TP_CALL, param_spec);

  param_spec = g_param_spec_boolean ("initial-audio",
    "initial-audio", "Whether the call should start with audio",
    TRUE,
    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIAL_AUDIO,
      param_spec);

  param_spec = g_param_spec_boolean ("initial-video",
    "initial-video", "Whether the call should start with video",
    FALSE,
    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_INITIAL_VIDEO,
    param_spec);

  param_spec = g_param_spec_boxed ("send-audio-codec",
    "send audio codec", "Codec used to encode the outgoing video stream",
    FS_TYPE_CODEC,
    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SEND_AUDIO_CODEC,
    param_spec);

  param_spec = g_param_spec_boxed ("send-video-codec",
    "send video codec", "Codec used to encode the outgoing video stream",
    FS_TYPE_CODEC,
    G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (object_class, PROP_SEND_VIDEO_CODEC,
    param_spec);

  signals[CONFERENCE_ADDED] =
    g_signal_new ("conference-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT,
      G_TYPE_NONE,
      1, FS_TYPE_CONFERENCE);

  signals[SRC_PAD_ADDED] =
    g_signal_new ("src-pad-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      _empathy_marshal_BOOLEAN__OBJECT_UINT,
      G_TYPE_BOOLEAN,
      2, GST_TYPE_PAD, G_TYPE_UINT);

  signals[SINK_PAD_ADDED] =
    g_signal_new ("sink-pad-added", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      _empathy_marshal_BOOLEAN__OBJECT_UINT,
      G_TYPE_BOOLEAN,
      2, GST_TYPE_PAD, G_TYPE_UINT);

  signals[REQUEST_RESOURCE] =
    g_signal_new ("request-resource", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0,
      g_signal_accumulator_true_handled, NULL,
      _empathy_marshal_BOOLEAN__UINT_UINT,
      G_TYPE_BOOLEAN, 2, G_TYPE_UINT, G_TYPE_UINT);

  signals[CLOSED] =
    g_signal_new ("closed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__VOID,
      G_TYPE_NONE,
      0);

  signals[STREAM_CLOSED] =
    g_signal_new ("stream-closed", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL,
      g_cclosure_marshal_VOID__OBJECT,
      G_TYPE_NONE, 1, TF_TYPE_STREAM);
}

/**
 * empathy_call_handler_new_for_contact:
 * @contact: an #EmpathyContact
 *
 * Creates a new #EmpathyCallHandler with contact @contact.
 *
 * Return value: a new #EmpathyCallHandler
 */
EmpathyCallHandler *
empathy_call_handler_new_for_contact (EmpathyContact *contact)
{
  return EMPATHY_CALL_HANDLER (g_object_new (EMPATHY_TYPE_CALL_HANDLER,
    "contact", contact, NULL));
}

EmpathyCallHandler *
empathy_call_handler_new_for_channel (EmpathyTpCall *call)
{
  return EMPATHY_CALL_HANDLER (g_object_new (EMPATHY_TYPE_CALL_HANDLER,
    "tp-call", call,
    "initial-video", empathy_tp_call_is_receiving_video (call),
    NULL));
}

static void
update_sending_codec (EmpathyCallHandler *self,
    FsCodec *codec,
    FsSession *session)
{
  EmpathyCallHandlerPriv *priv = GET_PRIV (self);
  FsMediaType type;

  if (codec == NULL || session == NULL)
    return;

  g_object_get (session, "media-type", &type, NULL);

  if (type == FS_MEDIA_TYPE_AUDIO)
    {
      priv->send_audio_codec = fs_codec_copy (codec);
      g_object_notify (G_OBJECT (self), "send-audio-codec");
    }
  else if (type == FS_MEDIA_TYPE_VIDEO)
    {
      priv->send_video_codec = fs_codec_copy (codec);
      g_object_notify (G_OBJECT (self), "send-video-codec");
    }
}

void
empathy_call_handler_bus_message (EmpathyCallHandler *handler,
  GstBus *bus, GstMessage *message)
{
  EmpathyCallHandlerPriv *priv = GET_PRIV (handler);
  const GstStructure *s = gst_message_get_structure (message);

  if (priv->tfchannel == NULL)
    return;

  if (s != NULL &&
      gst_structure_has_name (s, "farsight-send-codec-changed"))
    {
      const GValue *val;
      FsCodec *codec;
      FsSession *session;

      val = gst_structure_get_value (s, "codec");
      codec = g_value_get_boxed (val);

      val = gst_structure_get_value (s, "session");
      session = g_value_get_object (val);

      update_sending_codec (handler, codec, session);
    }

  tf_channel_bus_message (priv->tfchannel, message);
}

static void
empathy_call_handler_tf_channel_session_created_cb (TfChannel *tfchannel,
  FsConference *conference, FsParticipant *participant,
  EmpathyCallHandler *self)
{
  g_signal_emit (G_OBJECT (self), signals[CONFERENCE_ADDED], 0,
    GST_ELEMENT (conference));
}

static gboolean
src_pad_added_error_idle (gpointer data)
{
  TfStream *stream = data;

  tf_stream_error (stream, TP_MEDIA_STREAM_ERROR_MEDIA_ERROR,
      "Could not link sink");
  g_object_unref (stream);

  return FALSE;
}

static void
empathy_call_handler_tf_stream_src_pad_added_cb (TfStream *stream,
  GstPad *pad, FsCodec *codec, EmpathyCallHandler  *handler)
{
  guint media_type;
  gboolean retval;

  g_object_get (stream, "media-type", &media_type, NULL);

  g_signal_emit (G_OBJECT (handler), signals[SRC_PAD_ADDED], 0,
      pad, media_type, &retval);

  if (!retval)
    g_idle_add (src_pad_added_error_idle, g_object_ref (stream));
}


static gboolean
empathy_call_handler_tf_stream_request_resource_cb (TfStream *stream,
  guint direction, EmpathyTpCall *call)
{
  gboolean ret;
  guint media_type;

  g_object_get (G_OBJECT (stream), "media-type", &media_type, NULL);

  g_signal_emit (G_OBJECT (call),
    signals[REQUEST_RESOURCE], 0, media_type, direction, &ret);

  return ret;
}

static void
empathy_call_handler_tf_stream_closed_cb (TfStream *stream,
  EmpathyCallHandler *handler)
{
  g_signal_emit (handler, signals[STREAM_CLOSED], 0, stream);
}

static void
empathy_call_handler_tf_channel_stream_created_cb (TfChannel *tfchannel,
  TfStream *stream, EmpathyCallHandler *handler)
{
  guint media_type;
  GstPad *spad;
  gboolean retval;
  FsSession *session;
  FsCodec *codec;

  g_signal_connect (stream, "src-pad-added",
      G_CALLBACK (empathy_call_handler_tf_stream_src_pad_added_cb), handler);
  g_signal_connect (stream, "request-resource",
      G_CALLBACK (empathy_call_handler_tf_stream_request_resource_cb),
        handler);
  g_signal_connect (stream, "closed",
      G_CALLBACK (empathy_call_handler_tf_stream_closed_cb), handler);

  g_object_get (stream, "media-type", &media_type,
    "sink-pad", &spad, NULL);

  g_signal_emit (G_OBJECT (handler), signals[SINK_PAD_ADDED], 0,
      spad, media_type, &retval);

 if (!retval)
      tf_stream_error (stream, TP_MEDIA_STREAM_ERROR_MEDIA_ERROR,
          "Could not link source");

 g_object_get (stream, "farsight-session", &session, NULL);
 g_object_get (session, "current-send-codec", &codec, NULL);

 update_sending_codec (handler, codec, session);

 tp_clear_object (&session);
 tp_clear_object (&codec);

 gst_object_unref (spad);
}

static void
empathy_call_handler_tf_channel_closed_cb (TfChannel *tfchannel,
  EmpathyCallHandler *handler)
{
  g_signal_emit (G_OBJECT (handler), signals[CLOSED], 0);
}

static GList *
empathy_call_handler_tf_channel_codec_config_cb (TfChannel *channel,
  guint stream_id, FsMediaType media_type, guint direction, gpointer user_data)
{
  gchar *filename = empathy_file_lookup ("codec-preferences", "data");
  GList *codecs;
  GError *error = NULL;

  codecs = fs_codec_list_from_keyfile (filename, &error);
  g_free (filename);

  if (!codecs)
    {
      g_warning ("No codec-preferences file: %s",
          error ? error->message : "No error message");
    }
  g_clear_error (&error);

  return codecs;
}

static void
empathy_call_handler_start_tpfs (EmpathyCallHandler *self)
{
  EmpathyCallHandlerPriv *priv = GET_PRIV (self);
  TpChannel *channel;

  g_object_get (priv->call, "channel", &channel, NULL);

  g_assert (channel != NULL);

  priv->tfchannel = tf_channel_new (channel);

  /* Set up the telepathy farsight channel */
  g_signal_connect (priv->tfchannel, "session-created",
      G_CALLBACK (empathy_call_handler_tf_channel_session_created_cb), self);
  g_signal_connect (priv->tfchannel, "stream-created",
      G_CALLBACK (empathy_call_handler_tf_channel_stream_created_cb), self);
  g_signal_connect (priv->tfchannel, "closed",
      G_CALLBACK (empathy_call_handler_tf_channel_closed_cb), self);
  g_signal_connect (priv->tfchannel, "stream-get-codec-config",
      G_CALLBACK (empathy_call_handler_tf_channel_codec_config_cb), self);

  g_object_unref (channel);
}

static void
empathy_call_handler_request_cb (EmpathyDispatchOperation *operation,
  const GError *error,
  gpointer user_data)
{
  EmpathyCallHandler *self = EMPATHY_CALL_HANDLER (user_data);
  EmpathyCallHandlerPriv *priv = GET_PRIV (self);
  TpChannel *channel;

  if (error != NULL)
    return;

  channel = empathy_dispatch_operation_get_channel (operation);
  g_assert (channel != NULL);

  priv->call = empathy_tp_call_new (channel);

  g_object_notify (G_OBJECT (self), "tp-call");

  empathy_call_handler_start_tpfs (self);

  empathy_dispatch_operation_claim (operation);
}

void
empathy_call_handler_start_call (EmpathyCallHandler *handler,
    gint64 timestamp)
{

  EmpathyCallHandlerPriv *priv = GET_PRIV (handler);

  if (priv->call != NULL)
    {
      empathy_call_handler_start_tpfs (handler);
      empathy_tp_call_accept_incoming_call (priv->call);
      return;
    }

  /* No TpCall object (we are redialing). Request a new media channel that
   * will be used to create a new EmpathyTpCall. */
  g_assert (priv->contact != NULL);

  empathy_call_factory_new_call_with_streams (priv->contact,
      priv->initial_audio, priv->initial_video, timestamp,
      empathy_call_handler_request_cb, handler);
}

/**
 * empathy_call_handler_stop_call:
 * @handler: an #EmpathyCallHandler
 *
 * Closes the #EmpathyCallHandler's call and frees its resources.
 */
void
empathy_call_handler_stop_call (EmpathyCallHandler *handler)
{
  EmpathyCallHandlerPriv *priv = GET_PRIV (handler);

  if (priv->call != NULL)
    {
      empathy_tp_call_leave (priv->call);
      g_object_unref (priv->call);
    }

  priv->call = NULL;
}

/**
 * empathy_call_handler_has_initial_video:
 * @handler: an #EmpathyCallHandler
 *
 * Return %TRUE if the call managed by this #EmpathyCallHandler was
 * created with video enabled
 *
 * Return value: %TRUE if the call was created as a video conversation.
 */
gboolean
empathy_call_handler_has_initial_video (EmpathyCallHandler *handler)
{
  EmpathyCallHandlerPriv *priv = GET_PRIV (handler);

  return priv->initial_video;
}

FsCodec *
empathy_call_handler_get_send_audio_codec (EmpathyCallHandler *self)
{
  EmpathyCallHandlerPriv *priv = GET_PRIV (self);

  return priv->send_audio_codec;
}

FsCodec *
empathy_call_handler_get_send_video_codec (EmpathyCallHandler *self)
{
  EmpathyCallHandlerPriv *priv = GET_PRIV (self);

  return priv->send_video_codec;
}
