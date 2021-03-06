/*
 * libwebsockets - small server side websockets and web server implementation
 *
 * Copyright (C) 2010-2017 Andy Green <andy@warmcat.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation:
 *  version 2.1 of the License.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA  02110-1301  USA
 */

#include "private-libwebsockets.h"

int lws_alloc_vfs_file(struct lws_context *context, const char *filename, uint8_t **buf,
		lws_filepos_t *amount)
{
	lws_filepos_t len;
	lws_fop_flags_t	flags = LWS_O_RDONLY;
	lws_fop_fd_t fops_fd = lws_vfs_file_open(
				lws_get_fops(context), filename, &flags);
	int ret = 1;

	if (!fops_fd)
		return 1;

	len = lws_vfs_get_length(fops_fd);

	*buf = lws_malloc((size_t)len, "lws_alloc_vfs_file");
	if (!*buf)
		goto bail;

	if (lws_vfs_file_read(fops_fd, amount, *buf, len))
		goto bail;

	ret = 0;
bail:
	lws_vfs_file_close(&fops_fd);

	return ret;
}

int
lws_ssl_anybody_has_buffered_read_tsi(struct lws_context *context, int tsi)
{
	struct lws_context_per_thread *pt = &context->pt[tsi];
	struct lws *wsi, *wsi_next;

	wsi = pt->pending_read_list;
	while (wsi) {
		wsi_next = wsi->pending_read_list_next;
		pt->fds[wsi->position_in_fds_table].revents |=
			pt->fds[wsi->position_in_fds_table].events & LWS_POLLIN;
		if (pt->fds[wsi->position_in_fds_table].revents & LWS_POLLIN)
			return 1;

		wsi = wsi_next;
	}

	return 0;
}

LWS_VISIBLE void
lws_ssl_remove_wsi_from_buffered_list(struct lws *wsi)
{
	struct lws_context *context = wsi->context;
	struct lws_context_per_thread *pt = &context->pt[(int)wsi->tsi];

	if (!wsi->pending_read_list_prev &&
	    !wsi->pending_read_list_next &&
	    pt->pending_read_list != wsi)
		/* we are not on the list */
		return;

	/* point previous guy's next to our next */
	if (!wsi->pending_read_list_prev)
		pt->pending_read_list = wsi->pending_read_list_next;
	else
		wsi->pending_read_list_prev->pending_read_list_next =
			wsi->pending_read_list_next;

	/* point next guy's previous to our previous */
	if (wsi->pending_read_list_next)
		wsi->pending_read_list_next->pending_read_list_prev =
			wsi->pending_read_list_prev;

	wsi->pending_read_list_prev = NULL;
	wsi->pending_read_list_next = NULL;
}


int
lws_gate_accepts(struct lws_context *context, int on)
{
	struct lws_vhost *v = context->vhost_list;

	lwsl_info("gating accepts %d\n", on);
	context->ssl_gate_accepts = !on;
#if defined(LWS_WITH_STATS)
	context->updated = 1;
#endif

	while (v) {
		if (v->use_ssl &&  v->lserv_wsi) /* gate ability to accept incoming connections */
			if (lws_change_pollfd(v->lserv_wsi, (LWS_POLLIN) * !on,
					      (LWS_POLLIN) * on))
				lwsl_info("Unable to set accept POLLIN %d\n", on);

		v = v->vhost_next;
	}

	return 0;
}
