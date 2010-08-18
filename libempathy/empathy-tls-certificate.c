/*
 * empathy-tls-certificate.c - Source for EmpathyTLSCertificate
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

#include "empathy-tls-certificate.h"

#include <errno.h>

#include <glib/gstdio.h>

#include <gnutls/gnutls.h>
#include <gnutls/x509.h>

#include <telepathy-glib/proxy-subclass.h>

#define DEBUG_FLAG EMPATHY_DEBUG_TLS
#include "empathy-debug.h"
#include "empathy-utils.h"

#include "extensions/extensions.h"

static void async_initable_iface_init (GAsyncInitableIface *iface);

enum {
  PROP_OBJECT_PATH = 1,
  PROP_BUS_NAME,

  /* proxy properties */
  PROP_CERT_TYPE,
  PROP_CERT_DATA,
  PROP_STATE,
  LAST_PROPERTY,
};

typedef struct {
  gchar *object_path;
  gchar *bus_name;

  TpProxy *proxy;

  GSimpleAsyncResult *async_init_res;

  /* TLSCertificate properties */
  gchar *cert_type;
  GPtrArray *cert_data;
  EmpTLSCertificateState state;
} EmpathyTLSCertificatePriv;

G_DEFINE_TYPE_WITH_CODE (EmpathyTLSCertificate, empathy_tls_certificate,
    G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE, async_initable_iface_init));

#define GET_PRIV(obj) EMPATHY_GET_PRIV (obj, EmpathyTLSCertificate);

static gboolean
tls_certificate_init_finish (GAsyncInitable *initable,
    GAsyncResult *res,
    GError **error)
{
  gboolean retval = TRUE;
  EmpathyTLSCertificate *self = EMPATHY_TLS_CERTIFICATE (initable);
  EmpathyTLSCertificatePriv *priv = GET_PRIV (self);

  if (g_simple_async_result_propagate_error (priv->async_init_res, error))
    retval = FALSE;

  return retval;
}

static GType
array_of_ay_get_type (void)
{
  static GType t = 0;

  if (G_UNLIKELY (t == 0))
    {
      t = dbus_g_type_get_collection ("GPtrArray",
          dbus_g_type_get_collection ("GArray",
              G_TYPE_UCHAR));
    }

  return t;
}

static void
tls_certificate_got_all_cb (TpProxy *proxy,
    GHashTable *properties,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GPtrArray *cert_data;
  EmpathyTLSCertificate *self = EMPATHY_TLS_CERTIFICATE (weak_object);
  EmpathyTLSCertificatePriv *priv = GET_PRIV (self);

  if (error != NULL)
    {
      g_simple_async_result_set_from_error (priv->async_init_res, error);
      g_simple_async_result_complete_in_idle (priv->async_init_res);

      g_object_unref (priv->async_init_res);

      return;
    }

  priv->cert_type = g_strdup (tp_asv_get_string (properties,
          "CertificateType"));
  priv->state = tp_asv_get_uint32 (properties, "State", NULL);

  cert_data = tp_asv_get_boxed (properties, "CertificateChainData",
      array_of_ay_get_type ());
  g_assert (cert_data != NULL);
  priv->cert_data = g_boxed_copy (array_of_ay_get_type (), cert_data);

  DEBUG ("Got a certificate chain long %u, of type %s",
      priv->cert_data->len, priv->cert_type);

  g_simple_async_result_complete_in_idle (priv->async_init_res);
  g_object_unref (priv->async_init_res);
}

static void
tls_certificate_init_async (GAsyncInitable *initable,
    gint io_priority,
    GCancellable *cancellable,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  TpDBusDaemon *dbus;
  GError *error = NULL;
  EmpathyTLSCertificate *self = EMPATHY_TLS_CERTIFICATE (initable);
  EmpathyTLSCertificatePriv *priv = GET_PRIV (self);

  g_assert (priv->object_path != NULL);
  g_assert (priv->bus_name != NULL);

  priv->async_init_res = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, empathy_tls_certificate_new_async);
  dbus = tp_dbus_daemon_dup (&error);

  if (error != NULL)
    {
      g_simple_async_result_set_from_error (priv->async_init_res, error);
      g_simple_async_result_complete_in_idle (priv->async_init_res);

      g_error_free (error);
      g_object_unref (priv->async_init_res);
      return;
    }

  DEBUG ("Creating a proxy for object at path %s, owned by %s",
      priv->object_path, priv->bus_name);

  priv->proxy = g_object_new (TP_TYPE_PROXY,
      "object-path", priv->object_path,
      "bus-name", priv->bus_name,
      "dbus-daemon", dbus, NULL);

  tp_proxy_add_interface_by_id (priv->proxy,
      EMP_IFACE_QUARK_AUTHENTICATION_TLS_CERTIFICATE);

  /* call GetAll() on the certificate */
  tp_cli_dbus_properties_call_get_all (priv->proxy,
      -1, EMP_IFACE_AUTHENTICATION_TLS_CERTIFICATE,
      tls_certificate_got_all_cb, NULL, NULL, G_OBJECT (self));

  g_object_unref (dbus);
}

static void
async_initable_iface_init (GAsyncInitableIface *iface)
{
  iface->init_async = tls_certificate_init_async;
  iface->init_finish = tls_certificate_init_finish;
}

static void
empathy_tls_certificate_finalize (GObject *object)
{
  EmpathyTLSCertificatePriv *priv = GET_PRIV (object);

  DEBUG ("%p", object);

  g_free (priv->object_path);

  G_OBJECT_CLASS (empathy_tls_certificate_parent_class)->finalize (object);
}

static void
empathy_tls_certificate_get_property (GObject *object,
    guint property_id,
    GValue *value,
    GParamSpec *pspec)
{
  EmpathyTLSCertificatePriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_OBJECT_PATH:
      g_value_set_string (value, priv->object_path);
      break;
    case PROP_BUS_NAME:
      g_value_set_string (value, priv->bus_name);
      break;
    case PROP_CERT_TYPE:
      g_value_set_string (value, priv->cert_type);
      break;
    case PROP_CERT_DATA:
      g_value_set_boxed (value, priv->cert_data);
      break;
    case PROP_STATE:
      g_value_set_uint (value, priv->state);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
empathy_tls_certificate_set_property (GObject *object,
    guint property_id,
    const GValue *value,
    GParamSpec *pspec)
{
  EmpathyTLSCertificatePriv *priv = GET_PRIV (object);

  switch (property_id)
    {
    case PROP_OBJECT_PATH:
      priv->object_path = g_value_dup_string (value);
      break;
    case PROP_BUS_NAME:
      priv->bus_name = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
    }
}

static void
empathy_tls_certificate_init (EmpathyTLSCertificate *self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      EMPATHY_TYPE_TLS_CERTIFICATE, EmpathyTLSCertificatePriv);
}

static void
empathy_tls_certificate_class_init (EmpathyTLSCertificateClass *klass)
{
  GParamSpec *pspec;
  GObjectClass *oclass = G_OBJECT_CLASS (klass);

  oclass->get_property = empathy_tls_certificate_get_property;
  oclass->set_property = empathy_tls_certificate_set_property;
  oclass->finalize = empathy_tls_certificate_finalize;

  g_type_class_add_private (klass, sizeof (EmpathyTLSCertificatePriv));

  pspec = g_param_spec_string ("object-path", "The object path",
      "The path on the bus where the object we proxy is living.",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_OBJECT_PATH, pspec);

  pspec = g_param_spec_string ("bus-name", "The bus name",
      "The bus name owning this certificate.",
      NULL,
      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_BUS_NAME, pspec);

  pspec = g_param_spec_string ("cert-type", "Certificate type",
      "The type of this certificate.",
      NULL,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_CERT_TYPE, pspec);

  pspec = g_param_spec_boxed ("cert-data", "Certificate chain data",
      "The raw DER-encoded certificate chain data.",
      array_of_ay_get_type (),
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_CERT_DATA, pspec);

  pspec = g_param_spec_uint ("state", "State",
      "The state of this certificate.",
      EMP_TLS_CERTIFICATE_STATE_PENDING, NUM_EMP_TLS_CERTIFICATE_STATES -1,
      EMP_TLS_CERTIFICATE_STATE_PENDING,
      G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);
  g_object_class_install_property (oclass, PROP_STATE, pspec);
}

static void
cert_proxy_accept_cb (TpProxy *proxy,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSimpleAsyncResult *accept_result = user_data;

  DEBUG ("Callback for accept(), error %p", error);

  if (error != NULL)
    {
      DEBUG ("Error was %s", error->message);
      g_simple_async_result_set_from_error (accept_result, error);
    }

  g_simple_async_result_complete (accept_result);
}

static void
cert_proxy_reject_cb (TpProxy *proxy,
    const GError *error,
    gpointer user_data,
    GObject *weak_object)
{
  GSimpleAsyncResult *reject_result = user_data;

  DEBUG ("Callback for reject(), error %p", error);

  if (error != NULL)
    {
      DEBUG ("Error was %s", error->message);
      g_simple_async_result_set_from_error (reject_result, error);
    }

  g_simple_async_result_complete (reject_result);
}

static const gchar *
reject_reason_get_dbus_error (EmpTLSCertificateRejectReason reason)
{
  const gchar *retval = NULL;

  switch (reason)
    {
    case EMP_TLS_CERTIFICATE_REJECT_REASON_UNTRUSTED:
      retval = tp_error_get_dbus_name (TP_ERROR_CERT_UNTRUSTED);
      break;
    case EMP_TLS_CERTIFICATE_REJECT_REASON_EXPIRED:
      retval = tp_error_get_dbus_name (TP_ERROR_CERT_EXPIRED);
      break;
    case EMP_TLS_CERTIFICATE_REJECT_REASON_NOT_ACTIVATED:
      retval = tp_error_get_dbus_name (TP_ERROR_CERT_NOT_ACTIVATED);
      break;
    case EMP_TLS_CERTIFICATE_REJECT_REASON_FINGERPRINT_MISMATCH:
      retval = tp_error_get_dbus_name (TP_ERROR_CERT_FINGERPRINT_MISMATCH);
      break;
    case EMP_TLS_CERTIFICATE_REJECT_REASON_HOSTNAME_MISMATCH:
      retval = tp_error_get_dbus_name (TP_ERROR_CERT_HOSTNAME_MISMATCH);
      break;
    case EMP_TLS_CERTIFICATE_REJECT_REASON_SELF_SIGNED:
      retval = tp_error_get_dbus_name (TP_ERROR_CERT_SELF_SIGNED);
      break;
    case EMP_TLS_CERTIFICATE_REJECT_REASON_REVOKED:
      retval = tp_error_get_dbus_name (TP_ERROR_CERT_REVOKED);
      break;
    case EMP_TLS_CERTIFICATE_REJECT_REASON_INSECURE:
      retval = tp_error_get_dbus_name (TP_ERROR_CERT_INSECURE);
      break;
    case EMP_TLS_CERTIFICATE_REJECT_REASON_LIMIT_EXCEEDED:
      retval = tp_error_get_dbus_name (TP_ERROR_CERT_LIMIT_EXCEEDED);
      break;
    case EMP_TLS_CERTIFICATE_REJECT_REASON_UNKNOWN:
    default:
      retval = tp_error_get_dbus_name (TP_ERROR_CERT_INVALID);
      break;
    }

  return retval;
}

void
empathy_tls_certificate_new_async (const gchar *bus_name,
    const gchar *object_path,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  g_assert (object_path != NULL);

  g_async_initable_new_async (EMPATHY_TYPE_TLS_CERTIFICATE,
      G_PRIORITY_DEFAULT, NULL, callback, user_data,
      "bus-name", bus_name,
      "object-path", object_path, NULL);
}

EmpathyTLSCertificate *
empathy_tls_certificate_new_finish (GAsyncResult *res,
    GError **error)
{
  GObject *object, *source_object;

  source_object = g_async_result_get_source_object (res);

  object = g_async_initable_new_finish (G_ASYNC_INITABLE (source_object),
      res, error);
  g_object_unref (source_object);

  if (object != NULL)
    return EMPATHY_TLS_CERTIFICATE (object);
  else
    return NULL;
}

void
empathy_tls_certificate_accept_async (EmpathyTLSCertificate *self,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  GSimpleAsyncResult *accept_result;
  EmpathyTLSCertificatePriv *priv = GET_PRIV (self);

  g_assert (EMPATHY_IS_TLS_CERTIFICATE (self));

  DEBUG ("Accepting TLS certificate");

  accept_result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, empathy_tls_certificate_accept_async);

  emp_cli_authentication_tls_certificate_call_accept (priv->proxy,
      -1, cert_proxy_accept_cb,
      accept_result, g_object_unref,
      G_OBJECT (self));
}

gboolean
empathy_tls_certificate_accept_finish (EmpathyTLSCertificate *self,
    GAsyncResult *result,
    GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
          error))
    return FALSE;

  return TRUE;
}

void
empathy_tls_certificate_reject_async (EmpathyTLSCertificate *self,
    EmpTLSCertificateRejectReason reason,
    GHashTable *details,
    GAsyncReadyCallback callback,
    gpointer user_data)
{
  const gchar *dbus_error;
  GSimpleAsyncResult *reject_result;
  EmpathyTLSCertificatePriv *priv = GET_PRIV (self);

  g_assert (EMPATHY_IS_TLS_CERTIFICATE (self));

  DEBUG ("Rejecting TLS certificate with reason %u", reason);

  dbus_error = reject_reason_get_dbus_error (reason);
  reject_result = g_simple_async_result_new (G_OBJECT (self),
      callback, user_data, empathy_tls_certificate_reject_async);

  emp_cli_authentication_tls_certificate_call_reject (priv->proxy,
      -1, reason, dbus_error, details, cert_proxy_reject_cb,
      reject_result, g_object_unref, G_OBJECT (self));
}

gboolean
empathy_tls_certificate_reject_finish (EmpathyTLSCertificate *self,
    GAsyncResult *result,
    GError **error)
{
  if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result),
          error))
    return FALSE;

  return TRUE;
}

static gsize
get_exported_size (gnutls_x509_crt_t cert)
{
  gsize retval;
  guchar fake;

  /* fake an export so we get the size to allocate */
  gnutls_x509_crt_export (cert, GNUTLS_X509_FMT_PEM,
      &fake, &retval);

  DEBUG ("Should allocate %lu bytes", (gulong) retval);

  return retval;
}

void
empathy_tls_certificate_store_ca (EmpathyTLSCertificate *self)
{
  GArray *last_cert;
  gnutls_x509_crt_t cert;
  gnutls_datum_t datum = { NULL, 0 };
  gsize exported_len;
  guchar *exported_cert = NULL;
  gint res;
  gchar *user_certs_dir = NULL, *filename = NULL, *path = NULL;
  GError *error = NULL;
  EmpathyTLSCertificatePriv *priv = GET_PRIV (self);

  last_cert = g_ptr_array_index (priv->cert_data, priv->cert_data->len - 1);
  datum.data = (guchar *) last_cert->data;
  datum.size = last_cert->len;

  gnutls_x509_crt_init (&cert);
  gnutls_x509_crt_import (cert, &datum, GNUTLS_X509_FMT_DER);

  /* make sure it's self-signed, otherwise it's not a CA */
  if (gnutls_x509_crt_check_issuer (cert, cert) <= 0)
    {
      DEBUG ("Can't import the CA, as it's not self-signed");
      gnutls_x509_crt_deinit (cert);

      return;
    }

  if (gnutls_x509_crt_get_ca_status (cert, NULL) <= 0)
    {
      DEBUG ("Can't import the CA, it's not a valid CA certificate");
      gnutls_x509_crt_deinit (cert);

      goto out;
    }

  exported_len = get_exported_size (cert);
  exported_cert = g_malloc (sizeof (guchar) * exported_len);

  res = gnutls_x509_crt_export (cert, GNUTLS_X509_FMT_PEM,
      exported_cert, &exported_len);

  if (res < 0)
    {
      DEBUG ("Failed to export the CA certificate; GnuTLS returned %d", res);
      gnutls_x509_crt_deinit (cert);

      goto out;
    }

  gnutls_x509_crt_deinit (cert);

  /* write the file */
  user_certs_dir = g_build_filename (g_get_user_config_dir (),
      "telepathy", "certs", NULL);

  res = g_mkdir_with_parents (user_certs_dir, S_IRWXU | S_IRWXG);

  if (res < 0)
    {
      DEBUG ("Failed to create the user certificate directory: %s",
          g_strerror (errno));

      goto out;
    }

  do
    {
      g_free (path);

      filename = g_strdup_printf ("cert-%p", cert);
      path = g_build_filename (user_certs_dir, filename, NULL);

      g_free (filename);
    }
  while (g_file_test (path, G_FILE_TEST_EXISTS));

  DEBUG ("Will save to %s", path);

  g_file_set_contents (path, (const gchar *) exported_cert, exported_len,
      &error);

  if (error != NULL)
    {
      DEBUG ("Can't save the CA certificate to %s: %s",
          path, error->message);

      g_error_free (error);
    }

 out:
  g_free (path);
  g_free (exported_cert);
  g_free (user_certs_dir);
}
