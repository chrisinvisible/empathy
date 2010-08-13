/*
 * empathy-tls-dialog.c - Source for EmpathyTLSDialog
 * Copyright (C) 2010 Collabora Ltd.
 * @author Cosimo Cecchi <cosimo.cecchi@collabora.co.uk>
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

#include <config.h>

#include "empathy-tls-dialog.h"

#include <glib/gi18n-lib.h>
#include <gcr/gcr.h>
#include <telepathy-glib/util.h>

#include "gcr-simple-certificate.h"

#define DEBUG_FLAG EMPATHY_DEBUG_TLS
#include <libempathy/empathy-debug.h>
#include <libempathy/empathy-utils.h>

G_DEFINE_TYPE (EmpathyTLSDialog, empathy_tls_dialog,
    GTK_TYPE_MESSAGE_DIALOG)

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyTLSDialog);

enum {
  PROP_TLS_CERTIFICATE = 1,
  PROP_REASON,

  LAST_PROPERTY,
};

typedef struct {
  EmpathyTLSCertificate *certificate;
  EmpTLSCertificateRejectReason reason;

  gboolean dispose_run;
} EmpathyTLSDialogPriv;

static void
empathy_tls_dialog_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyTLSDialogPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_TLS_CERTIFICATE:
      g_value_set_object (value, priv->certificate);
      break;
    case PROP_REASON:
      g_value_set_uint (value, priv->reason);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
empathy_tls_dialog_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyTLSDialogPriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_TLS_CERTIFICATE:
      priv->certificate = g_value_dup_object (value);
      break;
    case PROP_REASON:
      priv->reason = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
empathy_tls_dialog_dispose (GObject *object)
{
  EmpathyTLSDialogPriv *priv = GET_PRIV (object);

  if (priv->dispose_run)
    return;

  priv->dispose_run = TRUE;

  tp_clear_object (&priv->certificate);

  G_OBJECT_CLASS (empathy_tls_dialog_parent_class)->dispose (object);
}

static gchar *
reason_to_string (EmpTLSCertificateRejectReason reason)
{
  GString *str;
  const gchar *reason_str;

  str = g_string_new (NULL);

  g_string_append (str, _("The identity provided by the chat server cannot be "
          "verified.\n"));

  switch (reason)
    {
    case EMP_TLS_CERTIFICATE_REJECT_REASON_UNTRUSTED:
      reason_str = _("The certrificate is not signed by a Certification "
          "Authority");
      break;
    case EMP_TLS_CERTIFICATE_REJECT_REASON_EXPIRED:
      reason_str = _("The certificate is expired");
      break;
    case EMP_TLS_CERTIFICATE_REJECT_REASON_NOT_ACTIVATED:
      reason_str = _("The certificate hasn't yet been activated");
      break;
    case EMP_TLS_CERTIFICATE_REJECT_REASON_FINGERPRINT_MISMATCH:
      reason_str = _("The certificate does not have the expected fingerprint");
      break;
    case EMP_TLS_CERTIFICATE_REJECT_REASON_HOSTNAME_MISMATCH:
      reason_str = _("The hostname verified by the certificate doesn't match "
          "the server name");
      break;
    case EMP_TLS_CERTIFICATE_REJECT_REASON_SELF_SIGNED:
      reason_str = _("The certificate is self-signed");
      break;
    case EMP_TLS_CERTIFICATE_REJECT_REASON_REVOKED:
      reason_str = _("The certificate has been revoked by the issuing "
          "Certification Authority");
      break;
    case EMP_TLS_CERTIFICATE_REJECT_REASON_INSECURE:
      reason_str = _("The certificate is cryptographically weak");
      break;
    case EMP_TLS_CERTIFICATE_REJECT_REASON_LIMIT_EXCEEDED:
      reason_str = _("The certificate length exceeds verifiable limits");
      break;
    case EMP_TLS_CERTIFICATE_REJECT_REASON_UNKNOWN:
    default:
      reason_str = _("The certificate is malformed");
      break;
    }

  g_string_append (str, reason_str);

  return g_string_free (str, FALSE);
}

static GtkWidget *
build_gcr_widget (EmpathyTLSDialog *self)
{
  GcrCertificateBasicsWidget *widget;
  GcrCertificate *certificate;
  GPtrArray *cert_chain = NULL;
  GArray *first_cert;
  EmpathyTLSDialogPriv *priv = GET_PRIV (self);

  g_object_get (priv->certificate,
      "cert-data", &cert_chain,
      NULL);
  first_cert = g_ptr_array_index (cert_chain, 0);

  certificate = gcr_simple_certificate_new ((const guchar *) first_cert->data,
      first_cert->len);
  widget = gcr_certificate_basics_widget_new (certificate);

  g_object_unref (certificate);
  g_ptr_array_unref (cert_chain);

  return GTK_WIDGET (widget);
}

static void
empathy_tls_dialog_constructed (GObject *object)
{
  GtkWidget *content_area, *expander, *details;
  gchar *text;
  EmpathyTLSDialog *self = EMPATHY_TLS_DIALOG (object);
  GtkMessageDialog *message_dialog = GTK_MESSAGE_DIALOG (self);
  GtkDialog *dialog = GTK_DIALOG (self);
  EmpathyTLSDialogPriv *priv = GET_PRIV (self);

  gtk_dialog_add_buttons (dialog,
      GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
      _("Continue"), GTK_RESPONSE_YES,
      NULL);

  text = reason_to_string (priv->reason);

  g_object_set (message_dialog,
      "text", _("This connection is untrusted, would you like to "
          "continue anyway?"),
      "secondary-text", text,
      NULL);

  g_free (text);

  content_area = gtk_dialog_get_content_area (dialog);
  text = g_strdup_printf ("<b>%s</b>", _("Certificate Details"));
  expander = gtk_expander_new (text);
  gtk_expander_set_use_markup (GTK_EXPANDER (expander), TRUE);
  gtk_box_pack_end (GTK_BOX (content_area), expander, TRUE, TRUE, 6);
  gtk_widget_show (expander);

  g_free (text);

  details = build_gcr_widget (self);
  gtk_container_add (GTK_CONTAINER (expander), details);
  gtk_widget_show (details);
}

static void
empathy_tls_dialog_init (EmpathyTLSDialog *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_TLS_DIALOG, EmpathyTLSDialogPriv);
}

static void
empathy_tls_dialog_class_init (EmpathyTLSDialogClass *klass)
{
  GParamSpec *pspec;
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (EmpathyTLSDialogPriv));

  oclass->set_property = empathy_tls_dialog_set_property;
  oclass->get_property = empathy_tls_dialog_get_property;
  oclass->dispose = empathy_tls_dialog_dispose;
  oclass->constructed = empathy_tls_dialog_constructed;

  pspec = g_param_spec_object ("certificate", "The EmpathyTLSCertificate",
      "The EmpathyTLSCertificate to be displayed.",
      EMPATHY_TYPE_TLS_CERTIFICATE,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_TLS_CERTIFICATE, pspec);

  pspec = g_param_spec_uint ("reason", "The reason",
      "The reason why the certificate is being asked for confirmation.",
      0, NUM_EMP_TLS_CERTIFICATE_REJECT_REASONS - 1,
      EMP_TLS_CERTIFICATE_REJECT_REASON_UNKNOWN,
      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_REASON, pspec);
}

GtkWidget *
empathy_tls_dialog_new (EmpathyTLSCertificate *certificate,
    EmpTLSCertificateRejectReason reason)
{
  g_assert (EMPATHY_IS_TLS_CERTIFICATE (certificate));

  return g_object_new (EMPATHY_TYPE_TLS_DIALOG,
      "message-type", GTK_MESSAGE_WARNING,
      "certificate", certificate,
      "reason", reason,
      NULL);
}
