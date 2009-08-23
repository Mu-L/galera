/*
 * Copyright (C) 2008 Codership Oy <info@codership.com>
 *
 * $Id$
 *
 * FIFO "class" customized for particular purpose
 * (here I decided to sacrifice generality for efficiency).
 * Implements simple fixed size "mallocless" FIFO.
 * Except gcs_fifo_create() there are two types of fifo
 * access methods - protected and unprotected. Unprotected
 * methods assume that calling routines implement their own
 * protection, and thus are simplified for speed.
 */

//#include <unistd.h>
#include <galerautils.h>

#include "gcs_fifo_lite.h"

/* Creates FIFO object. Since it practically consists of array of (void*),
 * the length can be chosen arbitrarily high - to minimize the risk
 * of overflow situation.
 */
gcs_fifo_lite_t* gcs_fifo_lite_create (size_t length, size_t item_size)
{
    gcs_fifo_lite_t* ret = NULL;
    size_t l = 1;

    /* check limits */
    if (length < 1 || item_size < 1)
        return NULL;

    /* Find real length. It must be power of 2*/
    while (l < length) l = l << 1;
    if (l             > (ulong) GU_LONG_MAX) return NULL;
    if (l * item_size > (ulong) GU_ULONG_MAX) return NULL;

    ret = GU_CALLOC (1, gcs_fifo_lite_t);
    if (ret) {
        ret->item_size = item_size;
        ret->length    = l;
	ret->mask      = ret->length - 1;
	ret->queue     = gu_malloc (ret->length * item_size);
	if (ret->queue) {
	    gu_mutex_init (&ret->lock,     NULL);
	    gu_cond_init  (&ret->put_cond, NULL);
	    gu_cond_init  (&ret->get_cond, NULL);
	    /* everything else must be initialized to 0 by calloc */
	}
	else {
	    gu_free (ret);
	    ret = NULL;
	}
    }

    return ret;
}

long gcs_fifo_lite_close (gcs_fifo_lite_t* fifo)
{
    int ret = gu_mutex_lock (&fifo->lock);

    if (!ret) {
        fifo->closed = true;

        // wake whoever is waiting
        fifo->put_wait = 0;
        gu_cond_broadcast (&fifo->put_cond);
        fifo->get_wait = 0;
        gu_cond_broadcast (&fifo->get_cond);

        gu_mutex_unlock (&fifo->lock);
    }

    return ret;
}

long gcs_fifo_lite_destroy (gcs_fifo_lite_t* f)
{
    int ret;

    if (f) {
	if ((ret = gu_mutex_lock (&f->lock))) {
	    return -ret; /* something's wrong */
	}
	if (f->destroyed) {
	    gu_mutex_unlock (&f->lock);
	    return -EALREADY;
	}

        f->closed    = true;
	f->destroyed = true;

	/* get rid of "put" threads waiting for lock or signal */
	while (pthread_cond_destroy (&f->put_cond)) {
            if (f->put_wait <= 0) {
                gu_fatal ("Can't destroy condition while nobody's waiting");
                abort();
            }
            f->put_wait = 0;
            gu_cond_broadcast (&f->put_cond);
	}

	while (f->used) {
	    /* there are some items in FIFO - and that means
	     * no gcs_fifo_lite_safe_get() is waiting on condition */
	    gu_mutex_unlock (&f->lock);
	    /* let them get remaining items from FIFO,
	     * we don't know how to deallocate them ourselves.
	     * unfortunately this may take some time */
	    usleep (10000); /* sleep a bit to avoid busy loop */
	    gu_mutex_lock (&f->lock);
	}
	f->length = 0;

	/* now all we have - "get" threads waiting for lock or signal */
	while (pthread_cond_destroy (&f->get_cond)) {
            if (f->get_wait <= 0) {
                gu_fatal ("Can't destroy condition while nobody's waiting");
                abort();
            }
            f->get_wait = 0;
            gu_cond_broadcast (&f->get_cond);
	}

	/* at this point there are only functions waiting for lock */
	gu_mutex_unlock (&f->lock);
	while (gu_mutex_destroy (&f->lock)) {
	    /* this should be fast provided safe get and safe put are
	     * wtitten correctly. They should immediately freak out. */
	    gu_mutex_lock   (&f->lock);
	    gu_mutex_unlock (&f->lock);
	}

	/* now nobody's waiting for anything */
	gu_free (f->queue);
	gu_free (f);
	return 0;
    }
    return -EINVAL;
}
